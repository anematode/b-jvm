import MainModuleFactory from "../build/bjvm_main.js"
import type { MainModule } from "../build/bjvm_main.d.ts"

let module: MainModule;
let error: any;
let pending: {resolve: Function, reject: Function}[] = [];

async function loaded() {
    if (module)
        return;
    if (error)
        throw error;
    return new Promise((resolve, reject) => {
        pending.push({resolve, reject});
    });
}

const factory = MainModuleFactory();
factory.then((instantiated) => {
    module = instantiated;
});

interface VMOptions {
    classpath: string;
    stdout?: (byte: number) => void;
    stderr?: (byte: number) => void;
}

function buffered() {
    let buffer = "";
    return (byte: number) => {
        if (byte === 10) {
            console.log(buffer);
            buffer = "";
        } else {
            buffer += String.fromCharCode(byte);
        }
    }
}

type JavaType = 'L' | 'I' | 'F' | 'D' | 'J' | 'S' | 'B' | 'C' | 'Z';

function readJavaType(vm: VM, addr: number, type: JavaType): any {
    switch (type) {
        case "L":
            return vm.createHandle(module.getValue(addr, "i32"));
        case "I":
            return module.getValue(addr, "i32");
        case "F":
            return module.getValue(addr, "float");
        case "D":
            return module.getValue(addr, "double");
        case "J":
            return module.getValue(addr, "i64");
        case "S":
            return module.getValue(addr, "i16");
        case "B":
            return module.getValue(addr, "i8");
        case "C":
            return String.fromCharCode(module.getValue(addr, "i16"));
        case "Z":
            return module.getValue(addr, "i8") !== 0;
        default:
            throw new Error("Invalid java type: " + type)
    }
}

function setJavaType(vm: VM, addr: number, type: JavaType, value: any) {
    switch (type) {
        case "L":
            if (value === null) {
                module.setValue(addr, 0, "i32");
                return;
            }
            if (!(value instanceof BaseHandle)) {
                throw new TypeError("Expected BaseHandle");
            }
            let obj = module._bjvm_deref_js_handle(vm.ptr, value.handleIndex);
            module.setValue(addr, obj, "i32");
            return;
        case "I":
            module.setValue(addr, value, "i32");
            return;
        case "F":
            module.setValue(addr, value, "float");
            return;
            case "D":
            module.setValue(addr, value, "double");
            return;
            case "J":
            module.setValue(addr, value, "i64");
            return;
            case "S":
                module.setValue(addr, value, "i16");
                return;
                case "B":
                module.setValue(addr, value, "i8");
                return;
                case "C":
                module.setValue(addr, value.charCodeAt(0), "i16");
                return;
                case "Z":
                module.setValue(addr, value ? 1 : 0, "i8");
                return;
                default:
                    throw new Error("Invalid java type: " + type)
    }
}

type FieldInfo = {
    name: string;
    type: JavaType;
    accessFlags: number;
    byteOffset: number;
};

type ParsedMethodDescriptor = {
    returnType: JavaType | 'V';
    parameterTypes: JavaType[];
};

function parseMethodDescriptor(desc: string): ParsedMethodDescriptor {
    let i = 1;
    let parameterTypes: JavaType[] = [];
    while (desc[i] !== ')') {
        let dims = 0;
        while (desc[i] === '[') {
            ++i;
            ++dims;
        }
        parameterTypes.push(dims ? 'L' : desc[i] as JavaType);
        if (desc[i] == 'L') {
            while (desc[i] !== ';') {
                i++;
            }
        }
        i++;
    }
    const returnType = desc[i + 1] as JavaType;
    return { returnType, parameterTypes };
}

type MethodInfo = {
    name: string;
    descriptor: string;
    methodPointer: number;
    accessFlags: number;
    parameterNames: string[];
};

type ClassInfo = {
    binaryName: string,
    fields: FieldInfo[],
    methods: MethodInfo[],
};

// Handle to a Java object
class BaseHandle {
    vm: VM;
    handleIndex: number;

    constructor(vm: VM, handleIndex: number) {
        this.vm = vm;
        this.handleIndex = handleIndex;
    }

    drop() {
        this.vm.handleRegistry.unregister(this);
        module._bjvm_drop_js_handle(this.vm.ptr, this.handleIndex);
    }
}


function createClassImpl(vm: VM, bjvm_classdesc_ptr: number): any {
    const classInfoStr = module._bjvm_ffi_get_class_json(bjvm_classdesc_ptr);
    const info = module.UTF8ToString(classInfoStr);
    const classInfo: ClassInfo = JSON.parse(info);
    module._free(classInfoStr);

    // Static methods
    const staticMethods: string[] = [];
    const instanceMethods: string[] = [];
    let i = 0;
    for (const method of classInfo.methods) {
        let m = method;
        const argNames = method.parameterNames.join(", ");
        if (method.accessFlags & 0x0008) {
            staticMethods.push(`static async ${m.name} (${argNames}) {
                const thread = vm.createThread();
                const result = await thread._runMethod(methods[${i}], ${argNames});
                return result;
            }`);
        } else {
            instanceMethods.push(`async ${m.name} (${argNames}) {
                const thread = vm.createThread();
                const result = await thread._runMethod(methods[${i}], this, ${argNames});
                return result;
            }`);
        }
        ++i;
    }

    const cow = classInfo.binaryName.replaceAll('/', "_");
    const body = `class ${cow} extends BaseHandle {
    ${staticMethods.join(' ')}
    ${instanceMethods.join(' ')}
    }; return ${cow}`;

    const Class = new Function("name", "BaseHandle", "vm", "methods", body)(classInfo.binaryName, BaseHandle, vm, classInfo.methods);
    Object.defineProperty(Class, 'name', { value: classInfo.binaryName });

    return Class;
}

class Thread {
    vm: VM;
    ptr: number;

    constructor(vm: VM, ptr: number) {
        this.vm = vm;
        this.ptr = ptr;
    }

    private async _runMethod(method: MethodInfo, ...args: any[]): Promise<any> {
        let argsPtr = module._malloc(args.length * 8);
        let resultPtr = module._malloc(8);
        try {
            const parsed = parseMethodDescriptor(method.descriptor);
            let isInstanceMethod = !(method.accessFlags & 0x0008);
            let j = 0;
            if (isInstanceMethod) {
                setJavaType(this.vm, argsPtr, 'L', args[0]);
                j++;
            }
            for (let i = 0; i < args.length - +isInstanceMethod; i++, j++) {
                setJavaType(this.vm, argsPtr + j * 8, parsed.parameterTypes[i], args[j]);
            }
            let ctx = module._bjvm_thread_async_run(this.ptr, method.methodPointer, argsPtr, resultPtr);
            while (!module._bjvm_async_run_step(ctx)) {
                await new Promise(resolve => setTimeout(resolve, 0));
            }
            this.throwThreadException();
            if (parsed.returnType !== 'V') {
                return readJavaType(this.vm, resultPtr, parsed.returnType);
            }
        } finally {
            module._free(argsPtr);
            module._free(resultPtr);
        }
    }

    throwThreadException() {
        let ptr = module._bjvm_ffi_get_current_exception(this.ptr);
        if (!ptr) {
            return;
        }
        const handle = this.vm.createHandle(ptr);
        module._bjvm_ffi_clear_current_exception(this.ptr);
        throw handle;
    }

    async loadClass(name: string): Promise<any> {
        let namePtr = module._malloc(name.length + 1);
        new TextEncoder().encodeInto(name, new Uint8Array(module.HEAPU8.buffer, namePtr, name.length));
        module.HEAPU8[namePtr + name.length] = 0;
        let ptr = module._bjvm_ffi_get_class(this.ptr, namePtr);
        if (!ptr) {
            this.throwThreadException();
        }
        const clazz = createClassImpl(this.vm, ptr);
        module._free(namePtr);
        return clazz;
    }
}

class VM {
    ptr: number;
    handleRegistry: FinalizationRegistry<BaseHandle> = new FinalizationRegistry((handle) => {
        module._bjvm_drop_js_handle(this.ptr, handle.handleIndex);
    });
    namedClasses: Map<number /* bjvm_classdesc* */, any> = new Map();
    cachedThread: Thread;

    private constructor(options: VMOptions) {
        let classpath = module._malloc(options.classpath.length + 1);
        new TextEncoder().encodeInto(options.classpath, new Uint8Array(module.HEAPU8.buffer, classpath, options.classpath.length));
        module.HEAPU8[classpath + options.classpath.length] = 0;

        options.stdout ??= buffered();
        options.stderr ??= buffered();

        this.ptr = module._bjvm_ffi_create_vm(classpath, module.addFunction(options.stdout, 'vii'), module.addFunction(options.stderr, 'vii'));
        module._free(classpath);
    }

    static async create(options: VMOptions) {
        await loaded();
        return new VM(options);
    }

    getClassForDescriptor(classdesc: number): any {
        if (this.namedClasses.has(classdesc)) {
            return this.namedClasses.get(classdesc);
        }

        const made = createClassImpl(this, classdesc);
        this.namedClasses.set(classdesc, made);
        return made;
    }

    createHandle(ptr: number) {
        if (ptr === 0) return null;

        let handleIndex = module._bjvm_make_js_handle(this.ptr, ptr);
        let classdesc = module._bjvm_ffi_get_classdesc(ptr);
        const clazz = this.getClassForDescriptor(classdesc);
        return new clazz(this, handleIndex);
    }

    createThread(): Thread {
        if (this.cachedThread) return this.cachedThread;

        let thread = module._bjvm_ffi_create_thread(this.ptr);
        return this.cachedThread = new Thread(this, thread);
    }

    async runMain(main: string, main2: string, param3: any[]) {
    }
}

const runtimeFilesList = `jre/lib/cmm/sRGB.pf
jre/lib/cmm/CIEXYZ.pf
jre/lib/cmm/PYCC.pf
jre/lib/cmm/GRAY.pf
jre/lib/cmm/LINEAR_RGB.pf
jre/lib/tzmappings
jre/lib/psfontj2d.properties
jre/lib/fontconfig.properties.src
jre/lib/logging.properties
jre/lib/jconsole.jar
jre/lib/sound.properties
jre/lib/currency.data
jre/lib/calendars.properties
jre/lib/hijrah-config-umalqura.properties
jre/lib/security/public_suffix_list.dat
jre/lib/security/trusted.libraries
jre/lib/security/blacklisted.certs
jre/lib/security/java.security
jre/lib/security/blacklist
jre/lib/security/java.policy
jre/lib/security/cacerts
jre/lib/security/javaws.policy
jre/lib/security/policy/unlimited/local_policy.jar
jre/lib/security/policy/unlimited/US_export_policy.jar
jre/lib/security/policy/limited/local_policy.jar
jre/lib/security/policy/limited/US_export_policy.jar
jre/lib/jfr/default.jfc
jre/lib/jfr/profile.jfc
jre/lib/images/cursors/win32_MoveDrop32x32.gif
jre/lib/images/cursors/cursors.properties
jre/lib/images/cursors/win32_CopyDrop32x32.gif
jre/lib/images/cursors/win32_LinkDrop32x32.gif
jre/lib/images/cursors/win32_LinkNoDrop32x32.gif
jre/lib/images/cursors/invalid32x32.gif
jre/lib/images/cursors/win32_CopyNoDrop32x32.gif
jre/lib/images/cursors/win32_MoveNoDrop32x32.gif
jre/lib/orb.idl
jre/lib/net.properties
jre/lib/rt.jar
jre/lib/deploy/messages_de.properties
jre/lib/deploy/splash.gif
jre/lib/deploy/splash_11-lic.gif
jre/lib/deploy/messages_ko.properties
jre/lib/deploy/messages_fr.properties
jre/lib/deploy/ffjcext.zip
jre/lib/deploy/messages_zh_TW.properties
jre/lib/deploy/messages_pt_BR.properties
jre/lib/deploy/messages_zh_HK.properties
jre/lib/deploy/messages_ja.properties
jre/lib/deploy/messages_zh_CN.properties
jre/lib/deploy/messages_es.properties
jre/lib/deploy/messages_sv.properties
jre/lib/deploy/messages_it.properties
jre/lib/deploy/splash@2x.gif
jre/lib/deploy/splash_11@2x-lic.gif
jre/lib/deploy/messages.properties
jre/lib/javafx.properties
jre/lib/accessibility.properties
jre/lib/tzdb.dat
jre/lib/tools.jar
jre/lib/ext/sunmscapi.jar
jre/lib/ext/sunec.jar
jre/lib/ext/nashorn.jar
jre/lib/ext/access-bridge-64.jar
jre/lib/ext/cldrdata.jar
jre/lib/ext/jfxrt.jar
jre/lib/ext/dnsns.jar
jre/lib/ext/localedata.jar
jre/lib/ext/sunjce_provider.jar
jre/lib/ext/meta-index
jre/lib/ext/sunpkcs11.jar
jre/lib/ext/jaccess.jar
jre/lib/ext/zipfs.jar
jre/lib/amd64/jvm.cfg
jre/lib/deploy.jar
jre/lib/management/jmxremote.access
jre/lib/management/snmp.acl.template
jre/lib/management/management.properties
jre/lib/management/jmxremote.password.template
jre/lib/javaws.jar
jre/lib/jfr.jar
jre/lib/jsse.jar
jre/lib/plugin.jar
jre/lib/ant-javafx.jar
jre/lib/jfxswt.jar
jre/lib/ct.sym
jre/lib/charsets.jar
jre/lib/resources.jar
jre/lib/jvm.hprof.txt
jre/lib/javafx-mx.jar
jre/lib/packager.jar
jre/lib/content-types.properties
jre/lib/fontconfig.bfc
jre/lib/dt.jar
jre/lib/meta-index
jre/lib/management-agent.jar
jre/lib/jce.jar
jre/lib/flavormap.properties
jre/lib/psfont.properties.ja
jre/lib/fonts/LucidaSansRegular.ttf
jre/lib/fonts/LucidaTypewriterRegular.ttf
jre/lib/fonts/LucidaBrightDemiBold.ttf
jre/lib/fonts/LucidaSansDemiBold.ttf
jre/lib/fonts/LucidaBrightRegular.ttf
jre/lib/fonts/LucidaBrightItalic.ttf
jre/lib/fonts/LucidaBrightDemiItalic.ttf
jre/lib/fonts/LucidaTypewriterBold.ttf
jre/lib/classlist
jre/lib/ir.idl
jre/lib/jexec
Main.class
jre/lib/sa-jdi.jar`.split('\n');

const dbName = 'bjvm';

function openDatabase(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
        const request = indexedDB.open("FileStorageDB", 1);

        request.onupgradeneeded = (event) => {
            const db = (event.target as IDBRequest).result as IDBDatabase;
            if (!db.objectStoreNames.contains(dbName)) {
                db.createObjectStore(dbName, { keyPath: "name" });
            }
        };

        request.onsuccess = (event) => {
            resolve((event.target as IDBRequest).result as IDBDatabase);
        };

        request.onerror = (event) => {
            reject((event.target as IDBRequest).error);
        };
    });
}

function addFile(db: IDBDatabase, name: string, data: Uint8Array): Promise<void> {
    return new Promise((resolve, reject) => {
        const transaction = db.transaction(dbName, "readwrite");
        const store = transaction.objectStore(dbName);

        const file = { name, data }; // Object with name and Uint8Array
        const request = store.add(file);

        request.onsuccess = () => resolve();
        request.onerror = (event) => reject((event.target as IDBRequest).error);
    });
}

function getFile(db: IDBDatabase, name: string): Promise<{ name: string; data: Uint8Array } | undefined> {
    return new Promise((resolve, reject) => {
        const transaction = db.transaction(dbName, "readonly");
        const store = transaction.objectStore(dbName);

        const request = store.get(name);

        request.onsuccess = (event) => resolve((event.target as IDBRequest).result as { name: string; data: Uint8Array });
        request.onerror = (event) => reject((event.target as IDBRequest).error);
    });
}

const TOTAL_BYTES = 152392939;

async function installRuntimeFiles(baseUrl: string, progress?: (loaded: number, total: number) => void) {
    let totalLoaded = 0;

    // Spawn fetch requests
    const requests = runtimeFilesList.map(async (file) => {
        const response = await fetch(`${baseUrl}/${file}`);
        const contentLength = response.headers.get('Content-Length');
        const total = contentLength ? parseInt(contentLength) : 0;

        const reader = response.body?.getReader();
        let loaded = 0;
        const data = new Uint8Array(total);
        while (true) {
            const {done, value} = await reader.read();
            if (done)
                break;
            data.set(value, loaded);
            loaded += value.length;
            totalLoaded += value.length;
            progress?.(totalLoaded, TOTAL_BYTES);
        }
        return {file, data};
    });

    // Wait for all requests to finish
    const results = await Promise.all(requests);

    // Wait for the WASM module to be done
    await factory;

    // Add to file system
    results.forEach(({file, data}) => {
        // make directories up to last /
        for (let i = 0; i < file.length; i++) {
            if (file[i] === '/') {
                const dir = file.substring(0, i);
                if (!module.FS.analyzePath(dir, true).exists)
                    module.FS.mkdir(dir, 0o777);
            }
        }
        module.FS.writeFile(file, data);
    });

    pending.forEach(p => p.resolve());
    pending.length = 0
    // Now cache them in IndexedDB TODO
}

export { VM, VMOptions, installRuntimeFiles };