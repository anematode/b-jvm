const bjvm = require("../build/bjvm_main.js");

// wait for wasm to be ready
bjvm.onRuntimeInitialized = function() {
    console.log(bjvm);

    // ccall set_up
    const ptr = bjvm.ccall("set_up", "number", [], []);
    function main() {
        if (bjvm.ccall("bjvm_async_run_step", "number", ["number"], [ptr]) == 1) {
            return;
        }

        console.log("Control flow returned to JS!");

        setTimeout(main, 0);
    }

    main();
};

