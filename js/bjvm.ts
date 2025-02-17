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

let wasmFile: string | null = null;
let factory: Promise<void>;

interface VMOptions {
    classpath: string;
    heapSize?: number;  // default is 2^26
    stdout?: (buf: number, len: number) => void;
    stderr?: (buf: number, len: number) => void;
}

function buffered() {
    let buffer = "";
    return (buf: number, len: number) => {
        buffer += new TextDecoder().decode(new Uint8Array(module.HEAPU8.buffer, buf, len));
        let idx: number;
        while ((idx = buffer.indexOf('\n')) != -1) {
            console.log(buffer.slice(0, idx));
            buffer = buffer.slice(idx + 1);
        }
    }
}

type JavaType = {
    kind: 'L' | 'I' | 'F' | 'D' | 'J' | 'S' | 'B' | 'C' | 'Z' | 'V';
    className: string;
};

function readJavaType(vm: VM, addr: number, type: JavaType): any {
    switch (type.kind) {
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

function explainObject(value: any) {
    if (value === null) {
        return "null";
    }
    if (typeof value === "object") {
        return value.constructor.name;
    }
    return typeof value;
}

function setJavaType(thread: Thread, addr: number, type: JavaType, value: any) {
    switch (type.kind) {
        case "L":
            if (value === null) {
                module.setValue(addr, 0, "i32");
                return;
            }
            if (typeof value === "string") {
                const strPtr = thread.createString(value);
                module.setValue(addr, strPtr, "i32");
                return;
            }
            if (!(value instanceof BaseHandle)) {
                throw new TypeError("Expected BaseHandle, not " + explainObject(value));
            }
            let obj = module._deref_js_handle(thread.vm.ptr, value.handleIndex);
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
    returnType: JavaType;
    parameterTypes: JavaType[];
};

function makePrimitiveArrayName(descElement: string, dims: number) {
    return "[".repeat(dims) + descElement;
}

function makeArrayName(s: string, dims: number) {
    return "[".repeat(dims) + "L" + s;
}

function parseParameterType(desc: string, i: number, parameterTypes: JavaType[]) {
    let dims = 0;
    while (desc[i] === '[') {
        ++i;
        ++dims;
    }
    if (desc[i] == 'L') {
        let j = i + 1;
        while (desc[i] !== ';') {
            i++;
        }
        parameterTypes.push({ kind: 'L', className: makeArrayName(desc.substring(j, i), dims) } as JavaType);
    } else {
        parameterTypes.push(dims ?
            { kind: 'L', className: makePrimitiveArrayName(desc[i], dims) } :
            { kind: desc[i], className: "" } as JavaType);
    }
    i++;
    return i;
}

function parseMethodDescriptor(method: MethodInfo): ParsedMethodDescriptor {
    if (method.parsedDescriptor)
        return method.parsedDescriptor
    const desc = method.descriptor;
    let i = 1;
    let parameterTypes: JavaType[] = [];
    while (desc[i] !== ')') {
        i = parseParameterType(desc, i, parameterTypes);
    }
    const returnType: JavaType[] = [];
    if (desc[i + 1] === 'V') {
        returnType.push({ kind: 'V', className: "" });
    } else {
        parseParameterType(desc, i + 1, returnType);
    }
    return method.parsedDescriptor = { returnType: returnType[0]!, parameterTypes };
}

type MethodInfo = {
    name: string;
    descriptor: string;
    methodPointer: number;
    accessFlags: number;
    parameterNames: string[];
    index: number;
    parsedDescriptor?: ParsedMethodDescriptor;
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

    drop() {
        if (this.handleIndex === -1) {  // already dropped
            return;
        }
        this.vm.handleRegistry.unregister(this);
        module._drop_js_handle(this.vm.ptr, this.handleIndex);
        this.handleIndex = -1;
    }
}

function binaryNameToJSName(name: string) {
    return name.replaceAll('/', "_");
}

function stringifyParameter(type: JavaType) {
    switch (type.kind) {
        case 'L':
            return type.className.replaceAll('/', "_");
        default:
            return type.kind;
    }
}

class OverloadResolver {
    grouped: Map<string /* method name */, Map<number /* argc */, MethodInfo[]> >;

    constructor() {
        this.grouped = new Map();
    }

    addMethod(method: MethodInfo) {
        if (!this.grouped.has(method.name)) {
            this.grouped.set(method.name, new Map());
        }
        let group = this.grouped.get(method.name);
        if (!group.has(method.parameterNames.length)) {
            group.set(method.parameterNames.length, []);
        }
        group.get(method.parameterNames.length).push(method);
    }

    flattenCollisions() {
        // For each method/argc combination with more than one possible method, generate a new method name
        // with the parameter types appended to it
        for (let [name, group] of this.grouped.entries()) {
            for (let [argc, methods] of group.entries()) {
                if (methods.length < 2) {
                    continue;
                }

                for (let i = 0; i < methods.length; ++i) {
                    let method = methods[i];
                    let desc = parseMethodDescriptor(method);
                    let newName = `${name}_$${desc.parameterTypes.map(stringifyParameter).join('$')}`;

                    this.grouped.set(newName, new Map([[argc, [method]]]));
                }

                group.delete(argc);
            }
        }
    }

    createImpls(classInfo: ClassInfo, static_: boolean): string {
        let impls: string[] = [];
        this.flattenCollisions();

        function escape(name: string) {
            return JSON.stringify(name);
        }

        // For each NAME, generate an implementation which checks the # of args, then calls the appropriate method
        for (let [name, group] of this.grouped.entries()) {
            const isCtor = name.startsWith("<init>");
            const staticLike = static_ || isCtor;
            const maybeStatic = staticLike ? 'static' : '';
            const maybeThis = staticLike ? '' : 'this,';
            const runner = isCtor ? "_runConstructor" : "_runMethod";
            let impl = `${maybeStatic} ${escape(name)} () {
                let i = 0;
                switch (arguments.length) {
                    ${[...group.entries()].map(([argc, method]) => {
                        const m = method[0];
                        return `case ${argc}: { i = ${m.index}; break; }`;
                    }).join('\n')}
                    default:
                    throw new RangeError("Invalid number of arguments (expected one of ${[...group.keys()].join(', ')}, got " + arguments.length + ")");
                }
                const thread = vm.createThread();
                return thread.${runner}(methods[i], ${maybeThis} ...arguments);
            }`;
            impls.push(impl);
        }

        return impls.join('\n');
    }

    createTSDefs() {

    }
}

interface HandleConstructor {
    new (): typeof BaseHandle;
}

function createClassImpl(vm: VM, bjvm_classdesc_ptr: number): HandleConstructor {
    const classInfoStr = module._ffi_get_class_json(bjvm_classdesc_ptr);
    const info = module.UTF8ToString(classInfoStr);
    const classInfo: ClassInfo = JSON.parse(info);
    module._free(classInfoStr);

    const instanceResolver = new OverloadResolver();
    const staticResolver = new OverloadResolver();

    // Static methods
    for (let i = 0 ; i < classInfo.methods.length; ++i) {
        let method = classInfo.methods[i];
        if (!(method.accessFlags & 0x0001)) {
            continue;
        }
        if (method.accessFlags & 0x0008) {
            staticResolver.addMethod(method);
        } else {
            instanceResolver.addMethod(method);
        }
    }

    const cow = binaryNameToJSName(classInfo.binaryName);
    const body = `class ${cow} extends BaseHandle {
    ${instanceResolver.createImpls(classInfo, false)}
    ${staticResolver.createImpls(classInfo, true)}
    }; return ${cow}`;

    const Class = new Function("name", "BaseHandle", "vm", "methods", body)(classInfo.binaryName, BaseHandle, vm, classInfo.methods);
    Object.defineProperty(Class, 'name', { value: classInfo.binaryName });

    return Class;
}

const _BRANDS = Symbol("BRANDS");

declare class TSJavaType {
    [_BRANDS]: {
        "TSJavaType": [];
    }
}

// Superclass
export declare class AbstractList<E extends TSJavaType> {
    get(index: number): Promise<E>;

    [_BRANDS]: {
        "TSJavaType": [];
        "AbstractList": [];
        "List": [E];
    }
}

// "Superinterface"
export declare class List<E extends TSJavaType> {
    [_BRANDS]: {
        "TSJavaType": [];
        "List": [E];
    }
}

const _PRIMITIVE = Symbol("PRIMITIVE");

type JavaPrimitive<N extends string, T> = T & {[_PRIMITIVE]: N}
type JavaInt = JavaPrimitive<"int", number>;
type JavaFloat = JavaPrimitive<"float", number>;

function makeInt(n: number): JavaInt {
    if (n < 2 ** 31 && n >= -(2 ** 31) && Number.isInteger(n)) {
        return (n | 0) as JavaInt;
    }
    throw new Error("NO!");
}

function makeFloat(n: number): JavaFloat {
    return Math.fround(n) as JavaFloat;
}

export declare class ArrayList<E extends TSJavaType> extends AbstractList<E> {
    private static vm: number;
    constructor(capacity: number);  // VERY GOOD. DO NOT YIELD TO ASYNC IN THE CONSTRUCTOR

    add(item: E): Promise<boolean>;
    add(index: JavaInt | Integer, item: E);

    pox(float: JavaFloat);
    pox(integer: JavaInt | Integer);

    [_BRANDS]: {
        "TSJavaType": [];
        "ArrayList": [];
        "AbstractList": [];
        "List": [E];
    }
}

export declare class Integer {
    static "valueOf_$I": (value: number) => Promise<Integer>;

    static cow(list: List<Integer>);

    [_BRANDS]: {
        "TSJavaType": [];
        "Integer": [];
    }
}

export declare class Multithreading {
    static main(args: any): ForceablePromise<void>;

    [_BRANDS]: {
        "TSJavaType": [];
        "Multithreading": [];
    }
}

// A promise type for which we can also attempt to call the function synchronously.
type ForceablePromise<T> = Promise<T> & {
    // - The promise resolves normally if __forceSync is not called.
    // - If __forceSync is called after the promise has resolved, it throws a JS error.
    // - If __forceSync is called before the promise has resolved, the promise will be discarded and never resolve.
    // - If __forceSync fails, it will return { success: false, illegalStateException: e } where e is an error containing
    //   a backtrace to the function which forced a yielding to occur. thread->current_exception is NOT set. The promise
    //   will resolve normally.
    // - If __forceSync succeeds, it will return { success: true }. The current exception must still be checked in case
    //   an error was thrown.
    //
    // This function is intended for internal use only, by the forceSync and attemptSync APIs.
    __forceSync(raiseIllegalState: boolean): { success: true, value: T } | { success: false, illegalStateException: BaseHandle | null};
};

export function sync<T>(toExecute: ForceablePromise<T>): T {
    const result = toExecute.__forceSync(true);
    if (result.success) {
        return result.value;
    } else if (result.success === false /* convince TS */) {
        throw result.illegalStateException;
    }
}


type LoadedClassMap = {
    "java/util/ArrayList": typeof ArrayList;
    "java/lang/Integer": typeof Integer;
    "NBodyProblem": typeof Multithreading;
};
export type LoadableClassName = keyof LoadedClassMap;
type LoadedClass<T extends LoadableClassName> = LoadedClassMap[T];

class Thread {
    vm: VM;
    ptr: number;

    constructor(vm: VM, ptr: number) {
        this.vm = vm;
        this.ptr = ptr;
    }

    private async _runConstructor(method: MethodInfo, ...args: any[]): Promise<any> {
        const this_ = module._ffi_allocate_object(this.ptr, method.methodPointer);
        if (!this_) {
            this.throwThreadException();
        }
        const handle = this.vm.createHandle(this_);
        await this._runMethod(method, handle, ...args);
        return handle;
    }

    // Create a raw (unmanaged) pointer to a java/lang/String
    createString(str: string): number {
        const malloced = module._malloc(2 * str.length);
        const arr = new Uint8Array(module.HEAPU8.buffer, malloced, str.length);  // TODO check for null bytes
        const result = new TextEncoder().encodeInto(str, arr);
        const ptr = module._ffi_create_string(this.ptr, malloced, result.written);
        module._free(malloced);
        this.throwThreadException();
        return ptr;
    }

    private _runMethod(method: MethodInfo, ...args: any[]): ForceablePromise<any> {
        let argsPtr = module._malloc(args.length * 8);
        let executionRecord = 0;

        let freed = false;
        function freeData() {
            if (freed) {
                return;
            }
            freed = true;
            if (executionRecord)
                module._ffi_free_execution_record(executionRecord);
            module._free(argsPtr);
        }

        try {
            const parsed = parseMethodDescriptor(method);
            let isInstanceMethod = !(method.accessFlags & 0x0008);
            let j = 0;
            if (isInstanceMethod) {
                setJavaType(this, argsPtr, { kind: 'L', className: "" }, args[0]);
                j++;
            }
            const nonInstanceArgc = args.length - +isInstanceMethod;
            for (let i = 0; i < nonInstanceArgc; i++, j++) {
                setJavaType(this, argsPtr + j * 8, parsed.parameterTypes[i], args[j]);
            }
            const scheduled = this.vm.scheduleMethod(this, method, argsPtr);
            executionRecord = scheduled.record;
            const readResult = () => {
                if (parsed.returnType.kind !== 'V') {
                    const resultPtr = module._ffi_get_execution_record_result_pointer(executionRecord);
                    return readJavaType(this.vm, resultPtr, parsed.returnType);
                }
            }

            const promise = (async () => {
                if (!await scheduled.waitForResolution) {
                    return;  // cancelled
                }
                try {
                    this.throwThreadException();
                    return readResult();
                } finally {
                    freeData();
                }
            })() as ForceablePromise<any>;

            promise.__forceSync = (raiseIllegalState: boolean) => {
                const status = module._ffi_execute_immediately(scheduled.record);
                if (status == 0 /* DONE */) {
                    const ret: ReturnType<typeof promise.__forceSync> = { success: true, value: readResult() };
                    freeData();
                    return ret;
                } else {
                    const ptr = module._ffi_get_current_exception(this.ptr);
                    const handle = this.vm.createHandle(ptr) as BaseHandle | null;
                    return { success: false, illegalStateException: handle };
                }
            };

            return promise;
        } catch (e: any) {
            freeData();
            throw e;
        }
    }

    throwThreadException() {
        let ptr = module._ffi_get_current_exception(this.ptr);
        if (!ptr) {
            return;
        }
        const handle = this.vm.createHandle(ptr);
        module._ffi_clear_current_exception(this.ptr);
        throw handle;
    }

    async loadClass<N extends LoadableClassName>(name: N): Promise<LoadedClass<N>> {
        let namePtr = module._malloc(name.length + 1);
        new TextEncoder().encodeInto(name, new Uint8Array(module.HEAPU8.buffer, namePtr, name.length));
        module.HEAPU8[namePtr + name.length] = 0;
        let ptr = module._ffi_get_class(this.ptr, namePtr);
        if (!ptr) {
            this.throwThreadException();
        }
        const clazz = this.vm.getClassForDescriptor(ptr);
        module._free(namePtr);
        return clazz;
    }
}

class VM {
    ptr: number;
    handleRegistry: FinalizationRegistry<BaseHandle> = new FinalizationRegistry((handle) => {
        module._drop_js_handle(this.ptr, handle.handleIndex);
    });
    namedClasses: Map<number /* bjvm_classdesc* */, any> = new Map();
    cachedThread: Thread;

    scheduler: number;
    timeout: number; // if not -1, then a scheduler step is scheduled

    waitingForYield: number;
    pending: Function[] = [];  // hook here to be called every time the timeout fires

    private constructor(options: VMOptions) {
        let classpath = module._malloc(options.classpath.length + 1);
        new TextEncoder().encodeInto(options.classpath, new Uint8Array(module.HEAPU8.buffer, classpath, options.classpath.length));
        module.HEAPU8[classpath + options.classpath.length] = 0;

        options.stdout ??= buffered();
        options.stderr ??= buffered();

        this.timeout = -1;
        this.ptr = module._ffi_create_vm(classpath, options.heapSize ?? 1 << 26, module.addFunction(options.stdout, 'viii'), module.addFunction(options.stderr, 'viii'));
        module._free(classpath);

        this.scheduler = module._ffi_create_rr_scheduler(this.ptr);
    }

    static module: MainModule;

    scheduleTimeout(waitUs: number = 0) {
        if (this.waitingForYield > waitUs && this.timeout !== -1) {
            clearTimeout(this.timeout);
            this.timeout = -1;
        }
        if (this.timeout === -1) {
            this.timeout = setTimeout(() => {
                this.timeout = -1;
                const status = module._ffi_rr_scheduler_step(this.scheduler);
                if (status !== 0) {
                    const waitUs = module._ffi_rr_scheduler_wait_for_us(this.scheduler);
                    this.scheduleTimeout(this.waitingForYield = waitUs);
                }
                for (let i = 0; i < this.pending.length; i++) {
                    this.pending[i]();
                }
                this.pending.length = 0;
            }, waitUs / 1000);
        }
    }

    // Low-level method scheduling apparatus
   scheduleMethod(thread: Thread, method: MethodInfo, argsPtr: number):
        { record: number, waitForResolution: Promise<boolean>, cancelResolution: () => void } {
        let record = module._ffi_rr_schedule(thread.ptr, method.methodPointer, argsPtr);
        let cancelled = false;
        let resolve_: Function;

        const waitForResolution = (async () => {
            while (!module._ffi_rr_record_is_ready(record)) {
                if (cancelled) {
                    return false;
                }

                this.scheduleTimeout();
                await new Promise((resolve) => {
                    resolve_ = resolve;
                    this.pending.push(resolve);
                });
            }

            return true;
        })();

        const cancelResolution = () => {
            cancelled = true;
            for (let i = 0; i < this.pending.length; i++) {
                if (this.pending[i] === resolve_) {
                    this.pending.splice(i, 1);
                    break;
                }
            }
        };

        return { record, waitForResolution, cancelResolution };
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

    createHandle(ptr: number): BaseHandle | string | null {
        if (ptr === 0) return null;

        let isString = module._ffi_is_string(ptr);
        if (isString) {
            const chars = module._ffi_get_string_data(ptr);
            const length = module._ffi_get_string_len(ptr);
            const coder = module._ffi_get_string_coder(ptr);
            enum Coder { STRING_CODER_LATIN1 = 0, STRING_CODER_UTF16 = 1 }
            if (coder === Coder.STRING_CODER_LATIN1) {
                return new TextDecoder('latin1').decode(new Uint8Array(module.HEAPU8.buffer, chars, length));
            } else {
                return new TextDecoder('utf16').decode(new Uint8Array(module.HEAPU8.buffer, chars, length));
            }
        }

        let handleIndex = module._make_js_handle(this.ptr, ptr);
        let classdesc = module._ffi_get_classdesc(ptr);

        const clazz = this.getClassForDescriptor(classdesc);
        const handle = Object.create(clazz.prototype);  // do this to avoid calling the constructor
        handle.vm = this;
        handle.handleIndex = handleIndex;
        return handle;
    }

    createThread(): Thread {
        if (this.cachedThread) return this.cachedThread;

        let thread = module._ffi_create_thread(this.ptr);
        return this.cachedThread = new Thread(this, thread);
    }
}

const runtimeFilesList = `./jdk23/lib/modules
./jdk23/lib/security/default.policy
./jdk23/conf/security/java.security
./jdk23/conf/security/java.policy
./jdk23.jar
./test_files/basic_multithreading/Multithreading.class
./test_files/basic_multithreading/MultithreadingDemo.class
./test_files/n_body_problem/NBodyProblem$Body.class
./test_files/n_body_problem/NBodyProblem.class`.split('\n');

export function appendRuntimeFiles(files: string[]) {
    runtimeFilesList.push(...files);
}

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
    // Delete the existing file
    return new Promise((resolve, reject) => {
        const req = db.transaction(dbName, "readwrite").objectStore(dbName).delete(name);
        req.onsuccess = req.onerror = () => {
            const transaction = db.transaction(dbName, "readwrite");
            const store = transaction.objectStore(dbName);

            const file = { name, data }; // Object with name and Uint8Array
            const request = store.add(file);

            request.onsuccess = () => resolve();
            request.onerror = (event) => reject((event.target as IDBRequest).error);
        }
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

const TOTAL_BYTES = 0;

export function setWasmLocation(location: string) {
    wasmFile = location;
    factory = MainModuleFactory({
        locateFile: (path: string) => {
            if (path.endsWith(".wasm")) {
                return wasmFile;
            }
            return path;
        }
    }).then((m) => {
        module = m;
        VM.module = m;
    });
}

async function installRuntimeFiles(baseUrl: string, progress?: (loaded: number, total: number) => void) {
    let totalLoaded = 0;

    const db = await openDatabase();

    // Spawn fetch requests
    const requests = runtimeFilesList.map(async (file) => {

        // Check whether the file is already in the database
        /*const cached = await getFile(db, file);
        if (cached) {
            totalLoaded += cached.data.length;
            progress?.(totalLoaded, TOTAL_BYTES);
            return {file, data: cached.data};
        }*/

        const response = await fetch(`${baseUrl}/${file}`, {
            method: 'GET',
            headers: {
                'Content-Type': 'application/octet-stream',
            }
        });
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
        // Insert into the DB
        // await addFile(db, file, data);
        return {file, data};
    });

    // Wait for all requests to finish
    const results = await Promise.all(requests);

    // Wait for the WASM module to be done
    await factory;

    let existingFolders: Set<string> = new Set();
    let existingFiles: Set<string> = new Set();

    // Add to file system
    results.forEach(({file, data}) => {
        // make directories up to last /
        while (file[0] == '.') file = file.substring(1);
        if (existingFiles.has(file)) {
            return;
        }
        existingFiles.add(file);
        if (!file)
            throw new Error("Invalid file name: " + file)
        for (let i = 1; i < file.length; i++) {
            if (file[i] === '/') {
                const dir = file.substring(0, i);
                if (!existingFolders.has(dir)) {
                    module.FS.mkdir(dir, 0o777);
                    existingFolders.add(dir)
                }
            }
        }
        module.FS.writeFile(file, data);
    });

    pending.forEach(p => p.resolve());
    pending.length = 0
}

export { VM, VMOptions, installRuntimeFiles, type HandleConstructor, BaseHandle, Thread };