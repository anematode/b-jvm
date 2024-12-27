const bjvm = require("../build/bjvm_main.js");
// get command line argument from node.js
const args = process.argv.slice(2);

// wait for wasm to be ready
bjvm.onRuntimeInitialized = function() {
    bjvm.ccall("set_max_calls", "number", ["number"], [Number.parseInt(args[0])]);

    // ccall set_up
    const ptr = bjvm.ccall("set_up", "number", [], []);
    function main() {
        while (!bjvm.ccall("bjvm_async_run_step", "number", ["number"], [ptr])) {
        }
        bjvm.ccall("print_error", "number", ["number"], [ptr]);
    }

    main();
};

