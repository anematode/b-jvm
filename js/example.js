const bjvm = require("../build/bjvm_main.js");

// wait for wasm to be ready
bjvm.onRuntimeInitialized = function() {

    // ccall set_up
    const ptr = bjvm.ccall("set_up", "number", [], []);
    function main() {
        while (!bjvm.ccall("bjvm_async_run_step", "number", ["number"], [ptr])) {
        }
    }

    main();
};

