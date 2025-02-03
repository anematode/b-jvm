
A JVM for the web. Bring back the applet!

Message me on Discord if you are interested in collaborating: forevermilk#0001


## Style guide

Always use `s64` instead of `long` because Emscripten has `sizeof(long) == 4`. `int` can be assumed to be 32 bits.

Place all native functions in the corresponding folder in `src/natives`, and use the `DECLARE_NATIVE` macro to link them.

Common abbreviations:

- `cf` – classfile
- `cp` – constant pool
- `bc` – bytecode
- `vm` – virtual machine

## Tasks

See GitHub issues

## JITing to WebAssembly

- General method signature: `bjvm_interpreter_result_t (*compiled)(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *result, ... args)`
- Generated on the fly for each argument type combination
- Longs are split into two ints to avoid overhead of creating `bigint`s
- Method is responsible for setting up its own stack frame and in the case of de-opt or interruption, generating all interpreter stack frames
- Methods which are not compiled will have a generated implementation that delegates appropriately to the interpreter

### Goals

- Full implementation of the JVM spec (including esoteric things like classfile verification)
- Java 17 support (starting with Java 8)
- Reasonably fast (geomean within factor of 10 of HotSpot in memory and CPU usage across benchmarks)
- JIT compilation to WebAssembly
- Interruptable execution
- Dynamic class loading
- Mild JNI compatibility

### Non-goals

- Desktop JVM -- only useful for testing/debugging
- Swing, AWT, etc. compatibility
- Beautiful, elegant code (see QuickJS for that)

### Useful

```
clang-format -i test/*.cc src/**/*.c src/**/*.h
```

## Documentation

### Overloaded Java functions

When overloaded functions are compiled into JS, overloads with the same number of arguments

Variadic functions are not yet supported; the variadic argument is 