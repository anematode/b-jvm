import MainModuleFactory, {MainModule} from "../build/bjvm_main";

type BovineOSOptions = {
    // Location of JDK runtime artifacts
    runtimeUrl: string;
    // Download progress
    progress?: (loaded: number, total: number) => void;
    // Additional files to load from the runtime directory (e.g. jars)
    additionalRuntimeFiles: string[];
    // Location of the wasm binary (use default loading logic if not provided)
    wasmLocation?: string;
    // default is console.log. If null is passed, output is suppressed.
    stdout?: (bytes: Uint8Array) => void | null;
    // default is console.error. If null is passed, output is suppressed.
    stderr?: (bytes: Uint8Array) => void | null;
    // Fetch parameters
    fetchParams?: RequestInit;
};

export const DEFAULT_HEAP_SIZE = 1 << 26;  // 64 MB

type VMOptions = {
    // Fixed at instantiation time and not growable
    heapSize?: number;
    // Colon-separated class path (just like normal Java)
    classpath: string;
    // Initial value of the preëmption frequency; can be changed later
    preemptionFrequencyUs?: number;
};

export class BovineOS {
    _module: MainModule;

    public options: BovineOSOptions;
    public FS: MainModule["FS"];

    constructor (options: BovineOSOptions, module: MainModule) {
        this.options = options;
        this._module = module;
        this.FS = module.FS;
    }

    /**
     * Create a virtual machine.
     * @param options The options for the VM.
     * @typeParam Classes The set of classes on the classpath.
     */
    makeVM<Classes>(options: VMOptions): BovineVM<Classes> {
        return new BovineVM(this, options);
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
    } else {
        throw 'unreachable';
    }
}

class BovineThread<Classes> {
    readonly ptr: number;
    vm: BovineVM<Classes>;

    constructor(vm: BovineVM<Classes>, ptr: number) {
        this.vm = vm;
        this.ptr = ptr;
    }

    async _runConstructor(method: MethodInfo, ...args: any[]): Promise<any> {
        const this_ = this.vm._module._ffi_allocate_object(this.ptr, method.methodPointer);
        if (!this_) {
            this.throwThreadException();
        }
        const handle = this.vm.createHandle(this_);
        await this._runMethod(method, handle, ...args);
        return handle;
    }

    // Create a raw (unmanaged) pointer to a java/lang/String
    createString(str: string): number {
        const module = this.vm._module;
        const malloced = module._malloc(2 * str.length);
        const arr = new Uint8Array(module.HEAPU8.buffer, malloced, str.length);  // TODO check for null bytes
        const result = new TextEncoder().encodeInto(str, arr);
        const ptr = module._ffi_create_string(this.ptr, malloced, result.written);
        module._free(malloced);
        this.throwThreadException();
        return ptr;
    }

    private _runMethod(method: MethodInfo, ...args: any[]): ForceablePromise<any> {
        const module = this.vm._module;
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
                setJavaType(this, argsPtr + j * 8, parsed.parameterTypes[i]!, args[j]!);
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

            promise.__forceSync = (_raiseIllegalState: boolean) => {
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
        const module = this.vm._module;
        let ptr = module._ffi_get_current_exception(this.ptr);
        if (!ptr) {
            return;
        }
        const handle = this.vm.createHandle(ptr);
        module._ffi_clear_current_exception(this.ptr);
        throw handle;
    }

    getArrayLength(array: BaseHandle): number {
        const module = this.vm._module;
        return module._ffi_get_array_length(ptr(array));
    }

    readArray(array: BaseHandle, index: number): any {
        const module = this.vm._module;
        let elementPtr = module._ffi_get_element_ptr(this.ptr, ptr(array), index);
        this.throwThreadException();

        let javaType: JavaType;
        let classify: Classification = module._ffi_classify_array(ptr(array));
        enum Classification {
            BYTE_ARRAY = 0,
            SHORT_ARRAY = 1,
            INT_ARRAY = 2,
            LONG_ARRAY = 3,
            FLOAT_ARRAY = 4,
            DOUBLE_ARRAY = 5,
            CHAR_ARRAY = 6,
            BOOLEAN_ARRAY = 7,
            OBJECT_ARRAY = 8
        }

        switch (classify) {
            case Classification.BYTE_ARRAY:
                javaType = { kind: 'B', className: "" };
                break;
            case Classification.SHORT_ARRAY:
                javaType = { kind: 'S', className: "" };
                break;
            case Classification.INT_ARRAY:
                javaType = { kind: 'I', className: "" };
                break;
            case Classification.LONG_ARRAY:
                javaType = { kind: 'J', className: "" };
                break;
            case Classification.FLOAT_ARRAY:
                javaType = { kind: 'F', className: "" };
                break;
            case Classification.DOUBLE_ARRAY:
                javaType = { kind: 'D', className: "" };
                break;
            case Classification.CHAR_ARRAY:
                javaType = { kind: 'C', className: "" };
                break;
            case Classification.BOOLEAN_ARRAY:
                javaType = { kind: 'Z', className: "" };
                break;
            case Classification.OBJECT_ARRAY:
                javaType = { kind: 'L', className: "" };
                break;
            default:
                throw new Error("Internal error: Bad array classification");
        }

        return readJavaType(this.vm, elementPtr, javaType);
    }

    // Join the thread, blocking until it completes all currently scheduled methods.
    async join(): Promise<void> {
    }
}

type ValidClassName<Classes> = string & keyof Classes;
type LoadClass<C> = any & { new(): C };

function buffered(call: (text: string) => void): (text: Uint8Array) => void {
    let buffer = "";
    return (text: Uint8Array) => {
        buffer += String.fromCharCode.apply(null, text);
        let idx: number;
        while ((idx = buffer.indexOf('\n')) != -1) {
            call(buffer.slice(0, idx + 1));
            buffer = buffer.slice(idx + 1);
        }
    }
}

function maybeOutOfMemory(malloced: number) {
    if (malloced === 0) {
        throw new Error("Out of memory!");
    }
}

type JavaType = {
    kind: 'L' | 'I' | 'F' | 'D' | 'J' | 'S' | 'B' | 'C' | 'Z' | 'V';
    className: string;
};


function readJavaType(vm: BovineVM<any>, addr: number, type: JavaType): any {
    const module = vm._module;
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

function setJavaType(thread: BovineThread<any>, addr: number, type: JavaType, value: any) {
    const module = thread.vm._module;
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

function makeArrayName(s: string, dims: number): string {
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
            { kind: 'L', className: makePrimitiveArrayName(desc[i]!, dims) } :
            { kind: desc[i]!, className: "" } as JavaType);
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

function ptr(handle: BaseHandle) {
    if (handle.handleIndex === -1) {
        throw new Error("Attempting to de-reference dropped handle");
    }
    return handle.vm._module._deref_js_handle(handle.vm.ptr, handle.handleIndex);
}

// Handle to a Java object
class BaseHandle {
    // @ts-ignore
    vm: BovineVM<any>;  // constructed manually, and we don't want a constructor for the class
    // @ts-ignore
    handleIndex: number;

    drop() {
        if (this.handleIndex === -1) {  // already dropped
            return;
        }
        this.vm.handleRegistry.unregister(this.vm.unregistrationTokens[this.handleIndex]!);
        this.vm._module._drop_js_handle(this.vm.ptr, this.handleIndex);
        this.handleIndex = -1;
    }
}

function binaryNameToJSName(name: string) {
    name = name.replaceAll('/', "_");
    // Remove potentially dangerous characters (only allow a-zA-Z0-9)
    if (!name.match(/^[a-zA-Z0-9_]$/)) {
        name = "";
    }
    return name;
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
        let group = this.grouped.get(method.name)!;
        if (!group.has(method.parameterNames.length)) {
            group.set(method.parameterNames.length, []);
        }
        group.get(method.parameterNames.length)!.push(method);
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
                    let method = methods[i]!;
                    let desc = parseMethodDescriptor(method);
                    let newName = `${name}_$${desc.parameterTypes.map(stringifyParameter).join('$')}`;

                    this.grouped.set(newName, new Map([[argc, [method]]]));
                }

                group.delete(argc);
            }
        }
    }

    createImpls(static_: boolean): string {
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
                return `case ${argc}: { i = ${m!.index}; break; }`;
            }).join('\n')}
                    default:
                    throw new RangeError("Invalid number of arguments (expected one of ${[...group.keys()].join(', ')}, got " + arguments.length + ")");
                }
                const thread = vm.getActiveThread();
                return thread.${runner}(methods[i], ${maybeThis} ...arguments);
            }`;
            impls.push(impl);
        }

        return impls.join('\n');
    }
}

interface HandleConstructor {
    new (): typeof BaseHandle;
}

function createClassImpl<Classes>(vm: BovineVM<Classes>, bjvm_classdesc_ptr: number): HandleConstructor {
    const module = vm._module;
    const classInfoStr = module._ffi_get_class_json(bjvm_classdesc_ptr);
    const info = module.UTF8ToString(classInfoStr);
    const classInfo: ClassInfo = JSON.parse(info);
    module._free(classInfoStr);

    const instanceResolver = new OverloadResolver();
    const staticResolver = new OverloadResolver();

    // Static methods
    for (let i = 0 ; i < classInfo.methods.length; ++i) {
        let method = classInfo.methods[i]!;
        if (!(method.accessFlags & 0x0001)) {
            continue;
        }
        if (method.accessFlags & 0x0008) {
            staticResolver.addMethod(method);
        } else {
            instanceResolver.addMethod(method);
        }
    }

    let arrayMethods = `get(index) { return vm.createThread().readArray(this, index); } get length() { return vm.createThread().getArrayLength(this); }`;

    const cow = binaryNameToJSName(classInfo.binaryName);
    const body = `return class ${cow} extends BaseHandle {
    ${instanceResolver.createImpls(false)}
    ${staticResolver.createImpls(true)}
    ${arrayMethods}
    };`;

    const Class = new Function("name", "BaseHandle", "vm", "methods", body)(classInfo.binaryName, BaseHandle, vm, classInfo.methods);
    Object.defineProperty(Class, 'name', { value: classInfo.binaryName });

    return Class;
}

export class BovineVM<Classes> {
    _module: MainModule;
    options: VMOptions;
    unregistrationTokens: Array<{}> = [];

    readonly ptr: number;
    private os: BovineOS;
    private activeThread: BovineThread<Classes>;
    private primordialThread: BovineThread<Classes>;

    private stdout: (bytes: Uint8Array) => void | null;
    private stderr: (bytes: Uint8Array) => void | null;

    private boundOnStdout: any;
    private boundOnStderr: any;

    handleRegistry: FinalizationRegistry<number> = new FinalizationRegistry((index: number) => {
        console.log("Dropping handle", index);
        this._module._drop_js_handle(this.ptr, index);
    });
    private namedClasses: Map<number /* bjvm_classdesc* */, any> = new Map();

    constructor(os: BovineOS, options: VMOptions) {
        this.os = os;
        this._module = os._module;
        this.options = options;

        let classpath = this._module._malloc(options.classpath.length + 1);
        maybeOutOfMemory(classpath);

        new TextEncoder().encodeInto(options.classpath,
            new Uint8Array(this._module.HEAPU8.buffer, classpath, options.classpath.length));
        this._module.HEAPU8[classpath + options.classpath.length] = 0;  // zero-terminate

        this.boundOnStderr = this.onStderr.bind(this);
        this.boundOnStdout = this.onStdout.bind(this);

        this.ptr = this._module._ffi_create_vm(classpath, options.heapSize ?? 1 << 26,
            this._module.addFunction(this.boundOnStderr, 'viii'), this._module.addFunction(this.boundOnStdout, 'viii'));
        this._module._free(classpath);
        if (this.ptr === 0) {
            throw new Error("Failed to create VM");
        }

        this.primordialThread = this.createThread();
        this.activeThread = this.primordialThread;

        this.stderr = os.options.stderr ?? buffered(console.error);
        this.stdout = os.options.stdout ?? buffered(console.log);

        this.scheduler = this._module._ffi_create_rr_scheduler(this.ptr);
    }

    private onStdout(bufPointer: number, len: number) {
        try {
            this.stdout?.(new Uint8Array(this.os._module.HEAPU8.buffer, bufPointer, len));
        } catch (e) { /* suppress error as it will break the VM to unwind */ }
    }

    private onStderr(bufPointer: number, len: number) {
        try {
            this.stderr?.(new Uint8Array(this.os._module.HEAPU8.buffer, bufPointer, len));
        } catch (e) {

        }
    }

    createThread(): BovineThread<Classes> {
        let threadPtr = this._module._ffi_create_thread(this.ptr);
        const thread = new BovineThread(this, threadPtr);
        return thread;
    }

    getActiveThread() {
        return this.activeThread;
    }

    /**
     * Set the active thread to which all methods will be scheduled. If null, the first thread created will be the
     * active thread.
     * @param thread
     */
    setActiveThread(thread: BovineThread<Classes> | null) {
        if (thread === null) {
            this.activeThread = this.primordialThread;
        } else if (thread.vm !== this) {
            throw new Error("Thread is not associated with this VM");
        } else {
            this.activeThread = thread;
        }
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
        const module = this._module;

        let isString = module._ffi_is_string(ptr);
        if (isString) {  // Convert to a JS string
            const chars = module._ffi_get_string_data(ptr);
            const length = module._ffi_get_string_len(ptr);
            const coder = module._ffi_get_string_coder(ptr);
            enum Coder { STRING_CODER_LATIN1 = 0, STRING_CODER_UTF16 = 1 }
            if (coder === Coder.STRING_CODER_LATIN1) {
                return new TextDecoder('latin1').decode(new Uint8Array(module.HEAPU8.buffer, chars, length));
            } else {
                return new TextDecoder('utf-16').decode(new Uint8Array(module.HEAPU8.buffer, chars, length));
            }
        }

        let handleIndex = module._make_js_handle(this.ptr, ptr);
        let classdesc = module._ffi_get_classdesc(ptr);

        const clazz = this.getClassForDescriptor(classdesc);
        const handle = Object.create(clazz.prototype);  // do this to avoid calling the constructor
        handle.vm = this;
        handle.handleIndex = handleIndex;

        this.handleRegistry.register(handle, handleIndex, this.unregistrationTokens[handleIndex] ??= {});

        return handle;
    }

    throwThreadException() {
        let ptr = this._module._ffi_get_current_exception(this.activeThread.ptr);
        if (!ptr) {
            return;
        }
        const handle = this.createHandle(ptr);
        this._module._ffi_clear_current_exception(this.activeThread.ptr);
        throw handle;
    }

    loadClass<C extends ValidClassName<Classes>>(name: C): LoadClass<C> {
        let namePtr = this._module._malloc(name.length + 1);
        new TextEncoder().encodeInto(name, new Uint8Array(this._module.HEAPU8.buffer, namePtr, name.length));
        this._module.HEAPU8[namePtr + name.length] = 0;
        let ptr = this._module._ffi_get_class(this.activeThread.ptr, namePtr);
        if (!ptr) {
            this.throwThreadException();
        }
        const clazz = this.getClassForDescriptor(ptr);
        this._module._free(namePtr);
        return clazz;
    }

    scheduler: number = 0;
    timeout: ReturnType<typeof setTimeout> = -1 as any; // if not -1, then a scheduler step is scheduled

    waitingForYield: number = 0;
    pending: Function[] = [];  // hook here to be called every time the timeout fires

    scheduleTimeout(waitUs: number = 0) {
        const module = this._module;
        if (this.waitingForYield > waitUs && this.timeout !== -1 as unknown as ReturnType<typeof setTimeout>) {
            clearTimeout(this.timeout);
            this.timeout = -1 as unknown as ReturnType<typeof setTimeout>;
        }
        if (this.timeout === -1 as unknown as ReturnType<typeof setTimeout>) {
            this.timeout = setTimeout(() => {
                this.timeout = -1 as unknown as ReturnType<typeof setTimeout>;
                const status = module._ffi_rr_scheduler_step(this.scheduler);
                if (status !== 0) {
                    const waitUs = module._ffi_rr_scheduler_wait_for_us(this.scheduler);
                    this.scheduleTimeout(this.waitingForYield = waitUs);
                }
                for (let i = 0; i < this.pending.length; i++) {
                    this.pending[i]!();
                }
                this.pending.length = 0;
            }, waitUs / 1000);
        }
    }

    // Low-level method scheduling apparatus
    scheduleMethod(thread: BovineThread<Classes>, method: MethodInfo, argsPtr: number):
        { record: number, waitForResolution: Promise<boolean>, cancelResolution: () => void } {
        const module = this._module;
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

    dispose() {

    }
}

const runtimeFilesList = `jdk23/lib/modules
jdk23/lib/security/default.policy
jdk23/conf/security/java.security
jdk23/conf/security/java.policy
jdk23.jar`.split('\n');

const TOTAL_BYTES = 1 << 26;

/**
 * Create a Bovine operating system, which may be inhabited by multiple independent Bovine VMs.
 * @param options Creation options.
 */
export async function makeBovineOS(options: BovineOSOptions): Promise<BovineOS> {
    const wasmUrl = options.wasmLocation;
    const runtimeUrl = options.runtimeUrl;
    const runtimeFiles = options.additionalRuntimeFiles;

    const modulePromise = MainModuleFactory({
        locateFile: (path: string) => {
            if (path.endsWith(".wasm") && wasmUrl) {
                return wasmUrl;
            }
            return path;
        }
    });

    let totalLoaded = 0;
    const filesNeeded = [...runtimeFilesList, ...runtimeFiles];
    // Spawn fetch requests
    const requests = filesNeeded.map(async (file) => {
        const response = await fetch(`${runtimeUrl}/${file}`, options.fetchParams ?? {
            method: 'GET',
            headers: {
                'Content-Type': 'application/octet-stream',
            }
        });

        const contentLength = response.headers.get('Content-Length');
        const total = contentLength ? parseInt(contentLength) : 0;

        const reader = response.body?.getReader()!;
        let loaded = 0;
        const data = new Uint8Array(total);
        while (true) {
            const {done, value} = await reader.read();
            if (done)
                break;
            try {
              data.set(value, loaded);
            } catch (e) {
              console.log(`failed to load ${file}`)
            }
            loaded += value.length;
            totalLoaded += value.length;
            options.progress?.(totalLoaded, TOTAL_BYTES);
        }
        // Insert into the DB
        // await addFile(db, file, data);
        return {file, data};
    });

    // Wait for all requests to finish
    const results = await Promise.all(requests);
    const module = await modulePromise;

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

    return new BovineOS(options, module);
}