var Module = (() => {
  var _scriptName = import.meta.url;
  
  return (
async function(moduleArg = {}) {
  var moduleRtn;

// include: shell.js
// The Module object: Our interface to the outside world. We import
// and export values on it. There are various ways Module can be used:
// 1. Not defined. We create it here
// 2. A function parameter, function(moduleArg) => Promise<Module>
// 3. pre-run appended it, var Module = {}; ..generated code..
// 4. External script tag defines var Module.
// We need to check if Module already exists (e.g. case 3 above).
// Substitution will be replaced with actual code on later stage of the build,
// this way Closure Compiler will not mangle it (e.g. case 4. above).
// Note that if you want to run closure, and also to use Module
// after the generated code, you will need to define   var Module = {};
// before the code. Then that object will be used in the code, and you
// can continue to use Module afterwards as well.
var Module = moduleArg;

// Set up the promise that indicates the Module is initialized
var readyPromiseResolve, readyPromiseReject;
var readyPromise = new Promise((resolve, reject) => {
  readyPromiseResolve = resolve;
  readyPromiseReject = reject;
});

// Determine the runtime environment we are in. You can customize this by
// setting the ENVIRONMENT setting at compile time (see settings.js).

var ENVIRONMENT_IS_WEB = true;
var ENVIRONMENT_IS_WORKER = false;
var ENVIRONMENT_IS_NODE = false;
var ENVIRONMENT_IS_SHELL = false;

// --pre-jses are emitted after the Module integration code, so that they can
// refer to Module (if they choose; they can also define Module)


// Sometimes an existing Module object exists with properties
// meant to overwrite the default module functionality. Here
// we collect those properties and reapply _after_ we configure
// the current environment's defaults to avoid having to be so
// defensive during initialization.
var moduleOverrides = Object.assign({}, Module);

var arguments_ = [];
var thisProgram = './this.program';
var quit_ = (status, toThrow) => {
  throw toThrow;
};

// `/` should be present at the end if `scriptDirectory` is not empty
var scriptDirectory = '';
function locateFile(path) {
  if (Module['locateFile']) {
    return Module['locateFile'](path, scriptDirectory);
  }
  return scriptDirectory + path;
}

// Hooks that are implemented differently in different runtime environments.
var readAsync, readBinary;

if (ENVIRONMENT_IS_SHELL) {

  if ((typeof process == 'object' && typeof require === 'function') || typeof window == 'object' || typeof WorkerGlobalScope != 'undefined') throw new Error('not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)');

} else

// Note that this includes Node.js workers when relevant (pthreads is enabled).
// Node.js workers are detected as a combination of ENVIRONMENT_IS_WORKER and
// ENVIRONMENT_IS_NODE.
if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
  if (ENVIRONMENT_IS_WORKER) { // Check worker, not web, since window could be polyfilled
    scriptDirectory = self.location.href;
  } else if (typeof document != 'undefined' && document.currentScript) { // web
    scriptDirectory = document.currentScript.src;
  }
  // When MODULARIZE, this JS may be executed later, after document.currentScript
  // is gone, so we saved it, and we use it here instead of any other info.
  if (_scriptName) {
    scriptDirectory = _scriptName;
  }
  // blob urls look like blob:http://site.com/etc/etc and we cannot infer anything from them.
  // otherwise, slice off the final part of the url to find the script directory.
  // if scriptDirectory does not contain a slash, lastIndexOf will return -1,
  // and scriptDirectory will correctly be replaced with an empty string.
  // If scriptDirectory contains a query (starting with ?) or a fragment (starting with #),
  // they are removed because they could contain a slash.
  if (scriptDirectory.startsWith('blob:')) {
    scriptDirectory = '';
  } else {
    scriptDirectory = scriptDirectory.substr(0, scriptDirectory.replace(/[?#].*/, '').lastIndexOf('/')+1);
  }

  if (!(typeof window == 'object' || typeof WorkerGlobalScope != 'undefined')) throw new Error('not compiled for this environment (did you build to HTML and try to run it not on the web, or set ENVIRONMENT to something - like node - and run it someplace else - like on the web?)');

  {
// include: web_or_worker_shell_read.js
readAsync = async (url) => {
    assert(!isFileURI(url), "readAsync does not work with file:// URLs");
    var response = await fetch(url, { credentials: 'same-origin' });
    if (response.ok) {
      return response.arrayBuffer();
    }
    throw new Error(response.status + ' : ' + response.url);
  };
// end include: web_or_worker_shell_read.js
  }
} else
{
  throw new Error('environment detection error');
}

var out = Module['print'] || console.log.bind(console);
var err = Module['printErr'] || console.error.bind(console);

// Merge back in the overrides
Object.assign(Module, moduleOverrides);
// Free the object hierarchy contained in the overrides, this lets the GC
// reclaim data used.
moduleOverrides = null;
checkIncomingModuleAPI();

// Emit code to handle expected values on the Module object. This applies Module.x
// to the proper local x. This has two benefits: first, we only emit it if it is
// expected to arrive, and second, by using a local everywhere else that can be
// minified.

if (Module['arguments']) arguments_ = Module['arguments'];legacyModuleProp('arguments', 'arguments_');

if (Module['thisProgram']) thisProgram = Module['thisProgram'];legacyModuleProp('thisProgram', 'thisProgram');

// perform assertions in shell.js after we set up out() and err(), as otherwise if an assertion fails it cannot print the message
// Assertions on removed incoming Module JS APIs.
assert(typeof Module['memoryInitializerPrefixURL'] == 'undefined', 'Module.memoryInitializerPrefixURL option was removed, use Module.locateFile instead');
assert(typeof Module['pthreadMainPrefixURL'] == 'undefined', 'Module.pthreadMainPrefixURL option was removed, use Module.locateFile instead');
assert(typeof Module['cdInitializerPrefixURL'] == 'undefined', 'Module.cdInitializerPrefixURL option was removed, use Module.locateFile instead');
assert(typeof Module['filePackagePrefixURL'] == 'undefined', 'Module.filePackagePrefixURL option was removed, use Module.locateFile instead');
assert(typeof Module['read'] == 'undefined', 'Module.read option was removed');
assert(typeof Module['readAsync'] == 'undefined', 'Module.readAsync option was removed (modify readAsync in JS)');
assert(typeof Module['readBinary'] == 'undefined', 'Module.readBinary option was removed (modify readBinary in JS)');
assert(typeof Module['setWindowTitle'] == 'undefined', 'Module.setWindowTitle option was removed (modify emscripten_set_window_title in JS)');
assert(typeof Module['TOTAL_MEMORY'] == 'undefined', 'Module.TOTAL_MEMORY has been renamed Module.INITIAL_MEMORY');
legacyModuleProp('asm', 'wasmExports');
legacyModuleProp('readAsync', 'readAsync');
legacyModuleProp('readBinary', 'readBinary');
legacyModuleProp('setWindowTitle', 'setWindowTitle');
var IDBFS = 'IDBFS is no longer included by default; build with -lidbfs.js';
var PROXYFS = 'PROXYFS is no longer included by default; build with -lproxyfs.js';
var WORKERFS = 'WORKERFS is no longer included by default; build with -lworkerfs.js';
var FETCHFS = 'FETCHFS is no longer included by default; build with -lfetchfs.js';
var ICASEFS = 'ICASEFS is no longer included by default; build with -licasefs.js';
var JSFILEFS = 'JSFILEFS is no longer included by default; build with -ljsfilefs.js';
var OPFS = 'OPFS is no longer included by default; build with -lopfs.js';

var NODEFS = 'NODEFS is no longer included by default; build with -lnodefs.js';

assert(!ENVIRONMENT_IS_WORKER, 'worker environment detected but not enabled at build time.  Add `worker` to `-sENVIRONMENT` to enable.');

assert(!ENVIRONMENT_IS_NODE, 'node environment detected but not enabled at build time.  Add `node` to `-sENVIRONMENT` to enable.');

assert(!ENVIRONMENT_IS_SHELL, 'shell environment detected but not enabled at build time.  Add `shell` to `-sENVIRONMENT` to enable.');

// end include: shell.js

// include: preamble.js
// === Preamble library stuff ===

// Documentation for the public APIs defined in this file must be updated in:
//    site/source/docs/api_reference/preamble.js.rst
// A prebuilt local version of the documentation is available at:
//    site/build/text/docs/api_reference/preamble.js.txt
// You can also build docs locally as HTML or other formats in site/
// An online HTML version (which may be of a different version of Emscripten)
//    is up at http://kripken.github.io/emscripten-site/docs/api_reference/preamble.js.html

var wasmBinary = Module['wasmBinary'];legacyModuleProp('wasmBinary', 'wasmBinary');

if (typeof WebAssembly != 'object') {
  err('no native wasm support detected');
}

// Wasm globals

var wasmMemory;

//========================================
// Runtime essentials
//========================================

// whether we are quitting the application. no code should run after this.
// set in exit() and abort()
var ABORT = false;

// set by exit() and abort().  Passed to 'onExit' handler.
// NOTE: This is also used as the process return code code in shell environments
// but only when noExitRuntime is false.
var EXITSTATUS;

// In STRICT mode, we only define assert() when ASSERTIONS is set.  i.e. we
// don't define it at all in release modes.  This matches the behaviour of
// MINIMAL_RUNTIME.
// TODO(sbc): Make this the default even without STRICT enabled.
/** @type {function(*, string=)} */
function assert(condition, text) {
  if (!condition) {
    abort('Assertion failed' + (text ? ': ' + text : ''));
  }
}

// We used to include malloc/free by default in the past. Show a helpful error in
// builds with assertions.

// Memory management

var HEAP,
/** @type {!Int8Array} */
  HEAP8,
/** @type {!Uint8Array} */
  HEAPU8,
/** @type {!Int16Array} */
  HEAP16,
/** @type {!Uint16Array} */
  HEAPU16,
/** @type {!Int32Array} */
  HEAP32,
/** @type {!Uint32Array} */
  HEAPU32,
/** @type {!Float32Array} */
  HEAPF32,
/* BigInt64Array type is not correctly defined in closure
/** not-@type {!BigInt64Array} */
  HEAP64,
/* BigUint64Array type is not correctly defined in closure
/** not-t@type {!BigUint64Array} */
  HEAPU64,
/** @type {!Float64Array} */
  HEAPF64;

var runtimeInitialized = false;

// include: URIUtils.js
// Prefix of data URIs emitted by SINGLE_FILE and related options.
var dataURIPrefix = 'data:application/octet-stream;base64,';

/**
 * Indicates whether filename is a base64 data URI.
 * @noinline
 */
var isDataURI = (filename) => filename.startsWith(dataURIPrefix);

/**
 * Indicates whether filename is delivered via file protocol (as opposed to http/https)
 * @noinline
 */
var isFileURI = (filename) => filename.startsWith('file://');
// end include: URIUtils.js
// include: runtime_shared.js
// include: runtime_stack_check.js
// Initializes the stack cookie. Called at the startup of main and at the startup of each thread in pthreads mode.
function writeStackCookie() {
  var max = _emscripten_stack_get_end();
  assert((max & 3) == 0);
  // If the stack ends at address zero we write our cookies 4 bytes into the
  // stack.  This prevents interference with SAFE_HEAP and ASAN which also
  // monitor writes to address zero.
  if (max == 0) {
    max += 4;
  }
  // The stack grow downwards towards _emscripten_stack_get_end.
  // We write cookies to the final two words in the stack and detect if they are
  // ever overwritten.
  HEAPU32[((max)>>2)] = 0x02135467;
  HEAPU32[(((max)+(4))>>2)] = 0x89BACDFE;
  // Also test the global address 0 for integrity.
  HEAPU32[((0)>>2)] = 1668509029;
}

function checkStackCookie() {
  if (ABORT) return;
  var max = _emscripten_stack_get_end();
  // See writeStackCookie().
  if (max == 0) {
    max += 4;
  }
  var cookie1 = HEAPU32[((max)>>2)];
  var cookie2 = HEAPU32[(((max)+(4))>>2)];
  if (cookie1 != 0x02135467 || cookie2 != 0x89BACDFE) {
    abort(`Stack overflow! Stack cookie has been overwritten at ${ptrToString(max)}, expected hex dwords 0x89BACDFE and 0x2135467, but received ${ptrToString(cookie2)} ${ptrToString(cookie1)}`);
  }
  // Also test the global address 0 for integrity.
  if (HEAPU32[((0)>>2)] != 0x63736d65 /* 'emsc' */) {
    abort('Runtime error: The application has corrupted its heap memory area (address zero)!');
  }
}
// end include: runtime_stack_check.js
// include: runtime_exceptions.js
// end include: runtime_exceptions.js
// include: runtime_debug.js
// Endianness check
(() => {
  var h16 = new Int16Array(1);
  var h8 = new Int8Array(h16.buffer);
  h16[0] = 0x6373;
  if (h8[0] !== 0x73 || h8[1] !== 0x63) throw 'Runtime error: expected the system to be little-endian! (Run with -sSUPPORT_BIG_ENDIAN to bypass)';
})();

if (Module['ENVIRONMENT']) {
  throw new Error('Module.ENVIRONMENT has been deprecated. To force the environment, use the ENVIRONMENT compile-time option (for example, -sENVIRONMENT=web or -sENVIRONMENT=node)');
}

function legacyModuleProp(prop, newName, incoming=true) {
  if (!Object.getOwnPropertyDescriptor(Module, prop)) {
    Object.defineProperty(Module, prop, {
      configurable: true,
      get() {
        let extra = incoming ? ' (the initial value can be provided on Module, but after startup the value is only looked for on a local variable of that name)' : '';
        abort(`\`Module.${prop}\` has been replaced by \`${newName}\`` + extra);

      }
    });
  }
}

function ignoredModuleProp(prop) {
  if (Object.getOwnPropertyDescriptor(Module, prop)) {
    abort(`\`Module.${prop}\` was supplied but \`${prop}\` not included in INCOMING_MODULE_JS_API`);
  }
}

// forcing the filesystem exports a few things by default
function isExportedByForceFilesystem(name) {
  return name === 'FS_createPath' ||
         name === 'FS_createDataFile' ||
         name === 'FS_createPreloadedFile' ||
         name === 'FS_unlink' ||
         name === 'addRunDependency' ||
         name === 'removeRunDependency';
}

/**
 * Intercept access to a global symbol.  This enables us to give informative
 * warnings/errors when folks attempt to use symbols they did not include in
 * their build, or no symbols that no longer exist.
 */
function hookGlobalSymbolAccess(sym, func) {
  if (typeof globalThis != 'undefined' && !Object.getOwnPropertyDescriptor(globalThis, sym)) {
    Object.defineProperty(globalThis, sym, {
      configurable: true,
      get() {
        func();
        return undefined;
      }
    });
  }
}

function missingGlobal(sym, msg) {
  hookGlobalSymbolAccess(sym, () => {
    warnOnce(`\`${sym}\` is not longer defined by emscripten. ${msg}`);
  });
}

missingGlobal('buffer', 'Please use HEAP8.buffer or wasmMemory.buffer');
missingGlobal('asm', 'Please use wasmExports instead');

function missingLibrarySymbol(sym) {
  hookGlobalSymbolAccess(sym, () => {
    // Can't `abort()` here because it would break code that does runtime
    // checks.  e.g. `if (typeof SDL === 'undefined')`.
    var msg = `\`${sym}\` is a library symbol and not included by default; add it to your library.js __deps or to DEFAULT_LIBRARY_FUNCS_TO_INCLUDE on the command line`;
    // DEFAULT_LIBRARY_FUNCS_TO_INCLUDE requires the name as it appears in
    // library.js, which means $name for a JS name with no prefix, or name
    // for a JS name like _name.
    var librarySymbol = sym;
    if (!librarySymbol.startsWith('_')) {
      librarySymbol = '$' + sym;
    }
    msg += ` (e.g. -sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE='${librarySymbol}')`;
    if (isExportedByForceFilesystem(sym)) {
      msg += '. Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you';
    }
    warnOnce(msg);
  });

  // Any symbol that is not included from the JS library is also (by definition)
  // not exported on the Module object.
  unexportedRuntimeSymbol(sym);
}

function unexportedRuntimeSymbol(sym) {
  if (!Object.getOwnPropertyDescriptor(Module, sym)) {
    Object.defineProperty(Module, sym, {
      configurable: true,
      get() {
        var msg = `'${sym}' was not exported. add it to EXPORTED_RUNTIME_METHODS (see the Emscripten FAQ)`;
        if (isExportedByForceFilesystem(sym)) {
          msg += '. Alternatively, forcing filesystem support (-sFORCE_FILESYSTEM) can export this for you';
        }
        abort(msg);
      }
    });
  }
}

// Used by XXXXX_DEBUG settings to output debug messages.
function dbg(...args) {
  // TODO(sbc): Make this configurable somehow.  Its not always convenient for
  // logging to show up as warnings.
  console.warn(...args);
}
// end include: runtime_debug.js
// include: memoryprofiler.js
// end include: memoryprofiler.js


function updateMemoryViews() {
  var b = wasmMemory.buffer;
  Module['HEAP8'] = HEAP8 = new Int8Array(b);
  Module['HEAP16'] = HEAP16 = new Int16Array(b);
  Module['HEAPU8'] = HEAPU8 = new Uint8Array(b);
  Module['HEAPU16'] = HEAPU16 = new Uint16Array(b);
  Module['HEAP32'] = HEAP32 = new Int32Array(b);
  Module['HEAPU32'] = HEAPU32 = new Uint32Array(b);
  Module['HEAPF32'] = HEAPF32 = new Float32Array(b);
  Module['HEAPF64'] = HEAPF64 = new Float64Array(b);
  Module['HEAP64'] = HEAP64 = new BigInt64Array(b);
  Module['HEAPU64'] = HEAPU64 = new BigUint64Array(b);
}

// end include: runtime_shared.js
assert(!Module['STACK_SIZE'], 'STACK_SIZE can no longer be set at runtime.  Use -sSTACK_SIZE at link time')

assert(typeof Int32Array != 'undefined' && typeof Float64Array !== 'undefined' && Int32Array.prototype.subarray != undefined && Int32Array.prototype.set != undefined,
       'JS engine does not provide full typed array support');

// If memory is defined in wasm, the user can't provide it, or set INITIAL_MEMORY
assert(!Module['wasmMemory'], 'Use of `wasmMemory` detected.  Use -sIMPORTED_MEMORY to define wasmMemory externally');
assert(!Module['INITIAL_MEMORY'], 'Detected runtime INITIAL_MEMORY setting.  Use -sIMPORTED_MEMORY to define wasmMemory dynamically');

var __ATPRERUN__  = []; // functions called before the runtime is initialized
var __ATINIT__    = []; // functions called during startup
var __ATMAIN__    = []; // functions called when main() is to be run
var __ATEXIT__    = []; // functions called during shutdown
var __ATPOSTRUN__ = []; // functions called after the main() is called

function preRun() {
  if (Module['preRun']) {
    if (typeof Module['preRun'] == 'function') Module['preRun'] = [Module['preRun']];
    while (Module['preRun'].length) {
      addOnPreRun(Module['preRun'].shift());
    }
  }
  callRuntimeCallbacks(__ATPRERUN__);
}

function initRuntime() {
  assert(!runtimeInitialized);
  runtimeInitialized = true;

  checkStackCookie();

  
  callRuntimeCallbacks(__ATINIT__);
}

function preMain() {
  checkStackCookie();
  
  callRuntimeCallbacks(__ATMAIN__);
}

function postRun() {
  checkStackCookie();

  if (Module['postRun']) {
    if (typeof Module['postRun'] == 'function') Module['postRun'] = [Module['postRun']];
    while (Module['postRun'].length) {
      addOnPostRun(Module['postRun'].shift());
    }
  }

  callRuntimeCallbacks(__ATPOSTRUN__);
}

function addOnPreRun(cb) {
  __ATPRERUN__.unshift(cb);
}

function addOnInit(cb) {
  __ATINIT__.unshift(cb);
}

function addOnPreMain(cb) {
  __ATMAIN__.unshift(cb);
}

function addOnExit(cb) {
}

function addOnPostRun(cb) {
  __ATPOSTRUN__.unshift(cb);
}

// A counter of dependencies for calling run(). If we need to
// do asynchronous work before running, increment this and
// decrement it. Incrementing must happen in a place like
// Module.preRun (used by emcc to add file preloading).
// Note that you can add dependencies in preRun, even though
// it happens right before run - run will be postponed until
// the dependencies are met.
var runDependencies = 0;
var dependenciesFulfilled = null; // overridden to take different actions when all run dependencies are fulfilled
var runDependencyTracking = {};
var runDependencyWatcher = null;

function getUniqueRunDependency(id) {
  var orig = id;
  while (1) {
    if (!runDependencyTracking[id]) return id;
    id = orig + Math.random();
  }
}

function addRunDependency(id) {
  runDependencies++;

  Module['monitorRunDependencies']?.(runDependencies);

  if (id) {
    assert(!runDependencyTracking[id]);
    runDependencyTracking[id] = 1;
    if (runDependencyWatcher === null && typeof setInterval != 'undefined') {
      // Check for missing dependencies every few seconds
      runDependencyWatcher = setInterval(() => {
        if (ABORT) {
          clearInterval(runDependencyWatcher);
          runDependencyWatcher = null;
          return;
        }
        var shown = false;
        for (var dep in runDependencyTracking) {
          if (!shown) {
            shown = true;
            err('still waiting on run dependencies:');
          }
          err(`dependency: ${dep}`);
        }
        if (shown) {
          err('(end of list)');
        }
      }, 10000);
    }
  } else {
    err('warning: run dependency added without ID');
  }
}

function removeRunDependency(id) {
  runDependencies--;

  Module['monitorRunDependencies']?.(runDependencies);

  if (id) {
    assert(runDependencyTracking[id]);
    delete runDependencyTracking[id];
  } else {
    err('warning: run dependency removed without ID');
  }
  if (runDependencies == 0) {
    if (runDependencyWatcher !== null) {
      clearInterval(runDependencyWatcher);
      runDependencyWatcher = null;
    }
    if (dependenciesFulfilled) {
      var callback = dependenciesFulfilled;
      dependenciesFulfilled = null;
      callback(); // can add another dependenciesFulfilled
    }
  }
}

/** @param {string|number=} what */
function abort(what) {
  Module['onAbort']?.(what);

  what = 'Aborted(' + what + ')';
  // TODO(sbc): Should we remove printing and leave it up to whoever
  // catches the exception?
  err(what);

  ABORT = true;

  // Use a wasm runtime error, because a JS error might be seen as a foreign
  // exception, which means we'd run destructors on it. We need the error to
  // simply make the program stop.
  // FIXME This approach does not work in Wasm EH because it currently does not assume
  // all RuntimeErrors are from traps; it decides whether a RuntimeError is from
  // a trap or not based on a hidden field within the object. So at the moment
  // we don't have a way of throwing a wasm trap from JS. TODO Make a JS API that
  // allows this in the wasm spec.

  // Suppress closure compiler warning here. Closure compiler's builtin extern
  // definition for WebAssembly.RuntimeError claims it takes no arguments even
  // though it can.
  // TODO(https://github.com/google/closure-compiler/pull/3913): Remove if/when upstream closure gets fixed.
  /** @suppress {checkTypes} */
  var e = new WebAssembly.RuntimeError(what);

  readyPromiseReject(e);
  // Throw the error whether or not MODULARIZE is set because abort is used
  // in code paths apart from instantiation where an exception is expected
  // to be thrown when abort is called.
  throw e;
}

function createExportWrapper(name, nargs) {
  return (...args) => {
    assert(runtimeInitialized, `native function \`${name}\` called before runtime initialization`);
    var f = wasmExports[name];
    assert(f, `exported native function \`${name}\` not found`);
    // Only assert for too many arguments. Too few can be valid since the missing arguments will be zero filled.
    assert(args.length <= nargs, `native function \`${name}\` called with ${args.length} args but expects ${nargs}`);
    return f(...args);
  };
}

var wasmBinaryFile;
function findWasmBinary() {
  if (Module['locateFile']) {
    var f = 'bjvm_main.wasm';
    if (!isDataURI(f)) {
      return locateFile(f);
    }
    return f;
  }
  // Use bundler-friendly `new URL(..., import.meta.url)` pattern; works in browsers too.
  return new URL('bjvm_main.wasm', import.meta.url).href;
}

function getBinarySync(file) {
  if (file == wasmBinaryFile && wasmBinary) {
    return new Uint8Array(wasmBinary);
  }
  if (readBinary) {
    return readBinary(file);
  }
  throw 'both async and sync fetching of the wasm failed';
}

async function getWasmBinary(binaryFile) {
  // If we don't have the binary yet, load it asynchronously using readAsync.
  if (!wasmBinary
      ) {
    // Fetch the binary using readAsync
    try {
      var response = await readAsync(binaryFile);
      return new Uint8Array(response);
    } catch {
      // Fall back to getBinarySync below;
    }
  }

  // Otherwise, getBinarySync should be able to get it synchronously
  return getBinarySync(binaryFile);
}

async function instantiateArrayBuffer(binaryFile, imports) {
  try {
    var binary = await getWasmBinary(binaryFile);
    var instance = await WebAssembly.instantiate(binary, imports);
    return instance;
  } catch (reason) {
    err(`failed to asynchronously prepare wasm: ${reason}`);

    // Warn on some common problems.
    if (isFileURI(wasmBinaryFile)) {
      err(`warning: Loading from a file URI (${wasmBinaryFile}) is not supported in most browsers. See https://emscripten.org/docs/getting_started/FAQ.html#how-do-i-run-a-local-webserver-for-testing-why-does-my-program-stall-in-downloading-or-preparing`);
    }
    abort(reason);
  }
}

async function instantiateAsync(binary, binaryFile, imports) {
  if (!binary &&
      typeof WebAssembly.instantiateStreaming == 'function' &&
      !isDataURI(binaryFile)
     ) {
    try {
      var response = fetch(binaryFile, { credentials: 'same-origin' });
      var instantiationResult = await WebAssembly.instantiateStreaming(response, imports);
      return instantiationResult;
    } catch (reason) {
      // We expect the most common failure cause to be a bad MIME type for the binary,
      // in which case falling back to ArrayBuffer instantiation should work.
      err(`wasm streaming compile failed: ${reason}`);
      err('falling back to ArrayBuffer instantiation');
      // fall back of instantiateArrayBuffer below
    };
  }
  return instantiateArrayBuffer(binaryFile, imports);
}

function getWasmImports() {
  // prepare imports
  return {
    'env': wasmImports,
    'wasi_snapshot_preview1': wasmImports,
  }
}

// Create the wasm instance.
// Receives the wasm imports, returns the exports.
async function createWasm() {
  // Load the wasm module and create an instance of using native support in the JS engine.
  // handle a generated wasm instance, receiving its exports and
  // performing other necessary setup
  /** @param {WebAssembly.Module=} module*/
  function receiveInstance(instance, module) {
    wasmExports = instance.exports;

    

    wasmMemory = wasmExports['memory'];
    Module['wasmMemory'] = wasmMemory;
    assert(wasmMemory, 'memory not found in wasm exports');
    updateMemoryViews();

    wasmTable = wasmExports['__indirect_function_table'];
    Module['wasmTable'] = wasmTable;
    assert(wasmTable, 'table not found in wasm exports');

    addOnInit(wasmExports['__wasm_call_ctors']);

    removeRunDependency('wasm-instantiate');
    return wasmExports;
  }
  // wait for the pthread pool (if any)
  addRunDependency('wasm-instantiate');

  // Prefer streaming instantiation if available.
  // Async compilation can be confusing when an error on the page overwrites Module
  // (for example, if the order of elements is wrong, and the one defining Module is
  // later), so we save Module and check it later.
  var trueModule = Module;
  function receiveInstantiationResult(result) {
    // 'result' is a ResultObject object which has both the module and instance.
    // receiveInstance() will swap in the exports (to Module.asm) so they can be called
    assert(Module === trueModule, 'the Module object should not be replaced during async compilation - perhaps the order of HTML elements is wrong?');
    trueModule = null;
    // TODO: Due to Closure regression https://github.com/google/closure-compiler/issues/3193, the above line no longer optimizes out down to the following line.
    // When the regression is fixed, can restore the above PTHREADS-enabled path.
    return receiveInstance(result['instance']);
  }

  var info = getWasmImports();

  // User shell pages can write their own Module.instantiateWasm = function(imports, successCallback) callback
  // to manually instantiate the Wasm module themselves. This allows pages to
  // run the instantiation parallel to any other async startup actions they are
  // performing.
  // Also pthreads and wasm workers initialize the wasm instance through this
  // path.
  if (Module['instantiateWasm']) {
    try {
      return Module['instantiateWasm'](info, receiveInstance);
    } catch(e) {
      err(`Module.instantiateWasm callback failed with error: ${e}`);
        // If instantiation fails, reject the module ready promise.
        readyPromiseReject(e);
    }
  }

  wasmBinaryFile ??= findWasmBinary();

  try {
    var result = await instantiateAsync(wasmBinary, wasmBinaryFile, info);
    var exports = receiveInstantiationResult(result);
    return exports;
  } catch (e) {
    // If instantiation fails, reject the module ready promise.
    readyPromiseReject(e);
    return Promise.reject(e);
  }
}

// === Body ===

var ASM_CONSTS = {
  5080240: () => { if (ENVIRONMENT_IS_NODE) { if (!FS.initialized) FS.init(); const fs = require('f' + String.fromCharCode(115)); const needed = ([ 'jdk23.jar', 'jdk23/lib/modules' ]); for (let i = 0; i < needed.length; ++i) { const path = needed[i]; const data = fs.readFileSync("./" + path); FS.createPath('/', path.substring(0, path.lastIndexOf('/'))); FS.writeFile(path, data); } } },  
 5080611: ($0) => { const fs = require('f' + String.fromCharCode(115)); return fs.existsSync(UTF8ToString($0)); },  
 5080707: ($0, $1) => { const fs = require('f' + String.fromCharCode(115)); const buffer = fs.readFileSync(UTF8ToString($0)); const length = buffer.length; const result = _malloc(length); Module.HEAPU32[$1 >> 2] = length; Module.HEAPU8.set(buffer, result); return result; },  
 5080959: () => { return ENVIRONMENT_IS_NODE; },  
 5080991: () => { return ENVIRONMENT_IS_NODE; }
};

// end include: preamble.js


  class ExitStatus {
      name = 'ExitStatus';
      constructor(status) {
        this.message = `Program terminated with exit(${status})`;
        this.status = status;
      }
    }

  var callRuntimeCallbacks = (callbacks) => {
      while (callbacks.length > 0) {
        // Pass the module as the first argument.
        callbacks.shift()(Module);
      }
    };

  
    /**
     * @param {number} ptr
     * @param {string} type
     */
  function getValue(ptr, type = 'i8') {
    if (type.endsWith('*')) type = '*';
    switch (type) {
      case 'i1': return HEAP8[ptr];
      case 'i8': return HEAP8[ptr];
      case 'i16': return HEAP16[((ptr)>>1)];
      case 'i32': return HEAP32[((ptr)>>2)];
      case 'i64': return HEAP64[((ptr)>>3)];
      case 'float': return HEAPF32[((ptr)>>2)];
      case 'double': return HEAPF64[((ptr)>>3)];
      case '*': return HEAPU32[((ptr)>>2)];
      default: abort(`invalid type for getValue: ${type}`);
    }
  }

  var noExitRuntime = Module['noExitRuntime'] || true;

  var ptrToString = (ptr) => {
      assert(typeof ptr === 'number');
      // With CAN_ADDRESS_2GB or MEMORY64, pointers are already unsigned.
      ptr >>>= 0;
      return '0x' + ptr.toString(16).padStart(8, '0');
    };

  
    /**
     * @param {number} ptr
     * @param {number} value
     * @param {string} type
     */
  function setValue(ptr, value, type = 'i8') {
    if (type.endsWith('*')) type = '*';
    switch (type) {
      case 'i1': HEAP8[ptr] = value; break;
      case 'i8': HEAP8[ptr] = value; break;
      case 'i16': HEAP16[((ptr)>>1)] = value; break;
      case 'i32': HEAP32[((ptr)>>2)] = value; break;
      case 'i64': HEAP64[((ptr)>>3)] = BigInt(value); break;
      case 'float': HEAPF32[((ptr)>>2)] = value; break;
      case 'double': HEAPF64[((ptr)>>3)] = value; break;
      case '*': HEAPU32[((ptr)>>2)] = value; break;
      default: abort(`invalid type for setValue: ${type}`);
    }
  }

  var stackRestore = (val) => __emscripten_stack_restore(val);

  var stackSave = () => _emscripten_stack_get_current();

  var warnOnce = (text) => {
      warnOnce.shown ||= {};
      if (!warnOnce.shown[text]) {
        warnOnce.shown[text] = 1;
        err(text);
      }
    };

  var UTF8Decoder = typeof TextDecoder != 'undefined' ? new TextDecoder() : undefined;
  
    /**
     * Given a pointer 'idx' to a null-terminated UTF8-encoded string in the given
     * array that contains uint8 values, returns a copy of that string as a
     * Javascript String object.
     * heapOrArray is either a regular array, or a JavaScript typed array view.
     * @param {number=} idx
     * @param {number=} maxBytesToRead
     * @return {string}
     */
  var UTF8ArrayToString = (heapOrArray, idx = 0, maxBytesToRead = NaN) => {
      var endIdx = idx + maxBytesToRead;
      var endPtr = idx;
      // TextDecoder needs to know the byte length in advance, it doesn't stop on
      // null terminator by itself.  Also, use the length info to avoid running tiny
      // strings through TextDecoder, since .subarray() allocates garbage.
      // (As a tiny code save trick, compare endPtr against endIdx using a negation,
      // so that undefined/NaN means Infinity)
      while (heapOrArray[endPtr] && !(endPtr >= endIdx)) ++endPtr;
  
      if (endPtr - idx > 16 && heapOrArray.buffer && UTF8Decoder) {
        return UTF8Decoder.decode(heapOrArray.subarray(idx, endPtr));
      }
      var str = '';
      // If building with TextDecoder, we have already computed the string length
      // above, so test loop end condition against that
      while (idx < endPtr) {
        // For UTF8 byte structure, see:
        // http://en.wikipedia.org/wiki/UTF-8#Description
        // https://www.ietf.org/rfc/rfc2279.txt
        // https://tools.ietf.org/html/rfc3629
        var u0 = heapOrArray[idx++];
        if (!(u0 & 0x80)) { str += String.fromCharCode(u0); continue; }
        var u1 = heapOrArray[idx++] & 63;
        if ((u0 & 0xE0) == 0xC0) { str += String.fromCharCode(((u0 & 31) << 6) | u1); continue; }
        var u2 = heapOrArray[idx++] & 63;
        if ((u0 & 0xF0) == 0xE0) {
          u0 = ((u0 & 15) << 12) | (u1 << 6) | u2;
        } else {
          if ((u0 & 0xF8) != 0xF0) warnOnce('Invalid UTF-8 leading byte ' + ptrToString(u0) + ' encountered when deserializing a UTF-8 string in wasm memory to a JS string!');
          u0 = ((u0 & 7) << 18) | (u1 << 12) | (u2 << 6) | (heapOrArray[idx++] & 63);
        }
  
        if (u0 < 0x10000) {
          str += String.fromCharCode(u0);
        } else {
          var ch = u0 - 0x10000;
          str += String.fromCharCode(0xD800 | (ch >> 10), 0xDC00 | (ch & 0x3FF));
        }
      }
      return str;
    };
  
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
  var UTF8ToString = (ptr, maxBytesToRead) => {
      assert(typeof ptr == 'number', `UTF8ToString expects a number (got ${typeof ptr})`);
      return ptr ? UTF8ArrayToString(HEAPU8, ptr, maxBytesToRead) : '';
    };
  var ___assert_fail = (condition, filename, line, func) =>
      abort(`Assertion failed: ${UTF8ToString(condition)}, at: ` + [filename ? UTF8ToString(filename) : 'unknown filename', line, func ? UTF8ToString(func) : 'unknown function']);

  var wasmTableMirror = [];
  
  /** @type {WebAssembly.Table} */
  var wasmTable;
  var getWasmTableEntry = (funcPtr) => {
      var func = wasmTableMirror[funcPtr];
      if (!func) {
        if (funcPtr >= wasmTableMirror.length) wasmTableMirror.length = funcPtr + 1;
        /** @suppress {checkTypes} */
        wasmTableMirror[funcPtr] = func = wasmTable.get(funcPtr);
      }
      /** @suppress {checkTypes} */
      assert(wasmTable.get(funcPtr) == func, 'JavaScript-side Wasm function table mirror is out of date!');
      return func;
    };
  var ___call_sighandler = (fp, sig) => getWasmTableEntry(fp)(sig);

  /** @type {function(...*):?} */
  function ___interpreter_intrinsic_notco_call_outlined(
  ) {
  abort('missing function: __interpreter_intrinsic_notco_call_outlined');
  }
  ___interpreter_intrinsic_notco_call_outlined.stub = true;

  var __abort_js = () =>
      abort('native code called abort()');

  var getExecutableName = () => thisProgram || './this.program';
  
  var stringToUTF8Array = (str, heap, outIdx, maxBytesToWrite) => {
      assert(typeof str === 'string', `stringToUTF8Array expects a string (got ${typeof str})`);
      // Parameter maxBytesToWrite is not optional. Negative values, 0, null,
      // undefined and false each don't write out any bytes.
      if (!(maxBytesToWrite > 0))
        return 0;
  
      var startIdx = outIdx;
      var endIdx = outIdx + maxBytesToWrite - 1; // -1 for string null terminator.
      for (var i = 0; i < str.length; ++i) {
        // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code
        // unit, not a Unicode code point of the character! So decode
        // UTF16->UTF32->UTF8.
        // See http://unicode.org/faq/utf_bom.html#utf16-3
        // For UTF8 byte structure, see http://en.wikipedia.org/wiki/UTF-8#Description
        // and https://www.ietf.org/rfc/rfc2279.txt
        // and https://tools.ietf.org/html/rfc3629
        var u = str.charCodeAt(i); // possibly a lead surrogate
        if (u >= 0xD800 && u <= 0xDFFF) {
          var u1 = str.charCodeAt(++i);
          u = 0x10000 + ((u & 0x3FF) << 10) | (u1 & 0x3FF);
        }
        if (u <= 0x7F) {
          if (outIdx >= endIdx) break;
          heap[outIdx++] = u;
        } else if (u <= 0x7FF) {
          if (outIdx + 1 >= endIdx) break;
          heap[outIdx++] = 0xC0 | (u >> 6);
          heap[outIdx++] = 0x80 | (u & 63);
        } else if (u <= 0xFFFF) {
          if (outIdx + 2 >= endIdx) break;
          heap[outIdx++] = 0xE0 | (u >> 12);
          heap[outIdx++] = 0x80 | ((u >> 6) & 63);
          heap[outIdx++] = 0x80 | (u & 63);
        } else {
          if (outIdx + 3 >= endIdx) break;
          if (u > 0x10FFFF) warnOnce('Invalid Unicode code point ' + ptrToString(u) + ' encountered when serializing a JS string to a UTF-8 string in wasm memory! (Valid unicode code points should be in range 0-0x10FFFF).');
          heap[outIdx++] = 0xF0 | (u >> 18);
          heap[outIdx++] = 0x80 | ((u >> 12) & 63);
          heap[outIdx++] = 0x80 | ((u >> 6) & 63);
          heap[outIdx++] = 0x80 | (u & 63);
        }
      }
      // Null-terminate the pointer to the buffer.
      heap[outIdx] = 0;
      return outIdx - startIdx;
    };
  var stringToUTF8 = (str, outPtr, maxBytesToWrite) => {
      assert(typeof maxBytesToWrite == 'number', 'stringToUTF8(str, outPtr, maxBytesToWrite) is missing the third parameter that specifies the length of the output buffer!');
      return stringToUTF8Array(str, HEAPU8, outPtr, maxBytesToWrite);
    };
  var __emscripten_get_progname = (str, len) => stringToUTF8(getExecutableName(), str, len);

  var runtimeKeepaliveCounter = 0;
  var __emscripten_runtime_keepalive_clear = () => {
      noExitRuntime = false;
      runtimeKeepaliveCounter = 0;
    };

  var __emscripten_throw_longjmp = () => {
      throw Infinity;
    };

  var timers = {
  };
  
  var handleException = (e) => {
      // Certain exception types we do not treat as errors since they are used for
      // internal control flow.
      // 1. ExitStatus, which is thrown by exit()
      // 2. "unwind", which is thrown by emscripten_unwind_to_js_event_loop() and others
      //    that wish to return to JS event loop.
      if (e instanceof ExitStatus || e == 'unwind') {
        return EXITSTATUS;
      }
      checkStackCookie();
      if (e instanceof WebAssembly.RuntimeError) {
        if (_emscripten_stack_get_current() <= 0) {
          err('Stack overflow detected.  You can try increasing -sSTACK_SIZE (currently set to 5000000)');
        }
      }
      quit_(1, e);
    };
  
  
  var keepRuntimeAlive = () => noExitRuntime || runtimeKeepaliveCounter > 0;
  var _proc_exit = (code) => {
      EXITSTATUS = code;
      if (!keepRuntimeAlive()) {
        Module['onExit']?.(code);
        ABORT = true;
      }
      quit_(code, new ExitStatus(code));
    };
  
  
  /** @suppress {duplicate } */
  /** @param {boolean|number=} implicit */
  var exitJS = (status, implicit) => {
      EXITSTATUS = status;
  
      checkUnflushedContent();
  
      // if exit() was called explicitly, warn the user if the runtime isn't actually being shut down
      if (keepRuntimeAlive() && !implicit) {
        var msg = `program exited (with status: ${status}), but keepRuntimeAlive() is set (counter=${runtimeKeepaliveCounter}) due to an async operation, so halting execution but not exiting the runtime or preventing further async execution (you can use emscripten_force_exit, if you want to force a true shutdown)`;
        readyPromiseReject(msg);
        err(msg);
      }
  
      _proc_exit(status);
    };
  var _exit = exitJS;
  
  
  var maybeExit = () => {
      if (!keepRuntimeAlive()) {
        try {
          _exit(EXITSTATUS);
        } catch (e) {
          handleException(e);
        }
      }
    };
  var callUserCallback = (func) => {
      if (ABORT) {
        err('user callback triggered after runtime exited or application aborted.  Ignoring.');
        return;
      }
      try {
        func();
        maybeExit();
      } catch (e) {
        handleException(e);
      }
    };
  
  
  var _emscripten_get_now = () => performance.now();
  var __setitimer_js = (which, timeout_ms) => {
      // First, clear any existing timer.
      if (timers[which]) {
        clearTimeout(timers[which].id);
        delete timers[which];
      }
  
      // A timeout of zero simply cancels the current timeout so we have nothing
      // more to do.
      if (!timeout_ms) return 0;
  
      var id = setTimeout(() => {
        assert(which in timers);
        delete timers[which];
        callUserCallback(() => __emscripten_timeout(which, _emscripten_get_now()));
      }, timeout_ms);
      timers[which] = { id, timeout_ms };
      return 0;
    };

  
  var lengthBytesUTF8 = (str) => {
      var len = 0;
      for (var i = 0; i < str.length; ++i) {
        // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code
        // unit, not a Unicode code point of the character! So decode
        // UTF16->UTF32->UTF8.
        // See http://unicode.org/faq/utf_bom.html#utf16-3
        var c = str.charCodeAt(i); // possibly a lead surrogate
        if (c <= 0x7F) {
          len++;
        } else if (c <= 0x7FF) {
          len += 2;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
          len += 4; ++i;
        } else {
          len += 3;
        }
      }
      return len;
    };
  var __tzset_js = (timezone, daylight, std_name, dst_name) => {
      // TODO: Use (malleable) environment variables instead of system settings.
      var currentYear = new Date().getFullYear();
      var winter = new Date(currentYear, 0, 1);
      var summer = new Date(currentYear, 6, 1);
      var winterOffset = winter.getTimezoneOffset();
      var summerOffset = summer.getTimezoneOffset();
  
      // Local standard timezone offset. Local standard time is not adjusted for
      // daylight savings.  This code uses the fact that getTimezoneOffset returns
      // a greater value during Standard Time versus Daylight Saving Time (DST).
      // Thus it determines the expected output during Standard Time, and it
      // compares whether the output of the given date the same (Standard) or less
      // (DST).
      var stdTimezoneOffset = Math.max(winterOffset, summerOffset);
  
      // timezone is specified as seconds west of UTC ("The external variable
      // `timezone` shall be set to the difference, in seconds, between
      // Coordinated Universal Time (UTC) and local standard time."), the same
      // as returned by stdTimezoneOffset.
      // See http://pubs.opengroup.org/onlinepubs/009695399/functions/tzset.html
      HEAPU32[((timezone)>>2)] = stdTimezoneOffset * 60;
  
      HEAP32[((daylight)>>2)] = Number(winterOffset != summerOffset);
  
      var extractZone = (timezoneOffset) => {
        // Why inverse sign?
        // Read here https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Date/getTimezoneOffset
        var sign = timezoneOffset >= 0 ? "-" : "+";
  
        var absOffset = Math.abs(timezoneOffset)
        var hours = String(Math.floor(absOffset / 60)).padStart(2, "0");
        var minutes = String(absOffset % 60).padStart(2, "0");
  
        return `UTC${sign}${hours}${minutes}`;
      }
  
      var winterName = extractZone(winterOffset);
      var summerName = extractZone(summerOffset);
      assert(winterName);
      assert(summerName);
      assert(lengthBytesUTF8(winterName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${winterName})`);
      assert(lengthBytesUTF8(summerName) <= 16, `timezone name truncated to fit in TZNAME_MAX (${summerName})`);
      if (summerOffset < winterOffset) {
        // Northern hemisphere
        stringToUTF8(winterName, std_name, 17);
        stringToUTF8(summerName, dst_name, 17);
      } else {
        stringToUTF8(winterName, dst_name, 17);
        stringToUTF8(summerName, std_name, 17);
      }
    };

  var __wasmfs_copy_preloaded_file_data = (index, buffer) =>
      HEAPU8.set(wasmFSPreloadedFiles[index].fileData, buffer);

  var wasmFSPreloadedDirs = [];
  var __wasmfs_get_num_preloaded_dirs = () => wasmFSPreloadedDirs.length;

  var wasmFSPreloadedFiles = [];
  
  var wasmFSPreloadingFlushed = false;
  var __wasmfs_get_num_preloaded_files = () => {
      // When this method is called from WasmFS it means that we are about to
      // flush all the preloaded data, so mark that. (There is no call that
      // occurs at the end of that flushing, which would be more natural, but it
      // is fine to mark the flushing here as during the flushing itself no user
      // code can run, so nothing will check whether we have flushed or not.)
      wasmFSPreloadingFlushed = true;
      return wasmFSPreloadedFiles.length;
    };

  var __wasmfs_get_preloaded_child_path = (index, childNameBuffer) => {
      var s = wasmFSPreloadedDirs[index].childName;
      var len = lengthBytesUTF8(s) + 1;
      stringToUTF8(s, childNameBuffer, len);
    };

  var __wasmfs_get_preloaded_file_mode = (index) => wasmFSPreloadedFiles[index].mode;

  var __wasmfs_get_preloaded_file_size = (index) =>
      wasmFSPreloadedFiles[index].fileData.length;

  var __wasmfs_get_preloaded_parent_path = (index, parentPathBuffer) => {
      var s = wasmFSPreloadedDirs[index].parentPath;
      var len = lengthBytesUTF8(s) + 1;
      stringToUTF8(s, parentPathBuffer, len);
    };

  
  var __wasmfs_get_preloaded_path_name = (index, fileNameBuffer) => {
      var s = wasmFSPreloadedFiles[index].pathName;
      var len = lengthBytesUTF8(s) + 1;
      stringToUTF8(s, fileNameBuffer, len);
    };

  var __wasmfs_jsimpl_alloc_file = (backend, file) => {
      assert(wasmFS$backends[backend]);
      return wasmFS$backends[backend].allocFile(file);
    };

  var __wasmfs_jsimpl_free_file = (backend, file) => {
      assert(wasmFS$backends[backend]);
      return wasmFS$backends[backend].freeFile(file);
    };

  var __wasmfs_jsimpl_get_size = (backend, file) => {
      assert(wasmFS$backends[backend]);
      return wasmFS$backends[backend].getSize(file);
    };

  var INT53_MAX = 9007199254740992;
  
  var INT53_MIN = -9007199254740992;
  var bigintToI53Checked = (num) => (num < INT53_MIN || num > INT53_MAX) ? NaN : Number(num);
  function __wasmfs_jsimpl_read(backend, file, buffer, length, offset) {
    offset = bigintToI53Checked(offset);
  
    
      assert(wasmFS$backends[backend]);
      if (!wasmFS$backends[backend].read) {
        return -28;
      }
      return wasmFS$backends[backend].read(file, buffer, length, offset);
    ;
  }

  function __wasmfs_jsimpl_set_size(backend, file, size) {
    size = bigintToI53Checked(size);
  
    
      assert(wasmFS$backends[backend]);
      return wasmFS$backends[backend].setSize(file, size);
    ;
  }

  function __wasmfs_jsimpl_write(backend, file, buffer, length, offset) {
    offset = bigintToI53Checked(offset);
  
    
      assert(wasmFS$backends[backend]);
      if (!wasmFS$backends[backend].write) {
        return -28;
      }
      return wasmFS$backends[backend].write(file, buffer, length, offset);
    ;
  }

  var FS_stdin_getChar_buffer = [];
  
  
  /** @type {function(string, boolean=, number=)} */
  function intArrayFromString(stringy, dontAddNull, length) {
    var len = length > 0 ? length : lengthBytesUTF8(stringy)+1;
    var u8array = new Array(len);
    var numBytesWritten = stringToUTF8Array(stringy, u8array, 0, u8array.length);
    if (dontAddNull) u8array.length = numBytesWritten;
    return u8array;
  }
  var FS_stdin_getChar = () => {
      if (!FS_stdin_getChar_buffer.length) {
        var result = null;
        if (typeof window != 'undefined' &&
          typeof window.prompt == 'function') {
          // Browser.
          result = window.prompt('Input: ');  // returns null on cancel
          if (result !== null) {
            result += '\n';
          }
        } else
        {}
        if (!result) {
          return null;
        }
        FS_stdin_getChar_buffer = intArrayFromString(result, true);
      }
      return FS_stdin_getChar_buffer.shift();
    };
  var __wasmfs_stdin_get_char = () => {
      // Return the read character, or -1 to indicate EOF.
      var c = FS_stdin_getChar();
      if (typeof c === 'number') {
        return c;
      }
      return -1;
    };

  var readEmAsmArgsArray = [];
  var readEmAsmArgs = (sigPtr, buf) => {
      // Nobody should have mutated _readEmAsmArgsArray underneath us to be something else than an array.
      assert(Array.isArray(readEmAsmArgsArray));
      // The input buffer is allocated on the stack, so it must be stack-aligned.
      assert(buf % 16 == 0);
      readEmAsmArgsArray.length = 0;
      var ch;
      // Most arguments are i32s, so shift the buffer pointer so it is a plain
      // index into HEAP32.
      while (ch = HEAPU8[sigPtr++]) {
        var chr = String.fromCharCode(ch);
        var validChars = ['d', 'f', 'i', 'p'];
        // In WASM_BIGINT mode we support passing i64 values as bigint.
        validChars.push('j');
        assert(validChars.includes(chr), `Invalid character ${ch}("${chr}") in readEmAsmArgs! Use only [${validChars}], and do not specify "v" for void return argument.`);
        // Floats are always passed as doubles, so all types except for 'i'
        // are 8 bytes and require alignment.
        var wide = (ch != 105);
        wide &= (ch != 112);
        buf += wide && (buf % 8) ? 4 : 0;
        readEmAsmArgsArray.push(
          // Special case for pointers under wasm64 or CAN_ADDRESS_2GB mode.
          ch == 112 ? HEAPU32[((buf)>>2)] :
          ch == 106 ? HEAP64[((buf)>>3)] :
          ch == 105 ?
            HEAP32[((buf)>>2)] :
            HEAPF64[((buf)>>3)]
        );
        buf += wide ? 8 : 4;
      }
      return readEmAsmArgsArray;
    };
  var runEmAsmFunction = (code, sigPtr, argbuf) => {
      var args = readEmAsmArgs(sigPtr, argbuf);
      assert(ASM_CONSTS.hasOwnProperty(code), `No EM_ASM constant found at address ${code}.  The loaded WebAssembly file is likely out of sync with the generated JavaScript.`);
      return ASM_CONSTS[code](...args);
    };
  var _emscripten_asm_const_int = (code, sigPtr, argbuf) => {
      return runEmAsmFunction(code, sigPtr, argbuf);
    };

  var runMainThreadEmAsm = (emAsmAddr, sigPtr, argbuf, sync) => {
      var args = readEmAsmArgs(sigPtr, argbuf);
      assert(ASM_CONSTS.hasOwnProperty(emAsmAddr), `No EM_ASM constant found at address ${emAsmAddr}.  The loaded WebAssembly file is likely out of sync with the generated JavaScript.`);
      return ASM_CONSTS[emAsmAddr](...args);
    };
  var _emscripten_asm_const_int_sync_on_main_thread = (emAsmAddr, sigPtr, argbuf) => runMainThreadEmAsm(emAsmAddr, sigPtr, argbuf, 1);

  var _emscripten_asm_const_ptr = (code, sigPtr, argbuf) => {
      return runEmAsmFunction(code, sigPtr, argbuf);
    };

  var _emscripten_date_now = () => Date.now();

  var _emscripten_err = (str) => err(UTF8ToString(str));


  var _emscripten_out = (str) => out(UTF8ToString(str));

  var getHeapMax = () =>
      // Stay one Wasm page short of 4GB: while e.g. Chrome is able to allocate
      // full 4GB Wasm memories, the size will wrap back to 0 bytes in Wasm side
      // for any code that deals with heap sizes, which would require special
      // casing all heap size related code to treat 0 specially.
      2147483648;
  
  var alignMemory = (size, alignment) => {
      assert(alignment, "alignment argument is required");
      return Math.ceil(size / alignment) * alignment;
    };
  
  var growMemory = (size) => {
      var b = wasmMemory.buffer;
      var pages = ((size - b.byteLength + 65535) / 65536) | 0;
      try {
        // round size grow request up to wasm page size (fixed 64KB per spec)
        wasmMemory.grow(pages); // .grow() takes a delta compared to the previous size
        updateMemoryViews();
        return 1 /*success*/;
      } catch(e) {
        err(`growMemory: Attempted to grow heap from ${b.byteLength} bytes to ${size} bytes, but got error: ${e}`);
      }
      // implicit 0 return to save code size (caller will cast "undefined" into 0
      // anyhow)
    };
  var _emscripten_resize_heap = (requestedSize) => {
      var oldSize = HEAPU8.length;
      // With CAN_ADDRESS_2GB or MEMORY64, pointers are already unsigned.
      requestedSize >>>= 0;
      // With multithreaded builds, races can happen (another thread might increase the size
      // in between), so return a failure, and let the caller retry.
      assert(requestedSize > oldSize);
  
      // Memory resize rules:
      // 1.  Always increase heap size to at least the requested size, rounded up
      //     to next page multiple.
      // 2a. If MEMORY_GROWTH_LINEAR_STEP == -1, excessively resize the heap
      //     geometrically: increase the heap size according to
      //     MEMORY_GROWTH_GEOMETRIC_STEP factor (default +20%), At most
      //     overreserve by MEMORY_GROWTH_GEOMETRIC_CAP bytes (default 96MB).
      // 2b. If MEMORY_GROWTH_LINEAR_STEP != -1, excessively resize the heap
      //     linearly: increase the heap size by at least
      //     MEMORY_GROWTH_LINEAR_STEP bytes.
      // 3.  Max size for the heap is capped at 2048MB-WASM_PAGE_SIZE, or by
      //     MAXIMUM_MEMORY, or by ASAN limit, depending on which is smallest
      // 4.  If we were unable to allocate as much memory, it may be due to
      //     over-eager decision to excessively reserve due to (3) above.
      //     Hence if an allocation fails, cut down on the amount of excess
      //     growth, in an attempt to succeed to perform a smaller allocation.
  
      // A limit is set for how much we can grow. We should not exceed that
      // (the wasm binary specifies it, so if we tried, we'd fail anyhow).
      var maxHeapSize = getHeapMax();
      if (requestedSize > maxHeapSize) {
        err(`Cannot enlarge memory, requested ${requestedSize} bytes, but the limit is ${maxHeapSize} bytes!`);
        return false;
      }
  
      // Loop through potential heap size increases. If we attempt a too eager
      // reservation that fails, cut down on the attempted size and reserve a
      // smaller bump instead. (max 3 times, chosen somewhat arbitrarily)
      for (var cutDown = 1; cutDown <= 4; cutDown *= 2) {
        var overGrownHeapSize = oldSize * (1 + 0.2 / cutDown); // ensure geometric growth
        // but limit overreserving (default to capping at +96MB overgrowth at most)
        overGrownHeapSize = Math.min(overGrownHeapSize, requestedSize + 100663296 );
  
        var newSize = Math.min(maxHeapSize, alignMemory(Math.max(requestedSize, overGrownHeapSize), 65536));
  
        var replacement = growMemory(newSize);
        if (replacement) {
  
          return true;
        }
      }
      err(`Failed to grow the heap from ${oldSize} bytes to ${newSize} bytes, not enough memory!`);
      return false;
    };

  var ENV = {
  };
  
  var getEnvStrings = () => {
      if (!getEnvStrings.strings) {
        // Default values.
        // Browser language detection #8751
        var lang = ((typeof navigator == 'object' && navigator.languages && navigator.languages[0]) || 'C').replace('-', '_') + '.UTF-8';
        var env = {
          'USER': 'web_user',
          'LOGNAME': 'web_user',
          'PATH': '/',
          'PWD': '/',
          'HOME': '/home/web_user',
          'LANG': lang,
          '_': getExecutableName()
        };
        // Apply the user-provided values, if any.
        for (var x in ENV) {
          // x is a key in ENV; if ENV[x] is undefined, that means it was
          // explicitly set to be so. We allow user code to do that to
          // force variables with default values to remain unset.
          if (ENV[x] === undefined) delete env[x];
          else env[x] = ENV[x];
        }
        var strings = [];
        for (var x in env) {
          strings.push(`${x}=${env[x]}`);
        }
        getEnvStrings.strings = strings;
      }
      return getEnvStrings.strings;
    };
  
  var stringToAscii = (str, buffer) => {
      for (var i = 0; i < str.length; ++i) {
        assert(str.charCodeAt(i) === (str.charCodeAt(i) & 0xff));
        HEAP8[buffer++] = str.charCodeAt(i);
      }
      // Null-terminate the string
      HEAP8[buffer] = 0;
    };
  var _environ_get = (__environ, environ_buf) => {
      var bufSize = 0;
      getEnvStrings().forEach((string, i) => {
        var ptr = environ_buf + bufSize;
        HEAPU32[(((__environ)+(i*4))>>2)] = ptr;
        stringToAscii(string, ptr);
        bufSize += string.length + 1;
      });
      return 0;
    };

  var _environ_sizes_get = (penviron_count, penviron_buf_size) => {
      var strings = getEnvStrings();
      HEAPU32[((penviron_count)>>2)] = strings.length;
      var bufSize = 0;
      strings.forEach((string) => bufSize += string.length + 1);
      HEAPU32[((penviron_buf_size)>>2)] = bufSize;
      return 0;
    };



  var initRandomFill = () => {
  
      return (view) => crypto.getRandomValues(view);
    };
  var randomFill = (view) => {
      // Lazily init on the first invocation.
      (randomFill = initRandomFill())(view);
    };
  var _random_get = (buffer, size) => {
      randomFill(HEAPU8.subarray(buffer, buffer + size));
      return 0;
    };




  var MEMFS = {
  createBackend(opts) {
        return _wasmfs_create_memory_backend();
      },
  };
  
  
  
  
  var PATH = {
  isAbs:(path) => path.charAt(0) === '/',
  splitPath:(filename) => {
        var splitPathRe = /^(\/?|)([\s\S]*?)((?:\.{1,2}|[^\/]+?|)(\.[^.\/]*|))(?:[\/]*)$/;
        return splitPathRe.exec(filename).slice(1);
      },
  normalizeArray:(parts, allowAboveRoot) => {
        // if the path tries to go above the root, `up` ends up > 0
        var up = 0;
        for (var i = parts.length - 1; i >= 0; i--) {
          var last = parts[i];
          if (last === '.') {
            parts.splice(i, 1);
          } else if (last === '..') {
            parts.splice(i, 1);
            up++;
          } else if (up) {
            parts.splice(i, 1);
            up--;
          }
        }
        // if the path is allowed to go above the root, restore leading ..s
        if (allowAboveRoot) {
          for (; up; up--) {
            parts.unshift('..');
          }
        }
        return parts;
      },
  normalize:(path) => {
        var isAbsolute = PATH.isAbs(path),
            trailingSlash = path.substr(-1) === '/';
        // Normalize the path
        path = PATH.normalizeArray(path.split('/').filter((p) => !!p), !isAbsolute).join('/');
        if (!path && !isAbsolute) {
          path = '.';
        }
        if (path && trailingSlash) {
          path += '/';
        }
        return (isAbsolute ? '/' : '') + path;
      },
  dirname:(path) => {
        var result = PATH.splitPath(path),
            root = result[0],
            dir = result[1];
        if (!root && !dir) {
          // No dirname whatsoever
          return '.';
        }
        if (dir) {
          // It has a dirname, strip trailing slash
          dir = dir.substr(0, dir.length - 1);
        }
        return root + dir;
      },
  basename:(path) => path && path.match(/([^\/]+|\/)\/*$/)[1],
  join:(...paths) => PATH.normalize(paths.join('/')),
  join2:(l, r) => PATH.normalize(l + '/' + r),
  };
  
  
  
  var stackAlloc = (sz) => __emscripten_stack_alloc(sz);
  var stringToUTF8OnStack = (str) => {
      var size = lengthBytesUTF8(str) + 1;
      var ret = stackAlloc(size);
      stringToUTF8(str, ret, size);
      return ret;
    };
  
  
  var withStackSave = (f) => {
      var stack = stackSave();
      var ret = f();
      stackRestore(stack);
      return ret;
    };
  
  var readI53FromI64 = (ptr) => {
      return HEAPU32[((ptr)>>2)] + HEAP32[(((ptr)+(4))>>2)] * 4294967296;
    };
  
  var readI53FromU64 = (ptr) => {
      return HEAPU32[((ptr)>>2)] + HEAPU32[(((ptr)+(4))>>2)] * 4294967296;
    };
  
  
  
  var FS_mknod = (path, mode, dev) => FS.handleError(withStackSave(() => {
      var pathBuffer = stringToUTF8OnStack(path);
      return __wasmfs_mknod(pathBuffer, mode, dev);
    }));
  var FS_create = (path, mode = 0o666) => {
      mode &= 4095;
      mode |= 32768;
      return FS_mknod(path, mode, 0);
    };
  
  
  
  var FS_writeFile = (path, data) => {
      var sp = stackSave();
      var pathBuffer = stringToUTF8OnStack(path);
      if (typeof data == 'string') {
        var buf = new Uint8Array(lengthBytesUTF8(data) + 1);
        var actualNumBytes = stringToUTF8Array(data, buf, 0, buf.length);
        data = buf.slice(0, actualNumBytes);
      }
      var dataBuffer = _malloc(data.length);
      assert(dataBuffer);
      for (var i = 0; i < data.length; i++) {
        HEAP8[(dataBuffer)+(i)] = data[i];
      }
      var ret = __wasmfs_write_file(pathBuffer, dataBuffer, data.length);
      _free(dataBuffer);
      stackRestore(sp);
      return ret;
    };
  var FS_createDataFile = (parent, name, fileData, canRead, canWrite, canOwn) => {
      var pathName = name ? parent + '/' + name : parent;
      var mode = FS_getMode(canRead, canWrite);
  
      if (!wasmFSPreloadingFlushed) {
        // WasmFS code in the wasm is not ready to be called yet. Cache the
        // files we want to create here in JS, and WasmFS will read them
        // later.
        wasmFSPreloadedFiles.push({pathName, fileData, mode});
      } else {
        // WasmFS is already running, so create the file normally.
        FS_create(pathName, mode);
        FS_writeFile(pathName, fileData);
      }
    };
  
  var asyncLoad = async (url) => {
      var arrayBuffer = await readAsync(url);
      assert(arrayBuffer, `Loading data file "${url}" failed (no arrayBuffer).`);
      return new Uint8Array(arrayBuffer);
    };
  
  
  
  var PATH_FS = {
  resolve:(...args) => {
        var resolvedPath = '',
          resolvedAbsolute = false;
        for (var i = args.length - 1; i >= -1 && !resolvedAbsolute; i--) {
          var path = (i >= 0) ? args[i] : FS.cwd();
          // Skip empty and invalid entries
          if (typeof path != 'string') {
            throw new TypeError('Arguments to path.resolve must be strings');
          } else if (!path) {
            return ''; // an invalid portion invalidates the whole thing
          }
          resolvedPath = path + '/' + resolvedPath;
          resolvedAbsolute = PATH.isAbs(path);
        }
        // At this point the path should be resolved to a full absolute path, but
        // handle relative paths to be safe (might happen when process.cwd() fails)
        resolvedPath = PATH.normalizeArray(resolvedPath.split('/').filter((p) => !!p), !resolvedAbsolute).join('/');
        return ((resolvedAbsolute ? '/' : '') + resolvedPath) || '.';
      },
  relative:(from, to) => {
        from = PATH_FS.resolve(from).substr(1);
        to = PATH_FS.resolve(to).substr(1);
        function trim(arr) {
          var start = 0;
          for (; start < arr.length; start++) {
            if (arr[start] !== '') break;
          }
          var end = arr.length - 1;
          for (; end >= 0; end--) {
            if (arr[end] !== '') break;
          }
          if (start > end) return [];
          return arr.slice(start, end - start + 1);
        }
        var fromParts = trim(from.split('/'));
        var toParts = trim(to.split('/'));
        var length = Math.min(fromParts.length, toParts.length);
        var samePartsLength = length;
        for (var i = 0; i < length; i++) {
          if (fromParts[i] !== toParts[i]) {
            samePartsLength = i;
            break;
          }
        }
        var outputParts = [];
        for (var i = samePartsLength; i < fromParts.length; i++) {
          outputParts.push('..');
        }
        outputParts = outputParts.concat(toParts.slice(samePartsLength));
        return outputParts.join('/');
      },
  };
  
  
  var preloadPlugins = Module['preloadPlugins'] || [];
  var FS_handledByPreloadPlugin = (byteArray, fullname, finish, onerror) => {
      // Ensure plugins are ready.
      if (typeof Browser != 'undefined') Browser.init();
  
      var handled = false;
      preloadPlugins.forEach((plugin) => {
        if (handled) return;
        if (plugin['canHandle'](fullname)) {
          plugin['handle'](byteArray, fullname, finish, onerror);
          handled = true;
        }
      });
      return handled;
    };
  var FS_createPreloadedFile = (parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) => {
      // TODO we should allow people to just pass in a complete filename instead
      // of parent and name being that we just join them anyways
      var fullname = name ? PATH_FS.resolve(PATH.join2(parent, name)) : parent;
      var dep = getUniqueRunDependency(`cp ${fullname}`); // might have several active requests for the same fullname
      function processData(byteArray) {
        function finish(byteArray) {
          preFinish?.();
          if (!dontCreateFile) {
            FS_createDataFile(parent, name, byteArray, canRead, canWrite, canOwn);
          }
          onload?.();
          removeRunDependency(dep);
        }
        if (FS_handledByPreloadPlugin(byteArray, fullname, finish, () => {
          onerror?.();
          removeRunDependency(dep);
        })) {
          return;
        }
        finish(byteArray);
      }
      addRunDependency(dep);
      if (typeof url == 'string') {
        asyncLoad(url).then(processData, onerror);
      } else {
        processData(url);
      }
    };
  
  var FS_getMode = (canRead, canWrite) => {
      var mode = 0;
      if (canRead) mode |= 292 | 73;
      if (canWrite) mode |= 146;
      return mode;
    };
  
  
  var FS_modeStringToFlags = (str) => {
      var flagModes = {
        'r': 0,
        'r+': 2,
        'w': 512 | 64 | 1,
        'w+': 512 | 64 | 2,
        'a': 1024 | 64 | 1,
        'a+': 1024 | 64 | 2,
      };
      var flags = flagModes[str];
      if (typeof flags == 'undefined') {
        throw new Error(`Unknown file open mode: ${str}`);
      }
      return flags;
    };
  
  
  
  var FS_mkdir = (path, mode = 0o777) => FS.handleError(withStackSave(() => {
      var buffer = stringToUTF8OnStack(path);
      return __wasmfs_mkdir(buffer, mode);
    }));
  
  
    /**
     * @param {number=} mode Optionally, the mode to create in. Uses mkdir's
     *                       default if not set.
     */
  var FS_mkdirTree = (path, mode) => {
      var dirs = path.split('/');
      var d = '';
      for (var i = 0; i < dirs.length; ++i) {
        if (!dirs[i]) continue;
        d += '/' + dirs[i];
        try {
          FS_mkdir(d, mode);
        } catch(e) {
          if (e.errno != 20) throw e;
        }
      }
    };
  
  
  var FS_unlink = (path) => withStackSave(() => {
      var buffer = stringToUTF8OnStack(path);
      return __wasmfs_unlink(buffer);
    });
  
  
  
  
  var wasmFS$backends = {
  };
  
  var wasmFSDevices = {
  };
  
  var wasmFSDeviceStreams = {
  };
  
  var FS = {
  init() {
        FS.ensureErrnoError();
      },
  ErrnoError:null,
  handleError(returnValue) {
        // Assume errors correspond to negative returnValues
        // since some functions like _wasmfs_open() return positive
        // numbers on success (some callers of this function may need to negate the parameter).
        if (returnValue < 0) {
          throw new FS.ErrnoError(-returnValue);
        }
  
        return returnValue;
      },
  ensureErrnoError() {
        if (FS.ErrnoError) return;
        FS.ErrnoError = /** @this{Object} */ function ErrnoError(code) {
          this.errno = code;
          this.message = 'FS error';
          this.name = "ErrnoError";
        }
        FS.ErrnoError.prototype = new Error();
        FS.ErrnoError.prototype.constructor = FS.ErrnoError;
      },
  createDataFile(parent, name, fileData, canRead, canWrite, canOwn) {
        FS_createDataFile(parent, name, fileData, canRead, canWrite, canOwn);
      },
  createPath(parent, path, canRead, canWrite) {
        // Cache file path directory names.
        var parts = path.split('/').reverse();
        while (parts.length) {
          var part = parts.pop();
          if (!part) continue;
          var current = PATH.join2(parent, part);
          if (!wasmFSPreloadingFlushed) {
            wasmFSPreloadedDirs.push({parentPath: parent, childName: part});
          } else {
            FS.mkdir(current);
          }
          parent = current;
        }
        return current;
      },
  createPreloadedFile(parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish) {
        return FS_createPreloadedFile(parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn, preFinish);
      },
  readFile(path, opts = {}) {
        opts.encoding = opts.encoding || 'binary';
        if (opts.encoding !== 'utf8' && opts.encoding !== 'binary') {
          throw new Error('Invalid encoding type "' + opts.encoding + '"');
        }
  
        // Copy the file into a JS buffer on the heap.
        var sp = stackSave();
        var buf = __wasmfs_read_file(stringToUTF8OnStack(path));
        stackRestore(sp);
  
        // The signed integer length resides in the first 8 bytes of the buffer.
        var length = readI53FromI64(buf);
  
        // Default return type is binary.
        // The buffer contents exist 8 bytes after the returned pointer.
        var ret = new Uint8Array(HEAPU8.subarray(buf + 8, buf + 8 + length));
        if (opts.encoding === 'utf8') {
          ret = UTF8ArrayToString(ret);
        }
  
        return ret;
      },
  cwd:() => UTF8ToString(__wasmfs_get_cwd()),
  analyzePath(path) {
        // TODO: Consider simplifying this API, which for now matches the JS FS.
        var exists = !!FS.findObject(path);
        return {
          exists,
          object: {
            contents: exists ? FS.readFile(path) : null
          }
        };
      },
  mkdir:(path, mode) => FS_mkdir(path, mode),
  mkdirTree:(path, mode) => FS_mkdirTree(path, mode),
  rmdir:(path) => FS.handleError(
        withStackSave(() => __wasmfs_rmdir(stringToUTF8OnStack(path)))
      ),
  open:(path, flags, mode = 0o666) => withStackSave(() => {
        flags = typeof flags == 'string' ? FS_modeStringToFlags(flags) : flags;
        var buffer = stringToUTF8OnStack(path);
        var fd = FS.handleError(__wasmfs_open(buffer, flags, mode));
        return { fd : fd };
      }),
  create:(path, mode) => FS_create(path, mode),
  close:(stream) => FS.handleError(-__wasmfs_close(stream.fd)),
  unlink:(path) => FS_unlink(path),
  chdir:(path) => withStackSave(() => {
        var buffer = stringToUTF8OnStack(path);
        return __wasmfs_chdir(buffer);
      }),
  read(stream, buffer, offset, length, position) {
        var seeking = typeof position != 'undefined';
  
        var dataBuffer = _malloc(length);
  
        var bytesRead;
        if (seeking) {
          bytesRead = __wasmfs_pread(stream.fd, dataBuffer, length, BigInt(position));
        } else {
          bytesRead = __wasmfs_read(stream.fd, dataBuffer, length);
        }
        bytesRead = FS.handleError(bytesRead);
  
        for (var i = 0; i < length; i++) {
          buffer[offset + i] = HEAP8[(dataBuffer)+(i)]
        }
  
        _free(dataBuffer);
        return bytesRead;
      },
  write(stream, buffer, offset, length, position, canOwn) {
        var seeking = typeof position != 'undefined';
  
        var dataBuffer = _malloc(length);
        for (var i = 0; i < length; i++) {
          HEAP8[(dataBuffer)+(i)] = buffer[offset + i];
        }
  
        var bytesRead;
        if (seeking) {
          bytesRead = __wasmfs_pwrite(stream.fd, dataBuffer, length, BigInt(position));
        } else {
          bytesRead = __wasmfs_write(stream.fd, dataBuffer, length);
        }
        bytesRead = FS.handleError(bytesRead);
        _free(dataBuffer);
  
        return bytesRead;
      },
  allocate(stream, offset, length) {
        return FS.handleError(__wasmfs_allocate(stream.fd, BigInt(offset), BigInt(length)));
      },
  writeFile:(path, data) => FS_writeFile(path, data),
  mmap:(stream, length, offset, prot, flags) => {
        var buf = FS.handleError(__wasmfs_mmap(length, prot, flags, stream.fd, BigInt(offset)));
        return { ptr: buf, allocated: true };
      },
  msync:(stream, bufferPtr, offset, length, mmapFlags) => {
        assert(offset === 0);
        // TODO: assert that stream has the fd corresponding to the mapped buffer (bufferPtr).
        return FS.handleError(__wasmfs_msync(bufferPtr, length, mmapFlags));
      },
  munmap:(addr, length) => (
        FS.handleError(__wasmfs_munmap(addr, length))
      ),
  symlink:(target, linkpath) => withStackSave(() => (
        __wasmfs_symlink(stringToUTF8OnStack(target), stringToUTF8OnStack(linkpath))
      )),
  readlink(path) {
        var readBuffer = FS.handleError(withStackSave(() => __wasmfs_readlink(stringToUTF8OnStack(path))));
        return UTF8ToString(readBuffer);
      },
  statBufToObject(statBuf) {
        // i53/u53 are enough for times and ino in practice.
        return {
            dev: HEAPU32[((statBuf)>>2)],
            mode: HEAPU32[(((statBuf)+(4))>>2)],
            nlink: HEAPU32[(((statBuf)+(8))>>2)],
            uid: HEAPU32[(((statBuf)+(12))>>2)],
            gid: HEAPU32[(((statBuf)+(16))>>2)],
            rdev: HEAPU32[(((statBuf)+(20))>>2)],
            size: readI53FromI64((statBuf)+(24)),
            blksize: HEAPU32[(((statBuf)+(32))>>2)],
            blocks: HEAPU32[(((statBuf)+(36))>>2)],
            atime: readI53FromI64((statBuf)+(40)),
            mtime: readI53FromI64((statBuf)+(56)),
            ctime: readI53FromI64((statBuf)+(72)),
            ino: readI53FromU64((statBuf)+(88))
        }
      },
  stat(path) {
        var statBuf = _malloc(96);
        FS.handleError(withStackSave(() =>
          __wasmfs_stat(stringToUTF8OnStack(path), statBuf)
        ));
        var stats = FS.statBufToObject(statBuf);
        _free(statBuf);
  
        return stats;
      },
  lstat(path) {
        var statBuf = _malloc(96);
        FS.handleError(withStackSave(() =>
          __wasmfs_lstat(stringToUTF8OnStack(path), statBuf)
        ));
        var stats = FS.statBufToObject(statBuf);
        _free(statBuf);
  
        return stats;
      },
  chmod(path, mode) {
        return FS.handleError(withStackSave(() => {
          var buffer = stringToUTF8OnStack(path);
          return __wasmfs_chmod(buffer, mode);
        }));
      },
  lchmod(path, mode) {
        return FS.handleError(withStackSave(() => {
          var buffer = stringToUTF8OnStack(path);
          return __wasmfs_lchmod(buffer, mode);
        }));
      },
  fchmod(fd, mode) {
        return FS.handleError(__wasmfs_fchmod(fd, mode));
      },
  utime:(path, atime, mtime) => (
        FS.handleError(withStackSave(() => (
          __wasmfs_utime(stringToUTF8OnStack(path), atime, mtime)
        )))
      ),
  truncate(path, len) {
        return FS.handleError(withStackSave(() => (__wasmfs_truncate(stringToUTF8OnStack(path), BigInt(len)))));
      },
  ftruncate(fd, len) {
        return FS.handleError(__wasmfs_ftruncate(fd, BigInt(len)));
      },
  findObject(path) {
        var result = withStackSave(() => __wasmfs_identify(stringToUTF8OnStack(path)));
        if (result == 44) {
          return null;
        }
        return {
          isFolder: result == 31,
          isDevice: false, // TODO: wasmfs support for devices
        };
      },
  readdir:(path) => withStackSave(() => {
        var pathBuffer = stringToUTF8OnStack(path);
        var entries = [];
        var state = __wasmfs_readdir_start(pathBuffer);
        if (!state) {
          // TODO: The old FS threw an ErrnoError here.
          throw new Error("No such directory");
        }
        var entry;
        while (entry = __wasmfs_readdir_get(state)) {
          entries.push(UTF8ToString(entry));
        }
        __wasmfs_readdir_finish(state);
        return entries;
      }),
  mount:(type, opts, mountpoint) => {
        if (typeof type == 'string') {
          // The filesystem was not included, and instead we have an error
          // message stored in the variable.
          throw type;
        }
        var backendPointer = type.createBackend(opts);
        return FS.handleError(withStackSave(() => __wasmfs_mount(stringToUTF8OnStack(mountpoint), backendPointer)));
      },
  unmount:(mountpoint) => (
        FS.handleError(withStackSave(() => __wasmfs_unmount(stringToUTF8OnStack(mountpoint))))
      ),
  mknod:(path, mode, dev) => FS_mknod(path, mode, dev),
  makedev:(ma, mi) => ((ma) << 8 | (mi)),
  registerDevice(dev, ops) {
        var backendPointer = _wasmfs_create_jsimpl_backend();
        var definedOps = {
          userRead: ops.read,
          userWrite: ops.write,
  
          allocFile: (file) => {
            wasmFSDeviceStreams[file] = {}
          },
          freeFile: (file) => {
            wasmFSDeviceStreams[file] = undefined;
          },
          getSize: (file) => {},
          // Devices cannot be resized.
          setSize: (file, size) => 0,
          read: (file, buffer, length, offset) => {
            var bufferArray = Module.HEAP8.subarray(buffer, buffer + length);
            try {
              var bytesRead = definedOps.userRead(wasmFSDeviceStreams[file], bufferArray, 0, length, offset);
            } catch (e) {
              return -e.errno;
            }
            Module.HEAP8.set(bufferArray, buffer);
            return bytesRead;
          },
          write: (file, buffer, length, offset) => {
            var bufferArray = Module.HEAP8.subarray(buffer, buffer + length);
            try {
              var bytesWritten = definedOps.userWrite(wasmFSDeviceStreams[file], bufferArray, 0, length, offset);
            } catch (e) {
              return -e.errno;
            }
            Module.HEAP8.set(bufferArray, buffer);
            return bytesWritten;
          },
        };
  
        wasmFS$backends[backendPointer] = definedOps;
        wasmFSDevices[dev] = backendPointer;
      },
  createDevice(parent, name, input, output) {
        if (typeof parent != 'string') {
          // The old API allowed parents to be objects, which do not exist in WasmFS.
          throw new Error("Only string paths are accepted");
        }
        var path = PATH.join2(parent, name);
        var mode = FS_getMode(!!input, !!output);
        FS.createDevice.major ??= 64;
        var dev = FS.makedev(FS.createDevice.major++, 0);
        // Create a fake device with a set of stream ops to emulate
        // the old API's createDevice().
        FS.registerDevice(dev, {
          read(stream, buffer, offset, length, pos /* ignored */) {
            var bytesRead = 0;
            for (var i = 0; i < length; i++) {
              var result;
              try {
                result = input();
              } catch (e) {
                throw new FS.ErrnoError(29);
              }
              if (result === undefined && bytesRead === 0) {
                throw new FS.ErrnoError(6);
              }
              if (result === null || result === undefined) break;
              bytesRead++;
              buffer[offset+i] = result;
            }
            return bytesRead;
          },
          write(stream, buffer, offset, length, pos) {
            for (var i = 0; i < length; i++) {
              try {
                output(buffer[offset+i]);
              } catch (e) {
                throw new FS.ErrnoError(29);
              }
            }
            return i;
          }
        });
        return FS.mkdev(path, mode, dev);
      },
  mkdev(path, mode, dev) {
        if (typeof dev === 'undefined') {
          dev = mode;
          mode = 0o666;
        }
  
        var deviceBackend = wasmFSDevices[dev];
        if (!deviceBackend) {
          throw new Error("Invalid device ID.");
        }
  
        return FS.handleError(withStackSave(() => (
          _wasmfs_create_file(stringToUTF8OnStack(path), mode, deviceBackend)
        )));
      },
  rename(oldPath, newPath) {
        return FS.handleError(withStackSave(() => {
          var oldPathBuffer = stringToUTF8OnStack(oldPath);
          var newPathBuffer = stringToUTF8OnStack(newPath);
          return __wasmfs_rename(oldPathBuffer, newPathBuffer);
        }));
      },
  llseek(stream, offset, whence) {
        return FS.handleError(__wasmfs_llseek(stream.fd, BigInt(offset), whence));
      },
  };

  var getCFunc = (ident) => {
      var func = Module['_' + ident]; // closure exported function
      assert(func, 'Cannot call unknown function ' + ident + ', make sure it is exported');
      return func;
    };
  
  var writeArrayToMemory = (array, buffer) => {
      assert(array.length >= 0, 'writeArrayToMemory array must have a length (should be an array or typed array)')
      HEAP8.set(array, buffer);
    };
  
  
  
  
  
  
    /**
     * @param {string|null=} returnType
     * @param {Array=} argTypes
     * @param {Arguments|Array=} args
     * @param {Object=} opts
     */
  var ccall = (ident, returnType, argTypes, args, opts) => {
      // For fast lookup of conversion functions
      var toC = {
        'string': (str) => {
          var ret = 0;
          if (str !== null && str !== undefined && str !== 0) { // null string
            ret = stringToUTF8OnStack(str);
          }
          return ret;
        },
        'array': (arr) => {
          var ret = stackAlloc(arr.length);
          writeArrayToMemory(arr, ret);
          return ret;
        }
      };
  
      function convertReturnValue(ret) {
        if (returnType === 'string') {
          return UTF8ToString(ret);
        }
        if (returnType === 'boolean') return Boolean(ret);
        return ret;
      }
  
      var func = getCFunc(ident);
      var cArgs = [];
      var stack = 0;
      assert(returnType !== 'array', 'Return type should not be "array".');
      if (args) {
        for (var i = 0; i < args.length; i++) {
          var converter = toC[argTypes[i]];
          if (converter) {
            if (stack === 0) stack = stackSave();
            cArgs[i] = converter(args[i]);
          } else {
            cArgs[i] = args[i];
          }
        }
      }
      var ret = func(...cArgs);
      function onDone(ret) {
        if (stack !== 0) stackRestore(stack);
        return convertReturnValue(ret);
      }
  
      ret = onDone(ret);
      return ret;
    };

  
  
    /**
     * @param {string=} returnType
     * @param {Array=} argTypes
     * @param {Object=} opts
     */
  var cwrap = (ident, returnType, argTypes, opts) => {
      return (...args) => ccall(ident, returnType, argTypes, args, opts);
    };

  var uleb128Encode = (n, target) => {
      assert(n < 16384);
      if (n < 128) {
        target.push(n);
      } else {
        target.push((n % 128) | 128, n >> 7);
      }
    };
  
  var sigToWasmTypes = (sig) => {
      var typeNames = {
        'i': 'i32',
        'j': 'i64',
        'f': 'f32',
        'd': 'f64',
        'e': 'externref',
        'p': 'i32',
      };
      var type = {
        parameters: [],
        results: sig[0] == 'v' ? [] : [typeNames[sig[0]]]
      };
      for (var i = 1; i < sig.length; ++i) {
        assert(sig[i] in typeNames, 'invalid signature char: ' + sig[i]);
        type.parameters.push(typeNames[sig[i]]);
      }
      return type;
    };
  
  var generateFuncType = (sig, target) => {
      var sigRet = sig.slice(0, 1);
      var sigParam = sig.slice(1);
      var typeCodes = {
        'i': 0x7f, // i32
        'p': 0x7f, // i32
        'j': 0x7e, // i64
        'f': 0x7d, // f32
        'd': 0x7c, // f64
        'e': 0x6f, // externref
      };
  
      // Parameters, length + signatures
      target.push(0x60 /* form: func */);
      uleb128Encode(sigParam.length, target);
      for (var i = 0; i < sigParam.length; ++i) {
        assert(sigParam[i] in typeCodes, 'invalid signature char: ' + sigParam[i]);
        target.push(typeCodes[sigParam[i]]);
      }
  
      // Return values, length + signatures
      // With no multi-return in MVP, either 0 (void) or 1 (anything else)
      if (sigRet == 'v') {
        target.push(0x00);
      } else {
        target.push(0x01, typeCodes[sigRet]);
      }
    };
  var convertJsFunctionToWasm = (func, sig) => {
  
      // If the type reflection proposal is available, use the new
      // "WebAssembly.Function" constructor.
      // Otherwise, construct a minimal wasm module importing the JS function and
      // re-exporting it.
      if (typeof WebAssembly.Function == "function") {
        return new WebAssembly.Function(sigToWasmTypes(sig), func);
      }
  
      // The module is static, with the exception of the type section, which is
      // generated based on the signature passed in.
      var typeSectionBody = [
        0x01, // count: 1
      ];
      generateFuncType(sig, typeSectionBody);
  
      // Rest of the module is static
      var bytes = [
        0x00, 0x61, 0x73, 0x6d, // magic ("\0asm")
        0x01, 0x00, 0x00, 0x00, // version: 1
        0x01, // Type section code
      ];
      // Write the overall length of the type section followed by the body
      uleb128Encode(typeSectionBody.length, bytes);
      bytes.push(...typeSectionBody);
  
      // The rest of the module is static
      bytes.push(
        0x02, 0x07, // import section
          // (import "e" "f" (func 0 (type 0)))
          0x01, 0x01, 0x65, 0x01, 0x66, 0x00, 0x00,
        0x07, 0x05, // export section
          // (export "f" (func 0 (type 0)))
          0x01, 0x01, 0x66, 0x00, 0x00,
      );
  
      // We can compile this wasm module synchronously because it is very small.
      // This accepts an import (at "e.f"), that it reroutes to an export (at "f")
      var module = new WebAssembly.Module(new Uint8Array(bytes));
      var instance = new WebAssembly.Instance(module, { 'e': { 'f': func } });
      var wrappedFunc = instance.exports['f'];
      return wrappedFunc;
    };
  
  
  var updateTableMap = (offset, count) => {
      if (functionsInTableMap) {
        for (var i = offset; i < offset + count; i++) {
          var item = getWasmTableEntry(i);
          // Ignore null values.
          if (item) {
            functionsInTableMap.set(item, i);
          }
        }
      }
    };
  
  var functionsInTableMap;
  
  var getFunctionAddress = (func) => {
      // First, create the map if this is the first use.
      if (!functionsInTableMap) {
        functionsInTableMap = new WeakMap();
        updateTableMap(0, wasmTable.length);
      }
      return functionsInTableMap.get(func) || 0;
    };
  
  
  var freeTableIndexes = [];
  
  var getEmptyTableSlot = () => {
      // Reuse a free index if there is one, otherwise grow.
      if (freeTableIndexes.length) {
        return freeTableIndexes.pop();
      }
      // Grow the table
      try {
        /** @suppress {checkTypes} */
        wasmTable.grow(1);
      } catch (err) {
        if (!(err instanceof RangeError)) {
          throw err;
        }
        throw 'Unable to grow wasm table. Set ALLOW_TABLE_GROWTH.';
      }
      return wasmTable.length - 1;
    };
  
  
  
  var setWasmTableEntry = (idx, func) => {
      /** @suppress {checkTypes} */
      wasmTable.set(idx, func);
      // With ABORT_ON_WASM_EXCEPTIONS wasmTable.get is overridden to return wrapped
      // functions so we need to call it here to retrieve the potential wrapper correctly
      // instead of just storing 'func' directly into wasmTableMirror
      /** @suppress {checkTypes} */
      wasmTableMirror[idx] = wasmTable.get(idx);
    };
  
  /** @param {string=} sig */
  var addFunction = (func, sig) => {
      assert(typeof func != 'undefined');
      // Check if the function is already in the table, to ensure each function
      // gets a unique index.
      var rtn = getFunctionAddress(func);
      if (rtn) {
        return rtn;
      }
  
      // It's not in the table, add it now.
  
      var ret = getEmptyTableSlot();
  
      // Set the new value.
      try {
        // Attempting to call this with JS function will cause of table.set() to fail
        setWasmTableEntry(ret, func);
      } catch (err) {
        if (!(err instanceof TypeError)) {
          throw err;
        }
        assert(typeof sig != 'undefined', 'Missing signature argument to addFunction: ' + func);
        var wrapped = convertJsFunctionToWasm(func, sig);
        setWasmTableEntry(ret, wrapped);
      }
  
      functionsInTableMap.set(func, ret);
  
      return ret;
    };

  
  
  
  
  var removeFunction = (index) => {
      functionsInTableMap.delete(getWasmTableEntry(index));
      setWasmTableEntry(index, null);
      freeTableIndexes.push(index);
    };






  var FS_createPath = FS.createPath;




  FS.init();
  ;
function checkIncomingModuleAPI() {
  ignoredModuleProp('fetchSettings');
}
var wasmImports = {
  /** @export */
  __assert_fail: ___assert_fail,
  /** @export */
  __call_sighandler: ___call_sighandler,
  /** @export */
  __interpreter_intrinsic_notco_call_outlined: ___interpreter_intrinsic_notco_call_outlined,
  /** @export */
  _abort_js: __abort_js,
  /** @export */
  _emscripten_get_progname: __emscripten_get_progname,
  /** @export */
  _emscripten_runtime_keepalive_clear: __emscripten_runtime_keepalive_clear,
  /** @export */
  _emscripten_throw_longjmp: __emscripten_throw_longjmp,
  /** @export */
  _setitimer_js: __setitimer_js,
  /** @export */
  _tzset_js: __tzset_js,
  /** @export */
  _wasmfs_copy_preloaded_file_data: __wasmfs_copy_preloaded_file_data,
  /** @export */
  _wasmfs_get_num_preloaded_dirs: __wasmfs_get_num_preloaded_dirs,
  /** @export */
  _wasmfs_get_num_preloaded_files: __wasmfs_get_num_preloaded_files,
  /** @export */
  _wasmfs_get_preloaded_child_path: __wasmfs_get_preloaded_child_path,
  /** @export */
  _wasmfs_get_preloaded_file_mode: __wasmfs_get_preloaded_file_mode,
  /** @export */
  _wasmfs_get_preloaded_file_size: __wasmfs_get_preloaded_file_size,
  /** @export */
  _wasmfs_get_preloaded_parent_path: __wasmfs_get_preloaded_parent_path,
  /** @export */
  _wasmfs_get_preloaded_path_name: __wasmfs_get_preloaded_path_name,
  /** @export */
  _wasmfs_jsimpl_alloc_file: __wasmfs_jsimpl_alloc_file,
  /** @export */
  _wasmfs_jsimpl_free_file: __wasmfs_jsimpl_free_file,
  /** @export */
  _wasmfs_jsimpl_get_size: __wasmfs_jsimpl_get_size,
  /** @export */
  _wasmfs_jsimpl_read: __wasmfs_jsimpl_read,
  /** @export */
  _wasmfs_jsimpl_set_size: __wasmfs_jsimpl_set_size,
  /** @export */
  _wasmfs_jsimpl_write: __wasmfs_jsimpl_write,
  /** @export */
  _wasmfs_stdin_get_char: __wasmfs_stdin_get_char,
  /** @export */
  emscripten_asm_const_int: _emscripten_asm_const_int,
  /** @export */
  emscripten_asm_const_int_sync_on_main_thread: _emscripten_asm_const_int_sync_on_main_thread,
  /** @export */
  emscripten_asm_const_ptr: _emscripten_asm_const_ptr,
  /** @export */
  emscripten_date_now: _emscripten_date_now,
  /** @export */
  emscripten_err: _emscripten_err,
  /** @export */
  emscripten_get_now: _emscripten_get_now,
  /** @export */
  emscripten_out: _emscripten_out,
  /** @export */
  emscripten_resize_heap: _emscripten_resize_heap,
  /** @export */
  environ_get: _environ_get,
  /** @export */
  environ_sizes_get: _environ_sizes_get,
  /** @export */
  exit: _exit,
  /** @export */
  invoke_ii,
  /** @export */
  invoke_iii,
  /** @export */
  invoke_iiii,
  /** @export */
  invoke_iiiii,
  /** @export */
  invoke_v,
  /** @export */
  invoke_vi,
  /** @export */
  invoke_viii,
  /** @export */
  proc_exit: _proc_exit,
  /** @export */
  random_get: _random_get
};
var wasmExports = await createWasm();
var ___wasm_call_ctors = createExportWrapper('__wasm_call_ctors', 0);
var _ffi_create_vm = Module['_ffi_create_vm'] = createExportWrapper('ffi_create_vm', 4);
var _ffi_create_thread = Module['_ffi_create_thread'] = createExportWrapper('ffi_create_thread', 1);
var _ffi_get_class = Module['_ffi_get_class'] = createExportWrapper('ffi_get_class', 2);
var _ffi_get_current_exception = Module['_ffi_get_current_exception'] = createExportWrapper('ffi_get_current_exception', 1);
var _ffi_clear_current_exception = Module['_ffi_clear_current_exception'] = createExportWrapper('ffi_clear_current_exception', 1);
var _ffi_get_classdesc = Module['_ffi_get_classdesc'] = createExportWrapper('ffi_get_classdesc', 1);
var _ffi_create_rr_scheduler = Module['_ffi_create_rr_scheduler'] = createExportWrapper('ffi_create_rr_scheduler', 1);
var _malloc = Module['_malloc'] = createExportWrapper('malloc', 1);
var _ffi_rr_scheduler_wait_for_us = Module['_ffi_rr_scheduler_wait_for_us'] = createExportWrapper('ffi_rr_scheduler_wait_for_us', 1);
var _ffi_rr_scheduler_step = Module['_ffi_rr_scheduler_step'] = createExportWrapper('ffi_rr_scheduler_step', 1);
var _ffi_rr_record_is_ready = Module['_ffi_rr_record_is_ready'] = createExportWrapper('ffi_rr_record_is_ready', 1);
var _ffi_rr_schedule = Module['_ffi_rr_schedule'] = createExportWrapper('ffi_rr_schedule', 3);
var _ffi_get_execution_record_result_pointer = Module['_ffi_get_execution_record_result_pointer'] = createExportWrapper('ffi_get_execution_record_result_pointer', 1);
var _deref_js_handle = Module['_deref_js_handle'] = createExportWrapper('deref_js_handle', 2);
var _ffi_get_execution_record_js_handle = Module['_ffi_get_execution_record_js_handle'] = createExportWrapper('ffi_get_execution_record_js_handle', 1);
var _ffi_execute_immediately = Module['_ffi_execute_immediately'] = createExportWrapper('ffi_execute_immediately', 1);
var _ffi_free_execution_record = Module['_ffi_free_execution_record'] = createExportWrapper('ffi_free_execution_record', 1);
var _ffi_async_run = Module['_ffi_async_run'] = createExportWrapper('ffi_async_run', 3);
var _ffi_allocate_object = Module['_ffi_allocate_object'] = createExportWrapper('ffi_allocate_object', 2);
var _ffi_create_string = Module['_ffi_create_string'] = createExportWrapper('ffi_create_string', 3);
var _ffi_is_string = Module['_ffi_is_string'] = createExportWrapper('ffi_is_string', 1);
var _ffi_get_string_data = Module['_ffi_get_string_data'] = createExportWrapper('ffi_get_string_data', 1);
var _ffi_get_string_len = Module['_ffi_get_string_len'] = createExportWrapper('ffi_get_string_len', 1);
var _ffi_get_string_coder = Module['_ffi_get_string_coder'] = createExportWrapper('ffi_get_string_coder', 1);
var _ffi_instanceof = Module['_ffi_instanceof'] = createExportWrapper('ffi_instanceof', 2);
var _ffi_run_step = Module['_ffi_run_step'] = createExportWrapper('ffi_run_step', 2);
var _ffi_free_async_run_ctx = Module['_ffi_free_async_run_ctx'] = createExportWrapper('ffi_free_async_run_ctx', 1);
var _free = Module['_free'] = createExportWrapper('free', 1);
var _ffi_get_class_json = Module['_ffi_get_class_json'] = createExportWrapper('ffi_get_class_json', 1);
var _main = Module['_main'] = createExportWrapper('main', 2);
var _make_js_handle = Module['_make_js_handle'] = createExportWrapper('make_js_handle', 2);
var _drop_js_handle = Module['_drop_js_handle'] = createExportWrapper('drop_js_handle', 2);
var _set_max_calls = Module['_set_max_calls'] = createExportWrapper('set_max_calls', 1);
var ___interpreter_intrinsic_void_table_base = Module['___interpreter_intrinsic_void_table_base'] = createExportWrapper('__interpreter_intrinsic_void_table_base', 0);
var ___interpreter_intrinsic_int_table_base = Module['___interpreter_intrinsic_int_table_base'] = createExportWrapper('__interpreter_intrinsic_int_table_base', 0);
var ___interpreter_intrinsic_float_table_base = Module['___interpreter_intrinsic_float_table_base'] = createExportWrapper('__interpreter_intrinsic_float_table_base', 0);
var ___interpreter_intrinsic_double_table_base = Module['___interpreter_intrinsic_double_table_base'] = createExportWrapper('__interpreter_intrinsic_double_table_base', 0);
var ___interpreter_intrinsic_max_insn = Module['___interpreter_intrinsic_max_insn'] = createExportWrapper('__interpreter_intrinsic_max_insn', 0);
var _nop_impl_void = Module['_nop_impl_void'] = createExportWrapper('nop_impl_void', 8);
var _wasm_push_export = Module['_wasm_push_export'] = createExportWrapper('wasm_push_export', 3);
var _FileDescriptor_initIDs_cb0 = Module['_FileDescriptor_initIDs_cb0'] = createExportWrapper('FileDescriptor_initIDs_cb0', 5);
var _FileDescriptor_set_cb0 = Module['_FileDescriptor_set_cb0'] = createExportWrapper('FileDescriptor_set_cb0', 5);
var _FileDescriptor_getHandle_cb0 = Module['_FileDescriptor_getHandle_cb0'] = createExportWrapper('FileDescriptor_getHandle_cb0', 5);
var _FileDescriptor_getAppend_cb0 = Module['_FileDescriptor_getAppend_cb0'] = createExportWrapper('FileDescriptor_getAppend_cb0', 5);
var _FileDescriptor_close0_cb0 = Module['_FileDescriptor_close0_cb0'] = createExportWrapper('FileDescriptor_close0_cb0', 5);
var _FileInputStream_initIDs_cb0 = Module['_FileInputStream_initIDs_cb0'] = createExportWrapper('FileInputStream_initIDs_cb0', 5);
var _FileInputStream_open0_cb0 = Module['_FileInputStream_open0_cb0'] = createExportWrapper('FileInputStream_open0_cb0', 5);
var _FileInputStream_read0_cb0 = Module['_FileInputStream_read0_cb0'] = createExportWrapper('FileInputStream_read0_cb0', 5);
var _FileInputStream_readBytes_cb0 = Module['_FileInputStream_readBytes_cb0'] = createExportWrapper('FileInputStream_readBytes_cb0', 5);
var _FileInputStream_close0_cb0 = Module['_FileInputStream_close0_cb0'] = createExportWrapper('FileInputStream_close0_cb0', 5);
var _FileInputStream_available0_cb0 = Module['_FileInputStream_available0_cb0'] = createExportWrapper('FileInputStream_available0_cb0', 5);
var _FileOutputStream_initIDs_cb0 = Module['_FileOutputStream_initIDs_cb0'] = createExportWrapper('FileOutputStream_initIDs_cb0', 5);
var _FileOutputStream_writeBytes_cb0 = Module['_FileOutputStream_writeBytes_cb0'] = createExportWrapper('FileOutputStream_writeBytes_cb0', 5);
var _FileOutputStream_close0_cb0 = Module['_FileOutputStream_close0_cb0'] = createExportWrapper('FileOutputStream_close0_cb0', 5);
var _RandomAccessFile_initIDs_cb0 = Module['_RandomAccessFile_initIDs_cb0'] = createExportWrapper('RandomAccessFile_initIDs_cb0', 5);
var _RandomAccessFile_open0_cb0 = Module['_RandomAccessFile_open0_cb0'] = createExportWrapper('RandomAccessFile_open0_cb0', 5);
var _RandomAccessFile_read0_cb0 = Module['_RandomAccessFile_read0_cb0'] = createExportWrapper('RandomAccessFile_read0_cb0', 5);
var _RandomAccessFile_seek0_cb0 = Module['_RandomAccessFile_seek0_cb0'] = createExportWrapper('RandomAccessFile_seek0_cb0', 5);
var _RandomAccessFile_getFilePointer_cb0 = Module['_RandomAccessFile_getFilePointer_cb0'] = createExportWrapper('RandomAccessFile_getFilePointer_cb0', 5);
var _RandomAccessFile_close0_cb0 = Module['_RandomAccessFile_close0_cb0'] = createExportWrapper('RandomAccessFile_close0_cb0', 5);
var _RandomAccessFile_length0_cb0 = Module['_RandomAccessFile_length0_cb0'] = createExportWrapper('RandomAccessFile_length0_cb0', 5);
var _RandomAccessFile_readBytes0_cb0 = Module['_RandomAccessFile_readBytes0_cb0'] = createExportWrapper('RandomAccessFile_readBytes0_cb0', 5);
var _UnixFileSystem_initIDs_cb0 = Module['_UnixFileSystem_initIDs_cb0'] = createExportWrapper('UnixFileSystem_initIDs_cb0', 5);
var _UnixFileSystem_checkAccess0_cb0 = Module['_UnixFileSystem_checkAccess0_cb0'] = createExportWrapper('UnixFileSystem_checkAccess0_cb0', 5);
var _UnixFileSystem_canonicalize0_cb0 = Module['_UnixFileSystem_canonicalize0_cb0'] = createExportWrapper('UnixFileSystem_canonicalize0_cb0', 5);
var _UnixFileSystem_getLastModifiedTime_cb0 = Module['_UnixFileSystem_getLastModifiedTime_cb0'] = createExportWrapper('UnixFileSystem_getLastModifiedTime_cb0', 5);
var _Class_registerNatives_cb0 = Module['_Class_registerNatives_cb0'] = createExportWrapper('Class_registerNatives_cb0', 5);
var _Class_getPrimitiveClass_cb0 = Module['_Class_getPrimitiveClass_cb0'] = createExportWrapper('Class_getPrimitiveClass_cb0', 5);
var _Class_getEnclosingMethod0_cb0 = Module['_Class_getEnclosingMethod0_cb0'] = createExportWrapper('Class_getEnclosingMethod0_cb0', 5);
var _Class_getDeclaringClass0_cb0 = Module['_Class_getDeclaringClass0_cb0'] = createExportWrapper('Class_getDeclaringClass0_cb0', 5);
var _Class_getComponentType_cb0 = Module['_Class_getComponentType_cb0'] = createExportWrapper('Class_getComponentType_cb0', 5);
var _Class_getModifiers_cb0 = Module['_Class_getModifiers_cb0'] = createExportWrapper('Class_getModifiers_cb0', 5);
var _Class_getSuperclass_cb0 = Module['_Class_getSuperclass_cb0'] = createExportWrapper('Class_getSuperclass_cb0', 5);
var _Class_getClassLoader_cb0 = Module['_Class_getClassLoader_cb0'] = createExportWrapper('Class_getClassLoader_cb0', 5);
var _Class_getPermittedSubclasses0_cb0 = Module['_Class_getPermittedSubclasses0_cb0'] = createExportWrapper('Class_getPermittedSubclasses0_cb0', 5);
var _Class_initClassName_cb0 = Module['_Class_initClassName_cb0'] = createExportWrapper('Class_initClassName_cb0', 5);
var _Class_forName0_cb0 = Module['_Class_forName0_cb0'] = createExportWrapper('Class_forName0_cb0', 5);
var _Class_desiredAssertionStatus0_cb0 = Module['_Class_desiredAssertionStatus0_cb0'] = createExportWrapper('Class_desiredAssertionStatus0_cb0', 5);
var _Class_getDeclaredFields0_cb0 = Module['_Class_getDeclaredFields0_cb0'] = createExportWrapper('Class_getDeclaredFields0_cb0', 5);
var _Class_getDeclaredConstructors0_cb0 = Module['_Class_getDeclaredConstructors0_cb0'] = createExportWrapper('Class_getDeclaredConstructors0_cb0', 5);
var _Class_getDeclaredMethods0_cb0 = Module['_Class_getDeclaredMethods0_cb0'] = createExportWrapper('Class_getDeclaredMethods0_cb0', 5);
var _Class_getDeclaredClasses0_cb0 = Module['_Class_getDeclaredClasses0_cb0'] = createExportWrapper('Class_getDeclaredClasses0_cb0', 5);
var _Class_isPrimitive_cb0 = Module['_Class_isPrimitive_cb0'] = createExportWrapper('Class_isPrimitive_cb0', 5);
var _Class_isInterface_cb0 = Module['_Class_isInterface_cb0'] = createExportWrapper('Class_isInterface_cb0', 5);
var _Class_isAssignableFrom_cb0 = Module['_Class_isAssignableFrom_cb0'] = createExportWrapper('Class_isAssignableFrom_cb0', 5);
var _Class_isInstance_cb0 = Module['_Class_isInstance_cb0'] = createExportWrapper('Class_isInstance_cb0', 5);
var _Class_isArray_cb0 = Module['_Class_isArray_cb0'] = createExportWrapper('Class_isArray_cb0', 5);
var _Class_isHidden_cb0 = Module['_Class_isHidden_cb0'] = createExportWrapper('Class_isHidden_cb0', 5);
var _Class_getNestHost0_cb0 = Module['_Class_getNestHost0_cb0'] = createExportWrapper('Class_getNestHost0_cb0', 5);
var _Class_getConstantPool_cb0 = Module['_Class_getConstantPool_cb0'] = createExportWrapper('Class_getConstantPool_cb0', 5);
var _Class_getRawAnnotations_cb0 = Module['_Class_getRawAnnotations_cb0'] = createExportWrapper('Class_getRawAnnotations_cb0', 5);
var _Class_getRawTypeAnnotations_cb0 = Module['_Class_getRawTypeAnnotations_cb0'] = createExportWrapper('Class_getRawTypeAnnotations_cb0', 5);
var _Class_getInterfaces0_cb0 = Module['_Class_getInterfaces0_cb0'] = createExportWrapper('Class_getInterfaces0_cb0', 5);
var _Class_getGenericSignature0_cb0 = Module['_Class_getGenericSignature0_cb0'] = createExportWrapper('Class_getGenericSignature0_cb0', 5);
var _Class_getProtectionDomain0_cb0 = Module['_Class_getProtectionDomain0_cb0'] = createExportWrapper('Class_getProtectionDomain0_cb0', 5);
var _ClassLoader_registerNatives_cb0 = Module['_ClassLoader_registerNatives_cb0'] = createExportWrapper('ClassLoader_registerNatives_cb0', 5);
var _ClassLoader_findLoadedClass0_cb0 = Module['_ClassLoader_findLoadedClass0_cb0'] = createExportWrapper('ClassLoader_findLoadedClass0_cb0', 5);
var _ClassLoader_findBootstrapClass_cb0 = Module['_ClassLoader_findBootstrapClass_cb0'] = createExportWrapper('ClassLoader_findBootstrapClass_cb0', 5);
var _ClassLoader_findBuiltinLib_cb0 = Module['_ClassLoader_findBuiltinLib_cb0'] = createExportWrapper('ClassLoader_findBuiltinLib_cb0', 5);
var _ClassLoader_defineClass2_cb0 = Module['_ClassLoader_defineClass2_cb0'] = createExportWrapper('ClassLoader_defineClass2_cb0', 5);
var _ClassLoader_defineClass1_cb0 = Module['_ClassLoader_defineClass1_cb0'] = createExportWrapper('ClassLoader_defineClass1_cb0', 5);
var _ClassLoader_defineClass0_cb0 = Module['_ClassLoader_defineClass0_cb0'] = createExportWrapper('ClassLoader_defineClass0_cb0', 5);
var _Double_doubleToRawLongBits_cb0 = Module['_Double_doubleToRawLongBits_cb0'] = createExportWrapper('Double_doubleToRawLongBits_cb0', 5);
var _Double_longBitsToDouble_cb0 = Module['_Double_longBitsToDouble_cb0'] = createExportWrapper('Double_longBitsToDouble_cb0', 5);
var _Float_floatToRawIntBits_cb0 = Module['_Float_floatToRawIntBits_cb0'] = createExportWrapper('Float_floatToRawIntBits_cb0', 5);
var _Float_intBitsToFloat_cb0 = Module['_Float_intBitsToFloat_cb0'] = createExportWrapper('Float_intBitsToFloat_cb0', 5);
var _Module_defineModule0_cb0 = Module['_Module_defineModule0_cb0'] = createExportWrapper('Module_defineModule0_cb0', 5);
var _Module_addReads0_cb0 = Module['_Module_addReads0_cb0'] = createExportWrapper('Module_addReads0_cb0', 5);
var _Module_addExportsToAll0_cb0 = Module['_Module_addExportsToAll0_cb0'] = createExportWrapper('Module_addExportsToAll0_cb0', 5);
var _Module_addExports0_cb0 = Module['_Module_addExports0_cb0'] = createExportWrapper('Module_addExports0_cb0', 5);
var _NullPointerException_getExtendedNPEMessage_cb0 = Module['_NullPointerException_getExtendedNPEMessage_cb0'] = createExportWrapper('NullPointerException_getExtendedNPEMessage_cb0', 5);
var _Object_hashCode_cb0 = Module['_Object_hashCode_cb0'] = createExportWrapper('Object_hashCode_cb0', 5);
var _Object_registerNatives_cb0 = Module['_Object_registerNatives_cb0'] = createExportWrapper('Object_registerNatives_cb0', 5);
var _Object_clone_cb0 = Module['_Object_clone_cb0'] = createExportWrapper('Object_clone_cb0', 5);
var _Object_getClass_cb0 = Module['_Object_getClass_cb0'] = createExportWrapper('Object_getClass_cb0', 5);
var _Object_notifyAll_cb0 = Module['_Object_notifyAll_cb0'] = createExportWrapper('Object_notifyAll_cb0', 5);
var _Object_notify_cb0 = Module['_Object_notify_cb0'] = createExportWrapper('Object_notify_cb0', 5);
var _Object_wait_cb0 = Module['_Object_wait_cb0'] = createExportWrapper('Object_wait_cb0', 5);
var _Runtime_availableProcessors_cb0 = Module['_Runtime_availableProcessors_cb0'] = createExportWrapper('Runtime_availableProcessors_cb0', 5);
var _Runtime_maxMemory_cb0 = Module['_Runtime_maxMemory_cb0'] = createExportWrapper('Runtime_maxMemory_cb0', 5);
var _String_intern_cb0 = Module['_String_intern_cb0'] = createExportWrapper('String_intern_cb0', 5);
var _StringUTF16_isBigEndian_cb0 = Module['_StringUTF16_isBigEndian_cb0'] = createExportWrapper('StringUTF16_isBigEndian_cb0', 5);
var _System_mapLibraryName_cb0 = Module['_System_mapLibraryName_cb0'] = createExportWrapper('System_mapLibraryName_cb0', 5);
var _System_arraycopy_cb0 = Module['_System_arraycopy_cb0'] = createExportWrapper('System_arraycopy_cb0', 5);
var _System_registerNatives_cb0 = Module['_System_registerNatives_cb0'] = createExportWrapper('System_registerNatives_cb0', 5);
var _System_setOut0_cb0 = Module['_System_setOut0_cb0'] = createExportWrapper('System_setOut0_cb0', 5);
var _System_setIn0_cb0 = Module['_System_setIn0_cb0'] = createExportWrapper('System_setIn0_cb0', 5);
var _System_setErr0_cb0 = Module['_System_setErr0_cb0'] = createExportWrapper('System_setErr0_cb0', 5);
var _System_identityHashCode_cb0 = Module['_System_identityHashCode_cb0'] = createExportWrapper('System_identityHashCode_cb0', 5);
var _System_currentTimeMillis_cb0 = Module['_System_currentTimeMillis_cb0'] = createExportWrapper('System_currentTimeMillis_cb0', 5);
var _System_nanoTime_cb0 = Module['_System_nanoTime_cb0'] = createExportWrapper('System_nanoTime_cb0', 5);
var _Thread_registerNatives_cb0 = Module['_Thread_registerNatives_cb0'] = createExportWrapper('Thread_registerNatives_cb0', 5);
var _Thread_currentThread_cb0 = Module['_Thread_currentThread_cb0'] = createExportWrapper('Thread_currentThread_cb0', 5);
var _Thread_setPriority0_cb0 = Module['_Thread_setPriority0_cb0'] = createExportWrapper('Thread_setPriority0_cb0', 5);
var _Thread_isAlive_cb0 = Module['_Thread_isAlive_cb0'] = createExportWrapper('Thread_isAlive_cb0', 5);
var _Thread_holdsLock_cb0 = Module['_Thread_holdsLock_cb0'] = createExportWrapper('Thread_holdsLock_cb0', 5);
var _Thread_start0_cb0 = Module['_Thread_start0_cb0'] = createExportWrapper('Thread_start0_cb0', 5);
var _Thread_ensureMaterializedForStackWalk_cb0 = Module['_Thread_ensureMaterializedForStackWalk_cb0'] = createExportWrapper('Thread_ensureMaterializedForStackWalk_cb0', 5);
var _Thread_getNextThreadIdOffset_cb0 = Module['_Thread_getNextThreadIdOffset_cb0'] = createExportWrapper('Thread_getNextThreadIdOffset_cb0', 5);
var _Thread_currentCarrierThread_cb0 = Module['_Thread_currentCarrierThread_cb0'] = createExportWrapper('Thread_currentCarrierThread_cb0', 5);
var _Thread_interrupt0_cb0 = Module['_Thread_interrupt0_cb0'] = createExportWrapper('Thread_interrupt0_cb0', 5);
var _Thread_clearInterruptEvent_cb0 = Module['_Thread_clearInterruptEvent_cb0'] = createExportWrapper('Thread_clearInterruptEvent_cb0', 5);
var _Throwable_fillInStackTrace_cb0 = Module['_Throwable_fillInStackTrace_cb0'] = createExportWrapper('Throwable_fillInStackTrace_cb0', 5);
var _Throwable_getStackTraceDepth_cb0 = Module['_Throwable_getStackTraceDepth_cb0'] = createExportWrapper('Throwable_getStackTraceDepth_cb0', 5);
var _Throwable_getStackTraceElement_cb0 = Module['_Throwable_getStackTraceElement_cb0'] = createExportWrapper('Throwable_getStackTraceElement_cb0', 5);
var _StackTraceElement_initStackTraceElements_cb0 = Module['_StackTraceElement_initStackTraceElements_cb0'] = createExportWrapper('StackTraceElement_initStackTraceElements_cb0', 5);
var _MethodHandleNatives_registerNatives_cb0 = Module['_MethodHandleNatives_registerNatives_cb0'] = createExportWrapper('MethodHandleNatives_registerNatives_cb0', 5);
var _MethodHandleNatives_getConstant_cb0 = Module['_MethodHandleNatives_getConstant_cb0'] = createExportWrapper('MethodHandleNatives_getConstant_cb0', 5);
var _MethodHandleNatives_getNamedCon_cb0 = Module['_MethodHandleNatives_getNamedCon_cb0'] = createExportWrapper('MethodHandleNatives_getNamedCon_cb0', 5);
var _MethodHandleNatives_resolve_cb0 = Module['_MethodHandleNatives_resolve_cb0'] = createExportWrapper('MethodHandleNatives_resolve_cb0', 5);
var _MethodHandleNatives_init_cb0 = Module['_MethodHandleNatives_init_cb0'] = createExportWrapper('MethodHandleNatives_init_cb0', 5);
var _MethodHandleNatives_objectFieldOffset_cb0 = Module['_MethodHandleNatives_objectFieldOffset_cb0'] = createExportWrapper('MethodHandleNatives_objectFieldOffset_cb0', 5);
var _MethodHandleNatives_staticFieldBase_cb0 = Module['_MethodHandleNatives_staticFieldBase_cb0'] = createExportWrapper('MethodHandleNatives_staticFieldBase_cb0', 5);
var _MethodHandleNatives_staticFieldOffset_cb0 = Module['_MethodHandleNatives_staticFieldOffset_cb0'] = createExportWrapper('MethodHandleNatives_staticFieldOffset_cb0', 5);
var _MethodHandleNatives_getMembers_cb0 = Module['_MethodHandleNatives_getMembers_cb0'] = createExportWrapper('MethodHandleNatives_getMembers_cb0', 5);
var _Finalizer_isFinalizationEnabled_cb0 = Module['_Finalizer_isFinalizationEnabled_cb0'] = createExportWrapper('Finalizer_isFinalizationEnabled_cb0', 5);
var _Reference_refersTo0_cb0 = Module['_Reference_refersTo0_cb0'] = createExportWrapper('Reference_refersTo0_cb0', 5);
var _Reference_clear0_cb0 = Module['_Reference_clear0_cb0'] = createExportWrapper('Reference_clear0_cb0', 5);
var _Reference_getAndClearReferencePendingList_cb0 = Module['_Reference_getAndClearReferencePendingList_cb0'] = createExportWrapper('Reference_getAndClearReferencePendingList_cb0', 5);
var _Array_newArray_cb0 = Module['_Array_newArray_cb0'] = createExportWrapper('Array_newArray_cb0', 5);
var _Array_getLength_cb0 = Module['_Array_getLength_cb0'] = createExportWrapper('Array_getLength_cb0', 5);
var _Executable_getParameters0_cb0 = Module['_Executable_getParameters0_cb0'] = createExportWrapper('Executable_getParameters0_cb0', 5);
var _Proxy_defineClass0_cb0 = Module['_Proxy_defineClass0_cb0'] = createExportWrapper('Proxy_defineClass0_cb0', 5);
var _AccessController_getStackAccessControlContext_cb0 = Module['_AccessController_getStackAccessControlContext_cb0'] = createExportWrapper('AccessController_getStackAccessControlContext_cb0', 5);
var _AccessController_ensureMaterializedForStackWalk_cb0 = Module['_AccessController_ensureMaterializedForStackWalk_cb0'] = createExportWrapper('AccessController_ensureMaterializedForStackWalk_cb0', 5);
var _TimeZone_getSystemTimeZoneID_cb0 = Module['_TimeZone_getSystemTimeZoneID_cb0'] = createExportWrapper('TimeZone_getSystemTimeZoneID_cb0', 5);
var _AtomicLong_VMSupportsCS8_cb0 = Module['_AtomicLong_VMSupportsCS8_cb0'] = createExportWrapper('AtomicLong_VMSupportsCS8_cb0', 5);
var _Inflater_initIDs_cb0 = Module['_Inflater_initIDs_cb0'] = createExportWrapper('Inflater_initIDs_cb0', 5);
var _Inflater_init_cb0 = Module['_Inflater_init_cb0'] = createExportWrapper('Inflater_init_cb0', 5);
var _Inflater_inflateBytesBytes_cb0 = Module['_Inflater_inflateBytesBytes_cb0'] = createExportWrapper('Inflater_inflateBytesBytes_cb0', 5);
var _Inflater_reset_cb0 = Module['_Inflater_reset_cb0'] = createExportWrapper('Inflater_reset_cb0', 5);
var _Inflater_end_cb0 = Module['_Inflater_end_cb0'] = createExportWrapper('Inflater_end_cb0', 5);
var _CRC32_updateBytes0_cb0 = Module['_CRC32_updateBytes0_cb0'] = createExportWrapper('CRC32_updateBytes0_cb0', 5);
var _ZipFile_initIDs_cb0 = Module['_ZipFile_initIDs_cb0'] = createExportWrapper('ZipFile_initIDs_cb0', 5);
var _NativeImageBuffer_getNativeMap_cb0 = Module['_NativeImageBuffer_getNativeMap_cb0'] = createExportWrapper('NativeImageBuffer_getNativeMap_cb0', 5);
var _BootLoader_setBootLoaderUnnamedModule0_cb0 = Module['_BootLoader_setBootLoaderUnnamedModule0_cb0'] = createExportWrapper('BootLoader_setBootLoaderUnnamedModule0_cb0', 5);
var _NativeLibraries_findBuiltinLib_cb0 = Module['_NativeLibraries_findBuiltinLib_cb0'] = createExportWrapper('NativeLibraries_findBuiltinLib_cb0', 5);
var _NativeLibraries_load_cb0 = Module['_NativeLibraries_load_cb0'] = createExportWrapper('NativeLibraries_load_cb0', 5);
var _CDS_isDumpingClassList0_cb0 = Module['_CDS_isDumpingClassList0_cb0'] = createExportWrapper('CDS_isDumpingClassList0_cb0', 5);
var _CDS_isDumpingArchive0_cb0 = Module['_CDS_isDumpingArchive0_cb0'] = createExportWrapper('CDS_isDumpingArchive0_cb0', 5);
var _CDS_isSharingEnabled0_cb0 = Module['_CDS_isSharingEnabled0_cb0'] = createExportWrapper('CDS_isSharingEnabled0_cb0', 5);
var _CDS_getRandomSeedForDumping_cb0 = Module['_CDS_getRandomSeedForDumping_cb0'] = createExportWrapper('CDS_getRandomSeedForDumping_cb0', 5);
var _CDS_getCDSConfigStatus_cb0 = Module['_CDS_getCDSConfigStatus_cb0'] = createExportWrapper('CDS_getCDSConfigStatus_cb0', 5);
var _CDS_initializeFromArchive_cb0 = Module['_CDS_initializeFromArchive_cb0'] = createExportWrapper('CDS_initializeFromArchive_cb0', 5);
var _ScopedMemoryAccess_registerNatives_cb0 = Module['_ScopedMemoryAccess_registerNatives_cb0'] = createExportWrapper('ScopedMemoryAccess_registerNatives_cb0', 5);
var _Signal_findSignal0_cb0 = Module['_Signal_findSignal0_cb0'] = createExportWrapper('Signal_findSignal0_cb0', 5);
var _Signal_handle0_cb0 = Module['_Signal_handle0_cb0'] = createExportWrapper('Signal_handle0_cb0', 5);
var _Unsafe_registerNatives_cb0 = Module['_Unsafe_registerNatives_cb0'] = createExportWrapper('Unsafe_registerNatives_cb0', 5);
var _Unsafe_arrayBaseOffset0_cb0 = Module['_Unsafe_arrayBaseOffset0_cb0'] = createExportWrapper('Unsafe_arrayBaseOffset0_cb0', 5);
var _Unsafe_shouldBeInitialized0_cb0 = Module['_Unsafe_shouldBeInitialized0_cb0'] = createExportWrapper('Unsafe_shouldBeInitialized0_cb0', 5);
var _Unsafe_ensureClassInitialized0_cb0 = Module['_Unsafe_ensureClassInitialized0_cb0'] = createExportWrapper('Unsafe_ensureClassInitialized0_cb0', 5);
var _Unsafe_objectFieldOffset0_cb0 = Module['_Unsafe_objectFieldOffset0_cb0'] = createExportWrapper('Unsafe_objectFieldOffset0_cb0', 5);
var _Unsafe_objectFieldOffset1_cb0 = Module['_Unsafe_objectFieldOffset1_cb0'] = createExportWrapper('Unsafe_objectFieldOffset1_cb0', 5);
var _Unsafe_staticFieldOffset0_cb0 = Module['_Unsafe_staticFieldOffset0_cb0'] = createExportWrapper('Unsafe_staticFieldOffset0_cb0', 5);
var _Unsafe_staticFieldBase0_cb0 = Module['_Unsafe_staticFieldBase0_cb0'] = createExportWrapper('Unsafe_staticFieldBase0_cb0', 5);
var _Unsafe_arrayIndexScale0_cb0 = Module['_Unsafe_arrayIndexScale0_cb0'] = createExportWrapper('Unsafe_arrayIndexScale0_cb0', 5);
var _Unsafe_getIntVolatile_cb0 = Module['_Unsafe_getIntVolatile_cb0'] = createExportWrapper('Unsafe_getIntVolatile_cb0', 5);
var _Unsafe_getLongVolatile_cb0 = Module['_Unsafe_getLongVolatile_cb0'] = createExportWrapper('Unsafe_getLongVolatile_cb0', 5);
var _Unsafe_putReferenceVolatile_cb0 = Module['_Unsafe_putReferenceVolatile_cb0'] = createExportWrapper('Unsafe_putReferenceVolatile_cb0', 5);
var _Unsafe_putOrderedReference_cb0 = Module['_Unsafe_putOrderedReference_cb0'] = createExportWrapper('Unsafe_putOrderedReference_cb0', 5);
var _Unsafe_putOrderedLong_cb0 = Module['_Unsafe_putOrderedLong_cb0'] = createExportWrapper('Unsafe_putOrderedLong_cb0', 5);
var _Unsafe_putReference_cb0 = Module['_Unsafe_putReference_cb0'] = createExportWrapper('Unsafe_putReference_cb0', 5);
var _Unsafe_compareAndSetInt_cb0 = Module['_Unsafe_compareAndSetInt_cb0'] = createExportWrapper('Unsafe_compareAndSetInt_cb0', 5);
var _Unsafe_compareAndSetLong_cb0 = Module['_Unsafe_compareAndSetLong_cb0'] = createExportWrapper('Unsafe_compareAndSetLong_cb0', 5);
var _Unsafe_compareAndSetReference_cb0 = Module['_Unsafe_compareAndSetReference_cb0'] = createExportWrapper('Unsafe_compareAndSetReference_cb0', 5);
var _Unsafe_addressSize_cb0 = Module['_Unsafe_addressSize_cb0'] = createExportWrapper('Unsafe_addressSize_cb0', 5);
var _Unsafe_allocateMemory0_cb0 = Module['_Unsafe_allocateMemory0_cb0'] = createExportWrapper('Unsafe_allocateMemory0_cb0', 5);
var _Unsafe_allocateInstance_cb0 = Module['_Unsafe_allocateInstance_cb0'] = createExportWrapper('Unsafe_allocateInstance_cb0', 5);
var _Unsafe_freeMemory0_cb0 = Module['_Unsafe_freeMemory0_cb0'] = createExportWrapper('Unsafe_freeMemory0_cb0', 5);
var _Unsafe_putLong_cb1 = Module['_Unsafe_putLong_cb1'] = createExportWrapper('Unsafe_putLong_cb1', 5);
var _Unsafe_putLong_cb2 = Module['_Unsafe_putLong_cb2'] = createExportWrapper('Unsafe_putLong_cb2', 5);
var _Unsafe_putInt_cb0 = Module['_Unsafe_putInt_cb0'] = createExportWrapper('Unsafe_putInt_cb0', 5);
var _Unsafe_putShort_cb1 = Module['_Unsafe_putShort_cb1'] = createExportWrapper('Unsafe_putShort_cb1', 5);
var _Unsafe_putShort_cb2 = Module['_Unsafe_putShort_cb2'] = createExportWrapper('Unsafe_putShort_cb2', 5);
var _Unsafe_putByte_cb0 = Module['_Unsafe_putByte_cb0'] = createExportWrapper('Unsafe_putByte_cb0', 5);
var _Unsafe_getReference_cb0 = Module['_Unsafe_getReference_cb0'] = createExportWrapper('Unsafe_getReference_cb0', 5);
var _Unsafe_getInt_cb0 = Module['_Unsafe_getInt_cb0'] = createExportWrapper('Unsafe_getInt_cb0', 5);
var _Unsafe_getShort_cb0 = Module['_Unsafe_getShort_cb0'] = createExportWrapper('Unsafe_getShort_cb0', 5);
var _Unsafe_getByte_cb0 = Module['_Unsafe_getByte_cb0'] = createExportWrapper('Unsafe_getByte_cb0', 5);
var _Unsafe_getLong_cb0 = Module['_Unsafe_getLong_cb0'] = createExportWrapper('Unsafe_getLong_cb0', 5);
var _Unsafe_getByte_cb1 = Module['_Unsafe_getByte_cb1'] = createExportWrapper('Unsafe_getByte_cb1', 5);
var _Unsafe_getReferenceVolatile_cb0 = Module['_Unsafe_getReferenceVolatile_cb0'] = createExportWrapper('Unsafe_getReferenceVolatile_cb0', 5);
var _Unsafe_defineClass_cb0 = Module['_Unsafe_defineClass_cb0'] = createExportWrapper('Unsafe_defineClass_cb0', 5);
var _Unsafe_storeFence_cb0 = Module['_Unsafe_storeFence_cb0'] = createExportWrapper('Unsafe_storeFence_cb0', 5);
var _Unsafe_fullFence_cb0 = Module['_Unsafe_fullFence_cb0'] = createExportWrapper('Unsafe_fullFence_cb0', 5);
var _Unsafe_copyMemory0_cb0 = Module['_Unsafe_copyMemory0_cb0'] = createExportWrapper('Unsafe_copyMemory0_cb0', 5);
var _Unsafe_setMemory0_cb0 = Module['_Unsafe_setMemory0_cb0'] = createExportWrapper('Unsafe_setMemory0_cb0', 5);
var _VM_initialize_cb0 = Module['_VM_initialize_cb0'] = createExportWrapper('VM_initialize_cb0', 5);
var _Perf_registerNatives_cb0 = Module['_Perf_registerNatives_cb0'] = createExportWrapper('Perf_registerNatives_cb0', 5);
var _Perf_createLong_cb0 = Module['_Perf_createLong_cb0'] = createExportWrapper('Perf_createLong_cb0', 5);
var _ConstantPool_getUTF8At0_cb0 = Module['_ConstantPool_getUTF8At0_cb0'] = createExportWrapper('ConstantPool_getUTF8At0_cb0', 5);
var _ConstantPool_getIntAt0_cb0 = Module['_ConstantPool_getIntAt0_cb0'] = createExportWrapper('ConstantPool_getIntAt0_cb0', 5);
var _Reflection_getCallerClass_cb0 = Module['_Reflection_getCallerClass_cb0'] = createExportWrapper('Reflection_getCallerClass_cb0', 5);
var _Reflection_getClassAccessFlags_cb0 = Module['_Reflection_getClassAccessFlags_cb0'] = createExportWrapper('Reflection_getClassAccessFlags_cb0', 5);
var _Reflection_areNestMates_cb0 = Module['_Reflection_areNestMates_cb0'] = createExportWrapper('Reflection_areNestMates_cb0', 5);
var _SystemProps_Raw_platformProperties_cb0 = Module['_SystemProps_Raw_platformProperties_cb0'] = createExportWrapper('SystemProps_Raw_platformProperties_cb0', 5);
var _SystemProps_Raw_vmProperties_cb0 = Module['_SystemProps_Raw_vmProperties_cb0'] = createExportWrapper('SystemProps_Raw_vmProperties_cb0', 5);
var _URLClassPath_getLookupCacheURLs_cb0 = Module['_URLClassPath_getLookupCacheURLs_cb0'] = createExportWrapper('URLClassPath_getLookupCacheURLs_cb0', 5);
var _VM_initialize_cb1 = Module['_VM_initialize_cb1'] = createExportWrapper('VM_initialize_cb1', 5);
var _IOUtil_initIDs_cb0 = Module['_IOUtil_initIDs_cb0'] = createExportWrapper('IOUtil_initIDs_cb0', 5);
var _IOUtil_iovMax_cb0 = Module['_IOUtil_iovMax_cb0'] = createExportWrapper('IOUtil_iovMax_cb0', 5);
var _IOUtil_writevMax_cb0 = Module['_IOUtil_writevMax_cb0'] = createExportWrapper('IOUtil_writevMax_cb0', 5);
var _NativeThread_init_cb0 = Module['_NativeThread_init_cb0'] = createExportWrapper('NativeThread_init_cb0', 5);
var _NativeThread_current0_cb0 = Module['_NativeThread_current0_cb0'] = createExportWrapper('NativeThread_current0_cb0', 5);
var _UnixNativeDispatcher_init_cb0 = Module['_UnixNativeDispatcher_init_cb0'] = createExportWrapper('UnixNativeDispatcher_init_cb0', 5);
var _UnixNativeDispatcher_getcwd_cb0 = Module['_UnixNativeDispatcher_getcwd_cb0'] = createExportWrapper('UnixNativeDispatcher_getcwd_cb0', 5);
var _UnixNativeDispatcher_stat0_cb0 = Module['_UnixNativeDispatcher_stat0_cb0'] = createExportWrapper('UnixNativeDispatcher_stat0_cb0', 5);
var _UnixNativeDispatcher_open0_cb0 = Module['_UnixNativeDispatcher_open0_cb0'] = createExportWrapper('UnixNativeDispatcher_open0_cb0', 5);
var _UnixFileDispatcherImpl_size0_cb0 = Module['_UnixFileDispatcherImpl_size0_cb0'] = createExportWrapper('UnixFileDispatcherImpl_size0_cb0', 5);
var _UnixFileDispatcherImpl_allocationGranularity0_cb0 = Module['_UnixFileDispatcherImpl_allocationGranularity0_cb0'] = createExportWrapper('UnixFileDispatcherImpl_allocationGranularity0_cb0', 5);
var _UnixFileDispatcherImpl_map0_cb0 = Module['_UnixFileDispatcherImpl_map0_cb0'] = createExportWrapper('UnixFileDispatcherImpl_map0_cb0', 5);
var _UnixFileDispatcherImpl_unmap0_cb0 = Module['_UnixFileDispatcherImpl_unmap0_cb0'] = createExportWrapper('UnixFileDispatcherImpl_unmap0_cb0', 5);
var _FileDispatcherImpl_init0_cb0 = Module['_FileDispatcherImpl_init0_cb0'] = createExportWrapper('FileDispatcherImpl_init0_cb0', 5);
var _emscripten_stack_get_base = wasmExports['emscripten_stack_get_base']
var _emscripten_stack_get_end = wasmExports['emscripten_stack_get_end']
var _emscripten_builtin_memalign = createExportWrapper('emscripten_builtin_memalign', 2);
var _emscripten_stack_get_current = wasmExports['emscripten_stack_get_current']
var _fflush = createExportWrapper('fflush', 1);
var _htons = createExportWrapper('htons', 1);
var _ntohs = createExportWrapper('ntohs', 1);
var _emscripten_proxy_execute_queue = createExportWrapper('emscripten_proxy_execute_queue', 1);
var _emscripten_proxy_finish = createExportWrapper('emscripten_proxy_finish', 1);
var __emscripten_timeout = createExportWrapper('_emscripten_timeout', 2);
var _setThrew = createExportWrapper('setThrew', 2);
var _emscripten_stack_init = wasmExports['emscripten_stack_init']
var _emscripten_stack_get_free = wasmExports['emscripten_stack_get_free']
var __emscripten_stack_restore = wasmExports['_emscripten_stack_restore']
var __emscripten_stack_alloc = wasmExports['_emscripten_stack_alloc']
var __wasmfs_fetch_get_file_path = createExportWrapper('_wasmfs_fetch_get_file_path', 1);
var __wasmfs_read_file = createExportWrapper('_wasmfs_read_file', 1);
var __wasmfs_write_file = createExportWrapper('_wasmfs_write_file', 3);
var __wasmfs_mkdir = createExportWrapper('_wasmfs_mkdir', 2);
var __wasmfs_rmdir = createExportWrapper('_wasmfs_rmdir', 1);
var __wasmfs_open = createExportWrapper('_wasmfs_open', 3);
var __wasmfs_allocate = createExportWrapper('_wasmfs_allocate', 3);
var __wasmfs_mknod = createExportWrapper('_wasmfs_mknod', 3);
var __wasmfs_unlink = createExportWrapper('_wasmfs_unlink', 1);
var __wasmfs_chdir = createExportWrapper('_wasmfs_chdir', 1);
var __wasmfs_symlink = createExportWrapper('_wasmfs_symlink', 2);
var __wasmfs_readlink = createExportWrapper('_wasmfs_readlink', 1);
var __wasmfs_write = createExportWrapper('_wasmfs_write', 3);
var __wasmfs_pwrite = createExportWrapper('_wasmfs_pwrite', 4);
var __wasmfs_chmod = createExportWrapper('_wasmfs_chmod', 2);
var __wasmfs_fchmod = createExportWrapper('_wasmfs_fchmod', 2);
var __wasmfs_lchmod = createExportWrapper('_wasmfs_lchmod', 2);
var __wasmfs_llseek = createExportWrapper('_wasmfs_llseek', 3);
var __wasmfs_rename = createExportWrapper('_wasmfs_rename', 2);
var __wasmfs_read = createExportWrapper('_wasmfs_read', 3);
var __wasmfs_pread = createExportWrapper('_wasmfs_pread', 4);
var __wasmfs_truncate = createExportWrapper('_wasmfs_truncate', 2);
var __wasmfs_ftruncate = createExportWrapper('_wasmfs_ftruncate', 2);
var __wasmfs_close = createExportWrapper('_wasmfs_close', 1);
var __wasmfs_mmap = createExportWrapper('_wasmfs_mmap', 5);
var __wasmfs_msync = createExportWrapper('_wasmfs_msync', 3);
var __wasmfs_munmap = createExportWrapper('_wasmfs_munmap', 2);
var __wasmfs_utime = createExportWrapper('_wasmfs_utime', 3);
var __wasmfs_stat = createExportWrapper('_wasmfs_stat', 2);
var __wasmfs_lstat = createExportWrapper('_wasmfs_lstat', 2);
var __wasmfs_mount = createExportWrapper('_wasmfs_mount', 2);
var __wasmfs_unmount = createExportWrapper('_wasmfs_unmount', 1);
var __wasmfs_identify = createExportWrapper('_wasmfs_identify', 1);
var __wasmfs_readdir_start = createExportWrapper('_wasmfs_readdir_start', 1);
var __wasmfs_readdir_get = createExportWrapper('_wasmfs_readdir_get', 1);
var __wasmfs_readdir_finish = createExportWrapper('_wasmfs_readdir_finish', 1);
var __wasmfs_get_cwd = createExportWrapper('_wasmfs_get_cwd', 0);
var _wasmfs_create_jsimpl_backend = createExportWrapper('wasmfs_create_jsimpl_backend', 0);
var _wasmfs_create_memory_backend = createExportWrapper('wasmfs_create_memory_backend', 0);
var __wasmfs_node_record_dirent = createExportWrapper('_wasmfs_node_record_dirent', 3);
var __wasmfs_opfs_record_entry = createExportWrapper('_wasmfs_opfs_record_entry', 3);
var _wasmfs_create_file = createExportWrapper('wasmfs_create_file', 3);
var _wasmfs_flush = createExportWrapper('wasmfs_flush', 0);
var _NATIVE_INFO_FileDescriptor_initIDs_0 = Module['_NATIVE_INFO_FileDescriptor_initIDs_0'] = 5071628;
var _NATIVE_INFO_FileDescriptor_set_0 = Module['_NATIVE_INFO_FileDescriptor_set_0'] = 5071660;
var _NATIVE_INFO_FileDescriptor_getHandle_0 = Module['_NATIVE_INFO_FileDescriptor_getHandle_0'] = 5071692;
var _NATIVE_INFO_FileDescriptor_getAppend_0 = Module['_NATIVE_INFO_FileDescriptor_getAppend_0'] = 5071724;
var _NATIVE_INFO_FileDescriptor_close0_0 = Module['_NATIVE_INFO_FileDescriptor_close0_0'] = 5071756;
var _NATIVE_INFO_FileInputStream_initIDs_0 = Module['_NATIVE_INFO_FileInputStream_initIDs_0'] = 5071788;
var _NATIVE_INFO_FileInputStream_open0_0 = Module['_NATIVE_INFO_FileInputStream_open0_0'] = 5071820;
var _NATIVE_INFO_FileInputStream_read0_0 = Module['_NATIVE_INFO_FileInputStream_read0_0'] = 5071852;
var _NATIVE_INFO_FileInputStream_readBytes_0 = Module['_NATIVE_INFO_FileInputStream_readBytes_0'] = 5071884;
var _NATIVE_INFO_FileInputStream_close0_0 = Module['_NATIVE_INFO_FileInputStream_close0_0'] = 5071916;
var _NATIVE_INFO_FileInputStream_available0_0 = Module['_NATIVE_INFO_FileInputStream_available0_0'] = 5071948;
var _NATIVE_INFO_FileOutputStream_initIDs_0 = Module['_NATIVE_INFO_FileOutputStream_initIDs_0'] = 5071980;
var _NATIVE_INFO_FileOutputStream_writeBytes_0 = Module['_NATIVE_INFO_FileOutputStream_writeBytes_0'] = 5072012;
var _NATIVE_INFO_FileOutputStream_close0_0 = Module['_NATIVE_INFO_FileOutputStream_close0_0'] = 5072044;
var _NATIVE_INFO_RandomAccessFile_initIDs_0 = Module['_NATIVE_INFO_RandomAccessFile_initIDs_0'] = 5072076;
var _NATIVE_INFO_RandomAccessFile_open0_0 = Module['_NATIVE_INFO_RandomAccessFile_open0_0'] = 5072108;
var _NATIVE_INFO_RandomAccessFile_read0_0 = Module['_NATIVE_INFO_RandomAccessFile_read0_0'] = 5072140;
var _NATIVE_INFO_RandomAccessFile_seek0_0 = Module['_NATIVE_INFO_RandomAccessFile_seek0_0'] = 5072172;
var _NATIVE_INFO_RandomAccessFile_getFilePointer_0 = Module['_NATIVE_INFO_RandomAccessFile_getFilePointer_0'] = 5072204;
var _NATIVE_INFO_RandomAccessFile_close0_0 = Module['_NATIVE_INFO_RandomAccessFile_close0_0'] = 5072236;
var _NATIVE_INFO_RandomAccessFile_length0_0 = Module['_NATIVE_INFO_RandomAccessFile_length0_0'] = 5072268;
var _NATIVE_INFO_RandomAccessFile_readBytes0_0 = Module['_NATIVE_INFO_RandomAccessFile_readBytes0_0'] = 5072300;
var _NATIVE_INFO_UnixFileSystem_initIDs_0 = Module['_NATIVE_INFO_UnixFileSystem_initIDs_0'] = 5072332;
var _NATIVE_INFO_UnixFileSystem_checkAccess0_0 = Module['_NATIVE_INFO_UnixFileSystem_checkAccess0_0'] = 5072364;
var _NATIVE_INFO_UnixFileSystem_getBooleanAttributes0_0 = Module['_NATIVE_INFO_UnixFileSystem_getBooleanAttributes0_0'] = 5072396;
var _NATIVE_INFO_UnixFileSystem_canonicalize0_0 = Module['_NATIVE_INFO_UnixFileSystem_canonicalize0_0'] = 5072428;
var _NATIVE_INFO_UnixFileSystem_getLastModifiedTime_0 = Module['_NATIVE_INFO_UnixFileSystem_getLastModifiedTime_0'] = 5072460;
var _NATIVE_INFO_Class_registerNatives_0 = Module['_NATIVE_INFO_Class_registerNatives_0'] = 5072492;
var _NATIVE_INFO_Class_getPrimitiveClass_0 = Module['_NATIVE_INFO_Class_getPrimitiveClass_0'] = 5072524;
var _NATIVE_INFO_Class_getEnclosingMethod0_0 = Module['_NATIVE_INFO_Class_getEnclosingMethod0_0'] = 5072556;
var _NATIVE_INFO_Class_getDeclaringClass0_0 = Module['_NATIVE_INFO_Class_getDeclaringClass0_0'] = 5072588;
var _NATIVE_INFO_Class_getComponentType_0 = Module['_NATIVE_INFO_Class_getComponentType_0'] = 5072620;
var _NATIVE_INFO_Class_getModifiers_0 = Module['_NATIVE_INFO_Class_getModifiers_0'] = 5072652;
var _NATIVE_INFO_Class_getSuperclass_0 = Module['_NATIVE_INFO_Class_getSuperclass_0'] = 5072684;
var _NATIVE_INFO_Class_getClassLoader_0 = Module['_NATIVE_INFO_Class_getClassLoader_0'] = 5072716;
var _NATIVE_INFO_Class_getPermittedSubclasses0_0 = Module['_NATIVE_INFO_Class_getPermittedSubclasses0_0'] = 5072748;
var _NATIVE_INFO_Class_initClassName_0 = Module['_NATIVE_INFO_Class_initClassName_0'] = 5072780;
var _NATIVE_INFO_Class_forName0_0 = Module['_NATIVE_INFO_Class_forName0_0'] = 5072812;
var _NATIVE_INFO_Class_desiredAssertionStatus0_0 = Module['_NATIVE_INFO_Class_desiredAssertionStatus0_0'] = 5072844;
var _NATIVE_INFO_Class_getDeclaredFields0_0 = Module['_NATIVE_INFO_Class_getDeclaredFields0_0'] = 5072876;
var _NATIVE_INFO_Class_getDeclaredConstructors0_0 = Module['_NATIVE_INFO_Class_getDeclaredConstructors0_0'] = 5072908;
var _NATIVE_INFO_Class_getDeclaredMethods0_0 = Module['_NATIVE_INFO_Class_getDeclaredMethods0_0'] = 5072940;
var _NATIVE_INFO_Class_getDeclaredClasses0_0 = Module['_NATIVE_INFO_Class_getDeclaredClasses0_0'] = 5072972;
var _NATIVE_INFO_Class_isPrimitive_0 = Module['_NATIVE_INFO_Class_isPrimitive_0'] = 5073004;
var _NATIVE_INFO_Class_isInterface_0 = Module['_NATIVE_INFO_Class_isInterface_0'] = 5073036;
var _NATIVE_INFO_Class_isAssignableFrom_0 = Module['_NATIVE_INFO_Class_isAssignableFrom_0'] = 5073068;
var _NATIVE_INFO_Class_isInstance_0 = Module['_NATIVE_INFO_Class_isInstance_0'] = 5073100;
var _NATIVE_INFO_Class_isArray_0 = Module['_NATIVE_INFO_Class_isArray_0'] = 5073132;
var _NATIVE_INFO_Class_isHidden_0 = Module['_NATIVE_INFO_Class_isHidden_0'] = 5073164;
var _NATIVE_INFO_Class_getNestHost0_0 = Module['_NATIVE_INFO_Class_getNestHost0_0'] = 5073196;
var _NATIVE_INFO_Class_getConstantPool_0 = Module['_NATIVE_INFO_Class_getConstantPool_0'] = 5073228;
var _NATIVE_INFO_Class_getRawAnnotations_0 = Module['_NATIVE_INFO_Class_getRawAnnotations_0'] = 5073260;
var _NATIVE_INFO_Class_getRawTypeAnnotations_0 = Module['_NATIVE_INFO_Class_getRawTypeAnnotations_0'] = 5073292;
var _NATIVE_INFO_Class_getInterfaces0_0 = Module['_NATIVE_INFO_Class_getInterfaces0_0'] = 5073324;
var _NATIVE_INFO_Class_getGenericSignature0_0 = Module['_NATIVE_INFO_Class_getGenericSignature0_0'] = 5073356;
var _NATIVE_INFO_Class_getProtectionDomain0_0 = Module['_NATIVE_INFO_Class_getProtectionDomain0_0'] = 5073388;
var _NATIVE_INFO_ClassLoader_registerNatives_0 = Module['_NATIVE_INFO_ClassLoader_registerNatives_0'] = 5073420;
var _NATIVE_INFO_ClassLoader_findLoadedClass0_0 = Module['_NATIVE_INFO_ClassLoader_findLoadedClass0_0'] = 5073452;
var _NATIVE_INFO_ClassLoader_findBootstrapClass_0 = Module['_NATIVE_INFO_ClassLoader_findBootstrapClass_0'] = 5073484;
var _NATIVE_INFO_ClassLoader_findBuiltinLib_0 = Module['_NATIVE_INFO_ClassLoader_findBuiltinLib_0'] = 5073516;
var _NATIVE_INFO_ClassLoader_defineClass2_0 = Module['_NATIVE_INFO_ClassLoader_defineClass2_0'] = 5073548;
var _NATIVE_INFO_ClassLoader_defineClass1_0 = Module['_NATIVE_INFO_ClassLoader_defineClass1_0'] = 5073580;
var _NATIVE_INFO_ClassLoader_defineClass0_0 = Module['_NATIVE_INFO_ClassLoader_defineClass0_0'] = 5073612;
var _NATIVE_INFO_Double_doubleToRawLongBits_0 = Module['_NATIVE_INFO_Double_doubleToRawLongBits_0'] = 5073644;
var _NATIVE_INFO_Double_longBitsToDouble_0 = Module['_NATIVE_INFO_Double_longBitsToDouble_0'] = 5073676;
var _NATIVE_INFO_Float_floatToRawIntBits_0 = Module['_NATIVE_INFO_Float_floatToRawIntBits_0'] = 5073708;
var _NATIVE_INFO_Float_intBitsToFloat_0 = Module['_NATIVE_INFO_Float_intBitsToFloat_0'] = 5073740;
var _NATIVE_INFO_Module_defineModule0_0 = Module['_NATIVE_INFO_Module_defineModule0_0'] = 5073772;
var _NATIVE_INFO_Module_addReads0_0 = Module['_NATIVE_INFO_Module_addReads0_0'] = 5073804;
var _NATIVE_INFO_Module_addExportsToAll0_0 = Module['_NATIVE_INFO_Module_addExportsToAll0_0'] = 5073836;
var _NATIVE_INFO_Module_addExports0_0 = Module['_NATIVE_INFO_Module_addExports0_0'] = 5073868;
var _NATIVE_INFO_NullPointerException_getExtendedNPEMessage_0 = Module['_NATIVE_INFO_NullPointerException_getExtendedNPEMessage_0'] = 5073900;
var _NATIVE_INFO_Object_hashCode_0 = Module['_NATIVE_INFO_Object_hashCode_0'] = 5073932;
var _NATIVE_INFO_Object_registerNatives_0 = Module['_NATIVE_INFO_Object_registerNatives_0'] = 5073964;
var _NATIVE_INFO_Object_clone_0 = Module['_NATIVE_INFO_Object_clone_0'] = 5073996;
var _NATIVE_INFO_Object_getClass_0 = Module['_NATIVE_INFO_Object_getClass_0'] = 5074028;
var _NATIVE_INFO_Object_notifyAll_0 = Module['_NATIVE_INFO_Object_notifyAll_0'] = 5074060;
var _NATIVE_INFO_Object_notify_0 = Module['_NATIVE_INFO_Object_notify_0'] = 5074092;
var _NATIVE_INFO_Object_wait_0 = Module['_NATIVE_INFO_Object_wait_0'] = 5074124;
var _NATIVE_INFO_Runtime_availableProcessors_0 = Module['_NATIVE_INFO_Runtime_availableProcessors_0'] = 5074156;
var _NATIVE_INFO_Runtime_maxMemory_0 = Module['_NATIVE_INFO_Runtime_maxMemory_0'] = 5074188;
var _NATIVE_INFO_String_intern_0 = Module['_NATIVE_INFO_String_intern_0'] = 5074220;
var _NATIVE_INFO_StringUTF16_isBigEndian_0 = Module['_NATIVE_INFO_StringUTF16_isBigEndian_0'] = 5074252;
var _NATIVE_INFO_System_mapLibraryName_0 = Module['_NATIVE_INFO_System_mapLibraryName_0'] = 5074284;
var _NATIVE_INFO_System_arraycopy_0 = Module['_NATIVE_INFO_System_arraycopy_0'] = 5074316;
var _NATIVE_INFO_System_registerNatives_0 = Module['_NATIVE_INFO_System_registerNatives_0'] = 5074348;
var _NATIVE_INFO_System_setOut0_0 = Module['_NATIVE_INFO_System_setOut0_0'] = 5074380;
var _NATIVE_INFO_System_setIn0_0 = Module['_NATIVE_INFO_System_setIn0_0'] = 5074412;
var _NATIVE_INFO_System_setErr0_0 = Module['_NATIVE_INFO_System_setErr0_0'] = 5074444;
var _NATIVE_INFO_System_identityHashCode_0 = Module['_NATIVE_INFO_System_identityHashCode_0'] = 5074476;
var _NATIVE_INFO_System_currentTimeMillis_0 = Module['_NATIVE_INFO_System_currentTimeMillis_0'] = 5074508;
var _NATIVE_INFO_System_nanoTime_0 = Module['_NATIVE_INFO_System_nanoTime_0'] = 5074540;
var _NATIVE_INFO_Thread_registerNatives_0 = Module['_NATIVE_INFO_Thread_registerNatives_0'] = 5074572;
var _NATIVE_INFO_Thread_currentThread_0 = Module['_NATIVE_INFO_Thread_currentThread_0'] = 5074604;
var _NATIVE_INFO_Thread_setPriority0_0 = Module['_NATIVE_INFO_Thread_setPriority0_0'] = 5074636;
var _NATIVE_INFO_Thread_isAlive_0 = Module['_NATIVE_INFO_Thread_isAlive_0'] = 5074668;
var _NATIVE_INFO_Thread_holdsLock_0 = Module['_NATIVE_INFO_Thread_holdsLock_0'] = 5074700;
var _NATIVE_INFO_Thread_start0_0 = Module['_NATIVE_INFO_Thread_start0_0'] = 5074732;
var _NATIVE_INFO_Thread_ensureMaterializedForStackWalk_0 = Module['_NATIVE_INFO_Thread_ensureMaterializedForStackWalk_0'] = 5074764;
var _NATIVE_INFO_Thread_getNextThreadIdOffset_0 = Module['_NATIVE_INFO_Thread_getNextThreadIdOffset_0'] = 5074796;
var _NATIVE_INFO_Thread_currentCarrierThread_0 = Module['_NATIVE_INFO_Thread_currentCarrierThread_0'] = 5074828;
var _NATIVE_INFO_Thread_interrupt0_0 = Module['_NATIVE_INFO_Thread_interrupt0_0'] = 5074860;
var _NATIVE_INFO_Thread_sleepNanos0_0 = Module['_NATIVE_INFO_Thread_sleepNanos0_0'] = 5074892;
var _NATIVE_INFO_Thread_clearInterruptEvent_0 = Module['_NATIVE_INFO_Thread_clearInterruptEvent_0'] = 5074924;
var _NATIVE_INFO_Thread_yield0_0 = Module['_NATIVE_INFO_Thread_yield0_0'] = 5074956;
var _NATIVE_INFO_Throwable_fillInStackTrace_0 = Module['_NATIVE_INFO_Throwable_fillInStackTrace_0'] = 5074988;
var _NATIVE_INFO_Throwable_getStackTraceDepth_0 = Module['_NATIVE_INFO_Throwable_getStackTraceDepth_0'] = 5075020;
var _NATIVE_INFO_Throwable_getStackTraceElement_0 = Module['_NATIVE_INFO_Throwable_getStackTraceElement_0'] = 5075052;
var _NATIVE_INFO_StackTraceElement_initStackTraceElements_0 = Module['_NATIVE_INFO_StackTraceElement_initStackTraceElements_0'] = 5075084;
var _NATIVE_INFO_MethodHandle_linkToVirtual_0 = Module['_NATIVE_INFO_MethodHandle_linkToVirtual_0'] = 5075116;
var _NATIVE_INFO_MethodHandle_linkToInterface_0 = Module['_NATIVE_INFO_MethodHandle_linkToInterface_0'] = 5075148;
var _NATIVE_INFO_MethodHandle_linkToSpecial_0 = Module['_NATIVE_INFO_MethodHandle_linkToSpecial_0'] = 5075180;
var _NATIVE_INFO_MethodHandle_linkToStatic_0 = Module['_NATIVE_INFO_MethodHandle_linkToStatic_0'] = 5075212;
var _NATIVE_INFO_MethodHandleNatives_registerNatives_0 = Module['_NATIVE_INFO_MethodHandleNatives_registerNatives_0'] = 5075244;
var _NATIVE_INFO_MethodHandleNatives_getConstant_0 = Module['_NATIVE_INFO_MethodHandleNatives_getConstant_0'] = 5075276;
var _NATIVE_INFO_MethodHandleNatives_getNamedCon_0 = Module['_NATIVE_INFO_MethodHandleNatives_getNamedCon_0'] = 5075308;
var _NATIVE_INFO_MethodHandleNatives_resolve_0 = Module['_NATIVE_INFO_MethodHandleNatives_resolve_0'] = 5075340;
var _NATIVE_INFO_MethodHandleNatives_getMemberVMInfo_0 = Module['_NATIVE_INFO_MethodHandleNatives_getMemberVMInfo_0'] = 5075372;
var _NATIVE_INFO_MethodHandleNatives_init_0 = Module['_NATIVE_INFO_MethodHandleNatives_init_0'] = 5075404;
var _NATIVE_INFO_MethodHandleNatives_objectFieldOffset_0 = Module['_NATIVE_INFO_MethodHandleNatives_objectFieldOffset_0'] = 5075436;
var _NATIVE_INFO_MethodHandleNatives_staticFieldBase_0 = Module['_NATIVE_INFO_MethodHandleNatives_staticFieldBase_0'] = 5075468;
var _NATIVE_INFO_MethodHandleNatives_staticFieldOffset_0 = Module['_NATIVE_INFO_MethodHandleNatives_staticFieldOffset_0'] = 5075500;
var _NATIVE_INFO_MethodHandleNatives_getMembers_0 = Module['_NATIVE_INFO_MethodHandleNatives_getMembers_0'] = 5075532;
var _NATIVE_INFO_Finalizer_isFinalizationEnabled_0 = Module['_NATIVE_INFO_Finalizer_isFinalizationEnabled_0'] = 5075564;
var _NATIVE_INFO_Reference_refersTo0_0 = Module['_NATIVE_INFO_Reference_refersTo0_0'] = 5075596;
var _NATIVE_INFO_Reference_clear0_0 = Module['_NATIVE_INFO_Reference_clear0_0'] = 5075628;
var _NATIVE_INFO_Reference_waitForReferencePendingList_0 = Module['_NATIVE_INFO_Reference_waitForReferencePendingList_0'] = 5075660;
var _NATIVE_INFO_Reference_getAndClearReferencePendingList_0 = Module['_NATIVE_INFO_Reference_getAndClearReferencePendingList_0'] = 5075692;
var _NATIVE_INFO_Array_newArray_0 = Module['_NATIVE_INFO_Array_newArray_0'] = 5075724;
var _NATIVE_INFO_Array_getLength_0 = Module['_NATIVE_INFO_Array_getLength_0'] = 5075756;
var _NATIVE_INFO_Executable_getParameters0_0 = Module['_NATIVE_INFO_Executable_getParameters0_0'] = 5075788;
var _NATIVE_INFO_Proxy_defineClass0_0 = Module['_NATIVE_INFO_Proxy_defineClass0_0'] = 5075820;
var _NATIVE_INFO_AccessController_doPrivileged_0 = Module['_NATIVE_INFO_AccessController_doPrivileged_0'] = 5075852;
var _NATIVE_INFO_AccessController_doPrivileged_1 = Module['_NATIVE_INFO_AccessController_doPrivileged_1'] = 5075884;
var _NATIVE_INFO_AccessController_getStackAccessControlContext_0 = Module['_NATIVE_INFO_AccessController_getStackAccessControlContext_0'] = 5075916;
var _NATIVE_INFO_AccessController_doPrivileged_2 = Module['_NATIVE_INFO_AccessController_doPrivileged_2'] = 5075948;
var _NATIVE_INFO_AccessController_ensureMaterializedForStackWalk_0 = Module['_NATIVE_INFO_AccessController_ensureMaterializedForStackWalk_0'] = 5075980;
var _NATIVE_INFO_AccessController_doPrivileged_3 = Module['_NATIVE_INFO_AccessController_doPrivileged_3'] = 5076012;
var _NATIVE_INFO_TimeZone_getSystemTimeZoneID_0 = Module['_NATIVE_INFO_TimeZone_getSystemTimeZoneID_0'] = 5076044;
var _NATIVE_INFO_AtomicLong_VMSupportsCS8_0 = Module['_NATIVE_INFO_AtomicLong_VMSupportsCS8_0'] = 5076076;
var _NATIVE_INFO_Inflater_initIDs_0 = Module['_NATIVE_INFO_Inflater_initIDs_0'] = 5076108;
var _NATIVE_INFO_Inflater_init_0 = Module['_NATIVE_INFO_Inflater_init_0'] = 5076140;
var _NATIVE_INFO_Inflater_inflateBytesBytes_0 = Module['_NATIVE_INFO_Inflater_inflateBytesBytes_0'] = 5076172;
var _NATIVE_INFO_Inflater_reset_0 = Module['_NATIVE_INFO_Inflater_reset_0'] = 5076204;
var _NATIVE_INFO_Inflater_end_0 = Module['_NATIVE_INFO_Inflater_end_0'] = 5076236;
var _NATIVE_INFO_CRC32_updateBytes0_0 = Module['_NATIVE_INFO_CRC32_updateBytes0_0'] = 5076268;
var _NATIVE_INFO_ZipFile_initIDs_0 = Module['_NATIVE_INFO_ZipFile_initIDs_0'] = 5076300;
var _NATIVE_INFO_NativeImageBuffer_getNativeMap_0 = Module['_NATIVE_INFO_NativeImageBuffer_getNativeMap_0'] = 5076332;
var _NATIVE_INFO_BootLoader_setBootLoaderUnnamedModule0_0 = Module['_NATIVE_INFO_BootLoader_setBootLoaderUnnamedModule0_0'] = 5076364;
var _NATIVE_INFO_NativeLibraries_findBuiltinLib_0 = Module['_NATIVE_INFO_NativeLibraries_findBuiltinLib_0'] = 5076396;
var _NATIVE_INFO_NativeLibraries_load_0 = Module['_NATIVE_INFO_NativeLibraries_load_0'] = 5076428;
var _NATIVE_INFO_CDS_isDumpingClassList0_0 = Module['_NATIVE_INFO_CDS_isDumpingClassList0_0'] = 5076460;
var _NATIVE_INFO_CDS_isDumpingArchive0_0 = Module['_NATIVE_INFO_CDS_isDumpingArchive0_0'] = 5076492;
var _NATIVE_INFO_CDS_isSharingEnabled0_0 = Module['_NATIVE_INFO_CDS_isSharingEnabled0_0'] = 5076524;
var _NATIVE_INFO_CDS_getRandomSeedForDumping_0 = Module['_NATIVE_INFO_CDS_getRandomSeedForDumping_0'] = 5076556;
var _NATIVE_INFO_CDS_getCDSConfigStatus_0 = Module['_NATIVE_INFO_CDS_getCDSConfigStatus_0'] = 5076588;
var _NATIVE_INFO_CDS_initializeFromArchive_0 = Module['_NATIVE_INFO_CDS_initializeFromArchive_0'] = 5076620;
var _NATIVE_INFO_ScopedMemoryAccess_registerNatives_0 = Module['_NATIVE_INFO_ScopedMemoryAccess_registerNatives_0'] = 5076652;
var _NATIVE_INFO_Signal_findSignal0_0 = Module['_NATIVE_INFO_Signal_findSignal0_0'] = 5076684;
var _NATIVE_INFO_Signal_handle0_0 = Module['_NATIVE_INFO_Signal_handle0_0'] = 5076716;
var _NATIVE_INFO_Unsafe_registerNatives_0 = Module['_NATIVE_INFO_Unsafe_registerNatives_0'] = 5076748;
var _NATIVE_INFO_Unsafe_arrayBaseOffset0_0 = Module['_NATIVE_INFO_Unsafe_arrayBaseOffset0_0'] = 5076780;
var _NATIVE_INFO_Unsafe_shouldBeInitialized0_0 = Module['_NATIVE_INFO_Unsafe_shouldBeInitialized0_0'] = 5076812;
var _NATIVE_INFO_Unsafe_ensureClassInitialized0_0 = Module['_NATIVE_INFO_Unsafe_ensureClassInitialized0_0'] = 5076844;
var _NATIVE_INFO_Unsafe_objectFieldOffset0_0 = Module['_NATIVE_INFO_Unsafe_objectFieldOffset0_0'] = 5076876;
var _NATIVE_INFO_Unsafe_objectFieldOffset1_0 = Module['_NATIVE_INFO_Unsafe_objectFieldOffset1_0'] = 5076908;
var _NATIVE_INFO_Unsafe_staticFieldOffset0_0 = Module['_NATIVE_INFO_Unsafe_staticFieldOffset0_0'] = 5076940;
var _NATIVE_INFO_Unsafe_staticFieldBase0_0 = Module['_NATIVE_INFO_Unsafe_staticFieldBase0_0'] = 5076972;
var _NATIVE_INFO_Unsafe_arrayIndexScale0_0 = Module['_NATIVE_INFO_Unsafe_arrayIndexScale0_0'] = 5077004;
var _NATIVE_INFO_Unsafe_getIntVolatile_0 = Module['_NATIVE_INFO_Unsafe_getIntVolatile_0'] = 5077036;
var _NATIVE_INFO_Unsafe_getLongVolatile_0 = Module['_NATIVE_INFO_Unsafe_getLongVolatile_0'] = 5077068;
var _NATIVE_INFO_Unsafe_putReferenceVolatile_0 = Module['_NATIVE_INFO_Unsafe_putReferenceVolatile_0'] = 5077100;
var _NATIVE_INFO_Unsafe_putOrderedReference_0 = Module['_NATIVE_INFO_Unsafe_putOrderedReference_0'] = 5077132;
var _NATIVE_INFO_Unsafe_putOrderedLong_0 = Module['_NATIVE_INFO_Unsafe_putOrderedLong_0'] = 5077164;
var _NATIVE_INFO_Unsafe_putReference_0 = Module['_NATIVE_INFO_Unsafe_putReference_0'] = 5077196;
var _NATIVE_INFO_Unsafe_compareAndSetInt_0 = Module['_NATIVE_INFO_Unsafe_compareAndSetInt_0'] = 5077228;
var _NATIVE_INFO_Unsafe_compareAndSetLong_0 = Module['_NATIVE_INFO_Unsafe_compareAndSetLong_0'] = 5077260;
var _NATIVE_INFO_Unsafe_compareAndSetReference_0 = Module['_NATIVE_INFO_Unsafe_compareAndSetReference_0'] = 5077292;
var _NATIVE_INFO_Unsafe_addressSize_0 = Module['_NATIVE_INFO_Unsafe_addressSize_0'] = 5077324;
var _NATIVE_INFO_Unsafe_allocateMemory0_0 = Module['_NATIVE_INFO_Unsafe_allocateMemory0_0'] = 5077356;
var _NATIVE_INFO_Unsafe_allocateInstance_0 = Module['_NATIVE_INFO_Unsafe_allocateInstance_0'] = 5077388;
var _NATIVE_INFO_Unsafe_freeMemory0_0 = Module['_NATIVE_INFO_Unsafe_freeMemory0_0'] = 5077420;
var _NATIVE_INFO_Unsafe_putLong_1 = Module['_NATIVE_INFO_Unsafe_putLong_1'] = 5077452;
var _NATIVE_INFO_Unsafe_putLong_2 = Module['_NATIVE_INFO_Unsafe_putLong_2'] = 5077484;
var _NATIVE_INFO_Unsafe_putInt_0 = Module['_NATIVE_INFO_Unsafe_putInt_0'] = 5077516;
var _NATIVE_INFO_Unsafe_putShort_1 = Module['_NATIVE_INFO_Unsafe_putShort_1'] = 5077548;
var _NATIVE_INFO_Unsafe_putShort_2 = Module['_NATIVE_INFO_Unsafe_putShort_2'] = 5077580;
var _NATIVE_INFO_Unsafe_putByte_0 = Module['_NATIVE_INFO_Unsafe_putByte_0'] = 5077612;
var _NATIVE_INFO_Unsafe_getReference_0 = Module['_NATIVE_INFO_Unsafe_getReference_0'] = 5077644;
var _NATIVE_INFO_Unsafe_getInt_0 = Module['_NATIVE_INFO_Unsafe_getInt_0'] = 5077676;
var _NATIVE_INFO_Unsafe_getShort_0 = Module['_NATIVE_INFO_Unsafe_getShort_0'] = 5077708;
var _NATIVE_INFO_Unsafe_getByte_0 = Module['_NATIVE_INFO_Unsafe_getByte_0'] = 5077740;
var _NATIVE_INFO_Unsafe_getLong_0 = Module['_NATIVE_INFO_Unsafe_getLong_0'] = 5077772;
var _NATIVE_INFO_Unsafe_getByte_1 = Module['_NATIVE_INFO_Unsafe_getByte_1'] = 5077804;
var _NATIVE_INFO_Unsafe_getReferenceVolatile_0 = Module['_NATIVE_INFO_Unsafe_getReferenceVolatile_0'] = 5077836;
var _NATIVE_INFO_Unsafe_defineClass_0 = Module['_NATIVE_INFO_Unsafe_defineClass_0'] = 5077868;
var _NATIVE_INFO_Unsafe_storeFence_0 = Module['_NATIVE_INFO_Unsafe_storeFence_0'] = 5077900;
var _NATIVE_INFO_Unsafe_fullFence_0 = Module['_NATIVE_INFO_Unsafe_fullFence_0'] = 5077932;
var _NATIVE_INFO_Unsafe_copyMemory0_0 = Module['_NATIVE_INFO_Unsafe_copyMemory0_0'] = 5077964;
var _NATIVE_INFO_Unsafe_setMemory0_0 = Module['_NATIVE_INFO_Unsafe_setMemory0_0'] = 5077996;
var _NATIVE_INFO_VM_initialize_0 = Module['_NATIVE_INFO_VM_initialize_0'] = 5078028;
var _NATIVE_INFO_Perf_registerNatives_0 = Module['_NATIVE_INFO_Perf_registerNatives_0'] = 5078060;
var _NATIVE_INFO_Perf_createLong_0 = Module['_NATIVE_INFO_Perf_createLong_0'] = 5078092;
var _NATIVE_INFO_ConstantPool_getUTF8At0_0 = Module['_NATIVE_INFO_ConstantPool_getUTF8At0_0'] = 5078124;
var _NATIVE_INFO_ConstantPool_getIntAt0_0 = Module['_NATIVE_INFO_ConstantPool_getIntAt0_0'] = 5078156;
var _NATIVE_INFO_Reflection_getCallerClass_0 = Module['_NATIVE_INFO_Reflection_getCallerClass_0'] = 5078188;
var _NATIVE_INFO_Reflection_getClassAccessFlags_0 = Module['_NATIVE_INFO_Reflection_getClassAccessFlags_0'] = 5078220;
var _NATIVE_INFO_Reflection_areNestMates_0 = Module['_NATIVE_INFO_Reflection_areNestMates_0'] = 5078252;
var _NATIVE_INFO_SystemProps_Raw_platformProperties_0 = Module['_NATIVE_INFO_SystemProps_Raw_platformProperties_0'] = 5078284;
var _NATIVE_INFO_SystemProps_Raw_vmProperties_0 = Module['_NATIVE_INFO_SystemProps_Raw_vmProperties_0'] = 5078316;
var _NATIVE_INFO_URLClassPath_getLookupCacheURLs_0 = Module['_NATIVE_INFO_URLClassPath_getLookupCacheURLs_0'] = 5078348;
var _NATIVE_INFO_VM_initialize_1 = Module['_NATIVE_INFO_VM_initialize_1'] = 5078380;
var _NATIVE_INFO_IOUtil_initIDs_0 = Module['_NATIVE_INFO_IOUtil_initIDs_0'] = 5078412;
var _NATIVE_INFO_IOUtil_iovMax_0 = Module['_NATIVE_INFO_IOUtil_iovMax_0'] = 5078444;
var _NATIVE_INFO_IOUtil_writevMax_0 = Module['_NATIVE_INFO_IOUtil_writevMax_0'] = 5078476;
var _NATIVE_INFO_NativeThread_init_0 = Module['_NATIVE_INFO_NativeThread_init_0'] = 5078508;
var _NATIVE_INFO_NativeThread_current0_0 = Module['_NATIVE_INFO_NativeThread_current0_0'] = 5078540;
var _NATIVE_INFO_UnixNativeDispatcher_init_0 = Module['_NATIVE_INFO_UnixNativeDispatcher_init_0'] = 5078572;
var _NATIVE_INFO_UnixNativeDispatcher_getcwd_0 = Module['_NATIVE_INFO_UnixNativeDispatcher_getcwd_0'] = 5078604;
var _NATIVE_INFO_UnixNativeDispatcher_stat0_0 = Module['_NATIVE_INFO_UnixNativeDispatcher_stat0_0'] = 5078636;
var _NATIVE_INFO_UnixNativeDispatcher_open0_0 = Module['_NATIVE_INFO_UnixNativeDispatcher_open0_0'] = 5078668;
var _NATIVE_INFO_UnixFileDispatcherImpl_size0_0 = Module['_NATIVE_INFO_UnixFileDispatcherImpl_size0_0'] = 5078700;
var _NATIVE_INFO_UnixFileDispatcherImpl_allocationGranularity0_0 = Module['_NATIVE_INFO_UnixFileDispatcherImpl_allocationGranularity0_0'] = 5078732;
var _NATIVE_INFO_UnixFileDispatcherImpl_map0_0 = Module['_NATIVE_INFO_UnixFileDispatcherImpl_map0_0'] = 5078764;
var _NATIVE_INFO_UnixFileDispatcherImpl_unmap0_0 = Module['_NATIVE_INFO_UnixFileDispatcherImpl_unmap0_0'] = 5078796;
var _NATIVE_INFO_FileDispatcherImpl_init0_0 = Module['_NATIVE_INFO_FileDispatcherImpl_init0_0'] = 5078828;
function invoke_vi(index,a1) {
  var sp = stackSave();
  try {
    getWasmTableEntry(index)(a1);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_ii(index,a1) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(index)(a1);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iiii(index,a1,a2,a3) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(index)(a1,a2,a3);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iii(index,a1,a2) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(index)(a1,a2);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_v(index) {
  var sp = stackSave();
  try {
    getWasmTableEntry(index)();
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_iiiii(index,a1,a2,a3,a4) {
  var sp = stackSave();
  try {
    return getWasmTableEntry(index)(a1,a2,a3,a4);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}

function invoke_viii(index,a1,a2,a3) {
  var sp = stackSave();
  try {
    getWasmTableEntry(index)(a1,a2,a3);
  } catch(e) {
    stackRestore(sp);
    if (e !== e+0) throw e;
    _setThrew(1, 0);
  }
}


// include: postamble.js
// === Auto-generated postamble setup entry stuff ===

Module['addRunDependency'] = addRunDependency;
Module['removeRunDependency'] = removeRunDependency;
Module['wasmMemory'] = wasmMemory;
Module['wasmTable'] = wasmTable;
Module['ccall'] = ccall;
Module['cwrap'] = cwrap;
Module['addFunction'] = addFunction;
Module['removeFunction'] = removeFunction;
Module['setValue'] = setValue;
Module['getValue'] = getValue;
Module['UTF8ToString'] = UTF8ToString;
Module['FS_createPreloadedFile'] = FS_createPreloadedFile;
Module['FS_unlink'] = FS_unlink;
Module['FS_createPath'] = FS_createPath;
Module['FS'] = FS;
Module['FS_createDataFile'] = FS_createDataFile;
var missingLibrarySymbols = [
  'writeI53ToI64',
  'writeI53ToI64Clamped',
  'writeI53ToI64Signaling',
  'writeI53ToU64Clamped',
  'writeI53ToU64Signaling',
  'convertI32PairToI53',
  'convertI32PairToI53Checked',
  'convertU32PairToI53',
  'getTempRet0',
  'setTempRet0',
  'zeroMemory',
  'strError',
  'inetPton4',
  'inetNtop4',
  'inetPton6',
  'inetNtop6',
  'readSockaddr',
  'writeSockaddr',
  'emscriptenLog',
  'jstoi_q',
  'listenOnce',
  'autoResumeAudioContext',
  'getDynCaller',
  'dynCall',
  'runtimeKeepalivePush',
  'runtimeKeepalivePop',
  'asmjsMangle',
  'mmapAlloc',
  'HandleAllocator',
  'getNativeTypeSize',
  'STACK_SIZE',
  'STACK_ALIGN',
  'POINTER_SIZE',
  'ASSERTIONS',
  'reallyNegative',
  'unSign',
  'strLen',
  'reSign',
  'formatString',
  'intArrayToString',
  'AsciiToString',
  'UTF16ToString',
  'stringToUTF16',
  'lengthBytesUTF16',
  'UTF32ToString',
  'stringToUTF32',
  'lengthBytesUTF32',
  'stringToNewUTF8',
  'registerKeyEventCallback',
  'maybeCStringToJsString',
  'findEventTarget',
  'getBoundingClientRect',
  'fillMouseEventData',
  'registerMouseEventCallback',
  'registerWheelEventCallback',
  'registerUiEventCallback',
  'registerFocusEventCallback',
  'fillDeviceOrientationEventData',
  'registerDeviceOrientationEventCallback',
  'fillDeviceMotionEventData',
  'registerDeviceMotionEventCallback',
  'screenOrientation',
  'fillOrientationChangeEventData',
  'registerOrientationChangeEventCallback',
  'fillFullscreenChangeEventData',
  'registerFullscreenChangeEventCallback',
  'JSEvents_requestFullscreen',
  'JSEvents_resizeCanvasForFullscreen',
  'registerRestoreOldStyle',
  'hideEverythingExceptGivenElement',
  'restoreHiddenElements',
  'setLetterbox',
  'softFullscreenResizeWebGLRenderTarget',
  'doRequestFullscreen',
  'fillPointerlockChangeEventData',
  'registerPointerlockChangeEventCallback',
  'registerPointerlockErrorEventCallback',
  'requestPointerLock',
  'fillVisibilityChangeEventData',
  'registerVisibilityChangeEventCallback',
  'registerTouchEventCallback',
  'fillGamepadEventData',
  'registerGamepadEventCallback',
  'registerBeforeUnloadEventCallback',
  'fillBatteryEventData',
  'battery',
  'registerBatteryEventCallback',
  'setCanvasElementSize',
  'getCanvasElementSize',
  'jsStackTrace',
  'getCallstack',
  'convertPCtoSourceLocation',
  'checkWasiClock',
  'flush_NO_FILESYSTEM',
  'wasiRightsToMuslOFlags',
  'wasiOFlagsToMuslOFlags',
  'safeSetTimeout',
  'setImmediateWrapped',
  'safeRequestAnimationFrame',
  'clearImmediateWrapped',
  'registerPostMainLoop',
  'registerPreMainLoop',
  'getPromise',
  'makePromise',
  'idsToPromises',
  'makePromiseCallback',
  'ExceptionInfo',
  'findMatchingCatch',
  'Browser_asyncPrepareDataCounter',
  'isLeapYear',
  'ydayFromDate',
  'arraySum',
  'addDays',
  'wasmfsNodeConvertNodeCode',
  'wasmfsTry',
  'wasmfsNodeFixStat',
  'wasmfsNodeLstat',
  'wasmfsNodeFstat',
  'FileSystemAsyncAccessHandle',
  'wasmfsOPFSCreateAsyncAccessHandle',
  'wasmfsOPFSProxyFinish',
  'wasmfsOPFSGetOrCreateFile',
  'wasmfsOPFSGetOrCreateDir',
  'heapObjectForWebGLType',
  'toTypedArrayIndex',
  'webgl_enable_ANGLE_instanced_arrays',
  'webgl_enable_OES_vertex_array_object',
  'webgl_enable_WEBGL_draw_buffers',
  'webgl_enable_WEBGL_multi_draw',
  'webgl_enable_EXT_polygon_offset_clamp',
  'webgl_enable_EXT_clip_control',
  'webgl_enable_WEBGL_polygon_mode',
  'emscriptenWebGLGet',
  'computeUnpackAlignedImageSize',
  'colorChannelsInGlTextureFormat',
  'emscriptenWebGLGetTexPixelData',
  'emscriptenWebGLGetUniform',
  'webglGetUniformLocation',
  'webglPrepareUniformLocationsBeforeFirstUse',
  'webglGetLeftBracePos',
  'emscriptenWebGLGetVertexAttrib',
  '__glGetActiveAttribOrUniform',
  'writeGLArray',
  'registerWebGlEventCallback',
  'runAndAbortIfError',
  'ALLOC_NORMAL',
  'ALLOC_STACK',
  'allocate',
  'writeStringToMemory',
  'writeAsciiToMemory',
  'setErrNo',
  'demangle',
  'stackTrace',
];
missingLibrarySymbols.forEach(missingLibrarySymbol)

var unexportedSymbols = [
  'run',
  'addOnPreRun',
  'addOnInit',
  'addOnPreMain',
  'addOnExit',
  'addOnPostRun',
  'out',
  'err',
  'callMain',
  'abort',
  'wasmExports',
  'writeStackCookie',
  'checkStackCookie',
  'readI53FromI64',
  'readI53FromU64',
  'INT53_MAX',
  'INT53_MIN',
  'bigintToI53Checked',
  'stackSave',
  'stackRestore',
  'stackAlloc',
  'ptrToString',
  'exitJS',
  'getHeapMax',
  'growMemory',
  'ENV',
  'ERRNO_CODES',
  'DNS',
  'Protocols',
  'Sockets',
  'timers',
  'warnOnce',
  'readEmAsmArgsArray',
  'readEmAsmArgs',
  'runEmAsmFunction',
  'runMainThreadEmAsm',
  'jstoi_s',
  'getExecutableName',
  'handleException',
  'keepRuntimeAlive',
  'callUserCallback',
  'maybeExit',
  'asyncLoad',
  'alignMemory',
  'noExitRuntime',
  'getCFunc',
  'uleb128Encode',
  'sigToWasmTypes',
  'generateFuncType',
  'convertJsFunctionToWasm',
  'freeTableIndexes',
  'functionsInTableMap',
  'getEmptyTableSlot',
  'updateTableMap',
  'getFunctionAddress',
  'PATH',
  'PATH_FS',
  'UTF8Decoder',
  'UTF8ArrayToString',
  'stringToUTF8Array',
  'stringToUTF8',
  'lengthBytesUTF8',
  'intArrayFromString',
  'stringToAscii',
  'UTF16Decoder',
  'stringToUTF8OnStack',
  'writeArrayToMemory',
  'JSEvents',
  'specialHTMLTargets',
  'findCanvasEventTarget',
  'currentFullscreenStrategy',
  'restoreOldWindowedStyle',
  'UNWIND_CACHE',
  'ExitStatus',
  'getEnvStrings',
  'initRandomFill',
  'randomFill',
  'emSetImmediate',
  'emClearImmediate_deps',
  'emClearImmediate',
  'promiseMap',
  'uncaughtExceptionCount',
  'exceptionLast',
  'exceptionCaught',
  'Browser',
  'getPreloadedImageData__data',
  'wget',
  'MONTH_DAYS_REGULAR',
  'MONTH_DAYS_LEAP',
  'MONTH_DAYS_REGULAR_CUMULATIVE',
  'MONTH_DAYS_LEAP_CUMULATIVE',
  'preloadPlugins',
  'FS_modeStringToFlags',
  'FS_getMode',
  'FS_stdin_getChar_buffer',
  'FS_stdin_getChar',
  'FS_createDevice',
  'FS_readFile',
  'MEMFS',
  'wasmFSPreloadedFiles',
  'wasmFSPreloadedDirs',
  'wasmFSPreloadingFlushed',
  'wasmFSDevices',
  'wasmFSDeviceStreams',
  'FS_mknod',
  'FS_create',
  'FS_writeFile',
  'FS_mkdir',
  'FS_mkdirTree',
  'wasmFS$JSMemoryFiles',
  'wasmFS$backends',
  'wasmfsNodeIsWindows',
  'wasmfsOPFSDirectoryHandles',
  'wasmfsOPFSFileHandles',
  'wasmfsOPFSAccessHandles',
  'wasmfsOPFSBlobs',
  'tempFixedLengthArray',
  'miniTempWebGLFloatBuffers',
  'miniTempWebGLIntBuffers',
  'GL',
  'AL',
  'GLUT',
  'EGL',
  'GLEW',
  'IDBStore',
  'SDL',
  'SDL_gfx',
  'allocateUTF8',
  'allocateUTF8OnStack',
  'print',
  'printErr',
];
unexportedSymbols.forEach(unexportedRuntimeSymbol);



var calledRun;

function callMain() {
  assert(runDependencies == 0, 'cannot call main when async dependencies remain! (listen on Module["onRuntimeInitialized"])');
  assert(__ATPRERUN__.length == 0, 'cannot call main when preRun functions remain to be called');

  var entryFunction = _main;

  var argc = 0;
  var argv = 0;

  try {

    var ret = entryFunction(argc, argv);

    // if we're not running an evented main loop, it's time to exit
    exitJS(ret, /* implicit = */ true);
    return ret;
  } catch (e) {
    return handleException(e);
  }
}

function stackCheckInit() {
  // This is normally called automatically during __wasm_call_ctors but need to
  // get these values before even running any of the ctors so we call it redundantly
  // here.
  _emscripten_stack_init();
  // TODO(sbc): Move writeStackCookie to native to to avoid this.
  writeStackCookie();
}

function run() {

  if (runDependencies > 0) {
    dependenciesFulfilled = run;
    return;
  }

  stackCheckInit();

  preRun();

  // a preRun added a dependency, run will be called later
  if (runDependencies > 0) {
    dependenciesFulfilled = run;
    return;
  }

  function doRun() {
    // run may have just been called through dependencies being fulfilled just in this very frame,
    // or while the async setStatus time below was happening
    assert(!calledRun);
    calledRun = true;
    Module['calledRun'] = true;

    if (ABORT) return;

    initRuntime();

    preMain();

    readyPromiseResolve(Module);
    Module['onRuntimeInitialized']?.();

    var noInitialRun = Module['noInitialRun'];legacyModuleProp('noInitialRun', 'noInitialRun');
    if (!noInitialRun) callMain();

    postRun();
  }

  if (Module['setStatus']) {
    Module['setStatus']('Running...');
    setTimeout(() => {
      setTimeout(() => Module['setStatus'](''), 1);
      doRun();
    }, 1);
  } else
  {
    doRun();
  }
  checkStackCookie();
}

function checkUnflushedContent() {
  // Compiler settings do not allow exiting the runtime, so flushing
  // the streams is not possible. but in ASSERTIONS mode we check
  // if there was something to flush, and if so tell the user they
  // should request that the runtime be exitable.
  // Normally we would not even include flush() at all, but in ASSERTIONS
  // builds we do so just for this check, and here we see if there is any
  // content to flush, that is, we check if there would have been
  // something a non-ASSERTIONS build would have not seen.
  // How we flush the streams depends on whether we are in SYSCALLS_REQUIRE_FILESYSTEM=0
  // mode (which has its own special function for this; otherwise, all
  // the code is inside libc)
  var oldOut = out;
  var oldErr = err;
  var has = false;
  out = err = (x) => {
    has = true;
  }
  try { // it doesn't matter if it fails
    // In WasmFS we must also flush the WasmFS internal buffers, for this check
    // to work.
    _wasmfs_flush();
  } catch(e) {}
  out = oldOut;
  err = oldErr;
  if (has) {
    warnOnce('stdio streams had content in them that was not flushed. you should set EXIT_RUNTIME to 1 (see the Emscripten FAQ), or make sure to emit a newline when you printf etc.');
    warnOnce('(this may also be due to not including full filesystem support - try building with -sFORCE_FILESYSTEM)');
  }
}

if (Module['preInit']) {
  if (typeof Module['preInit'] == 'function') Module['preInit'] = [Module['preInit']];
  while (Module['preInit'].length > 0) {
    Module['preInit'].pop()();
  }
}

run();

// end include: postamble.js

// include: postamble_modularize.js
// In MODULARIZE mode we wrap the generated code in a factory function
// and return either the Module itself, or a promise of the module.
//
// We assign to the `moduleRtn` global here and configure closure to see
// this as and extern so it won't get minified.

moduleRtn = readyPromise;

// Assertion for attempting to access module properties on the incoming
// moduleArg.  In the past we used this object as the prototype of the module
// and assigned properties to it, but now we return a distinct object.  This
// keeps the instance private until it is ready (i.e the promise has been
// resolved).
for (const prop of Object.keys(Module)) {
  if (!(prop in moduleArg)) {
    Object.defineProperty(moduleArg, prop, {
      configurable: true,
      get() {
        abort(`Access to module property ('${prop}') is no longer possible via the module constructor argument; Instead, use the result of the module constructor.`)
      }
    });
  }
}
// end include: postamble_modularize.js



  return moduleRtn;
}
);
})();
(() => {
  // Create a small, never-async wrapper around Module which
  // checks for callers incorrectly using it with `new`.
  var real_Module = Module;
  Module = function(arg) {
    if (new.target) throw new Error("Module() should not be called with `new Module()`");
    return real_Module(arg);
  }
})();
export default Module;
