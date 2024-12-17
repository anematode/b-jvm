
A JVM for the web.

## Style guide

Prefix all non-`static` functions with `bjvm_`.

Always use `int64_t` instead of `long` because Emscripten has `sizeof(long) == 4`. `int` can be assumed to be 32 bits.

Common abbreviations:

- `cf` – classfile
- `cp` – constant pool
- `bc` – bytecode
- `vm` – virtual machine

### Goals

- Full implementation of the JVM spec (including esoteric things like classfile verification)
- Java 17 support (starting with Java 8)
- Reasonably fast (geomean within factor of 10 of HotSpot in memory and CPU usage across benchmarks)
- JIT compilation to JavaScript
- Interruptable execution
- Dynamic class loading
- Mild JNI compatibility

### Non-goals

- Desktop JVM -- only useful for testing/debugging
- Swing, AWT, etc. compatibility
- Beautiful, elegant code (see QuickJS for that)

### Useful

```
clang-format -i test/*.cc src/*.c src/*.h
```