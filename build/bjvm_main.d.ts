// TypeScript bindings for emscripten-generated code.  Automatically generated at compile time.
declare namespace RuntimeExports {
    /**
     * @param {string|null=} returnType
     * @param {Array=} argTypes
     * @param {Arguments|Array=} args
     * @param {Object=} opts
     */
    function ccall(ident: any, returnType?: (string | null) | undefined, argTypes?: any[] | undefined, args?: (Arguments | any[]) | undefined, opts?: any | undefined): any;
    let wasmMemory: any;
    /**
     * @param {string=} returnType
     * @param {Array=} argTypes
     * @param {Object=} opts
     */
    function cwrap(ident: any, returnType?: string | undefined, argTypes?: any[] | undefined, opts?: any | undefined): any;
    /** @param {string=} sig */
    function addFunction(func: any, sig?: string | undefined): any;
    function removeFunction(index: any): void;
    let wasmTable: WebAssembly.Table;
    namespace FS {
        function init(): void;
        let ErrnoError: any;
        function handleError(returnValue: any): any;
        function ensureErrnoError(): void;
        function createDataFile(parent: any, name: any, fileData: any, canRead: any, canWrite: any, canOwn: any): void;
        function createPath(parent: any, path: any, canRead: any, canWrite: any): any;
        function createPreloadedFile(parent: any, name: any, url: any, canRead: any, canWrite: any, onload: any, onerror: any, dontCreateFile: any, canOwn: any, preFinish: any): any;
        function readFile(path: any, opts?: {}): Uint8Array;
        function cwd(): any;
        function analyzePath(path: any): {
            exists: boolean;
            object: {
                contents: any;
            };
        };
        function mkdir(path: any, mode: any): any;
        function mkdirTree(path: any, mode: any): any;
        function rmdir(path: any): any;
        function open(path: any, flags: any, mode?: number): any;
        function create(path: any, mode: any): any;
        function close(stream: any): any;
        function unlink(path: any): any;
        function chdir(path: any): any;
        function read(stream: any, buffer: any, offset: any, length: any, position: any): any;
        function write(stream: any, buffer: any, offset: any, length: any, position: any, canOwn: any): any;
        function allocate(stream: any, offset: any, length: any): any;
        function writeFile(path: any, data: any): any;
        function mmap(stream: any, length: any, offset: any, prot: any, flags: any): {
            ptr: any;
            allocated: boolean;
        };
        function msync(stream: any, bufferPtr: any, offset: any, length: any, mmapFlags: any): any;
        function munmap(addr: any, length: any): any;
        function symlink(target: any, linkpath: any): any;
        function readlink(path: any): any;
        function statBufToObject(statBuf: any): {
            dev: any;
            mode: any;
            nlink: any;
            uid: any;
            gid: any;
            rdev: any;
            size: any;
            blksize: any;
            blocks: any;
            atime: any;
            mtime: any;
            ctime: any;
            ino: any;
        };
        function stat(path: any): any;
        function lstat(path: any): any;
        function chmod(path: any, mode: any): any;
        function lchmod(path: any, mode: any): any;
        function fchmod(fd: any, mode: any): any;
        function utime(path: any, atime: any, mtime: any): any;
        function truncate(path: any, len: any): any;
        function ftruncate(fd: any, len: any): any;
        function findObject(path: any): {
            isFolder: boolean;
            isDevice: boolean;
        };
        function readdir(path: any): any;
        function mount(type: any, opts: any, mountpoint: any): any;
        function unmount(mountpoint: any): any;
        function mknod(path: any, mode: any, dev: any): any;
        function makedev(ma: any, mi: any): number;
        function registerDevice(dev: any, ops: any): void;
        function createDevice(parent: any, name: any, input: any, output: any): any;
        function mkdev(path: any, mode: any, dev: any): any;
        function rename(oldPath: any, newPath: any): any;
        function llseek(stream: any, offset: any, whence: any): any;
    }
    /**
     * Given a pointer 'ptr' to a null-terminated UTF8-encoded string in the
     * emscripten HEAP, returns a copy of that string as a Javascript String object.
     *
     * @param {number} ptr
     * @param {number=} maxBytesToRead - An optional length that specifies the
     *   maximum number of bytes to read. You can omit this parameter to scan the
     *   string until the first 0 byte. If maxBytesToRead is passed, and the string
     *   at [ptr, ptr+maxBytesToReadr[ contains a null byte in the middle, then the
     *   string will cut short at that byte index (i.e. maxBytesToRead will not
     *   produce a string of exact length [ptr, ptr+maxBytesToRead[) N.B. mixing
     *   frequent uses of UTF8ToString() with and without maxBytesToRead may throw
     *   JS JIT optimizations off, so it is worth to consider consistently using one
     * @return {string}
     */
    function UTF8ToString(ptr: number, maxBytesToRead?: number | undefined): string;
    /**
     * @param {number} ptr
     * @param {string} type
     */
    function getValue(ptr: number, type?: string): any;
    /**
     * @param {number} ptr
     * @param {number} value
     * @param {string} type
     */
    function setValue(ptr: number, value: number, type?: string): void;
    let HEAPF32: any;
    let HEAPF64: any;
    let HEAP_DATA_VIEW: any;
    let HEAP8: any;
    let HEAPU8: any;
    let HEAP16: any;
    let HEAPU16: any;
    let HEAP32: any;
    let HEAPU32: any;
    let HEAP64: any;
    let HEAPU64: any;
    let FS_createPath: any;
    function FS_createDataFile(parent: any, name: any, fileData: any, canRead: any, canWrite: any, canOwn: any): void;
    function FS_createPreloadedFile(parent: any, name: any, url: any, canRead: any, canWrite: any, onload: any, onerror: any, dontCreateFile: any, canOwn: any, preFinish: any): void;
    function FS_unlink(path: any): any;
    let addRunDependency: any;
    let removeRunDependency: any;
}
interface WasmModule {
  _bjvm_ffi_create_vm(_0: number, _1: number, _2: number): number;
  _bjvm_ffi_create_thread(_0: number): number;
  _bjvm_ffi_get_class(_0: number, _1: number): number;
  _bjvm_ffi_get_current_exception(_0: number): number;
  _bjvm_ffi_clear_current_exception(_0: number): void;
  _bjvm_ffi_get_classdesc(_0: number): number;
  _bjvm_ffi_create_rr_scheduler(_0: number): number;
  _malloc(_0: number): number;
  _bjvm_ffi_rr_scheduler_wait_for_us(_0: number): number;
  _bjvm_ffi_rr_scheduler_step(_0: number): number;
  _bjvm_ffi_rr_record_is_ready(_0: number): number;
  _bjvm_ffi_rr_schedule(_0: number, _1: number, _2: number): number;
  _bjvm_ffi_get_execution_record_result_pointer(_0: number): number;
  _bjvm_deref_js_handle(_0: number, _1: number): number;
  _bjvm_ffi_execute_immediately(_0: number): number;
  _bjvm_ffi_free_execution_record(_0: number): void;
  _bjvm_ffi_async_run(_0: number, _1: number, _2: number): number;
  _bjvm_ffi_allocate_object(_0: number, _1: number): number;
  _bjvm_ffi_run_step(_0: number, _1: number): number;
  _bjvm_ffi_free_async_run_ctx(_0: number): void;
  _free(_0: number): void;
  _bjvm_ffi_get_class_json(_0: number): number;
  _main(_0: number, _1: number): number;
  _bjvm_make_js_handle(_0: number, _1: number): number;
  _bjvm_drop_js_handle(_0: number, _1: number): void;
  _set_max_calls(_0: number): number;
  ___interpreter_intrinsic_void_table_base(): number;
  ___interpreter_intrinsic_int_table_base(): number;
  ___interpreter_intrinsic_float_table_base(): number;
  ___interpreter_intrinsic_double_table_base(): number;
  ___interpreter_intrinsic_max_insn(): number;
  _bjvm_wasm_push_export(_0: number, _1: number, _2: number): void;
}

export type MainModule = WasmModule & typeof RuntimeExports;
export default function MainModuleFactory (options?: unknown): Promise<MainModule>;
