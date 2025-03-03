// Dynamically link in faster implementations of fma and fmaf if supported by the runtime.

#ifdef EMSCRIPTEN

#include "wasm_utils.h"
#include "math.h"
#include <emscripten.h>

/**
 Bytes below are:
 (module
  (func (export "fma") (param f64 f64 f64) (result f64)
    local.get 0
    f64x2.splat
    local.get 1
    f64x2.splat
    local.get 2
    f64x2.splat
    f64x2.relaxed_madd
    f64x2.extract_lane 0)
  (func (export "fmaf") (param f32 f32 f32) (result f32)
    local.get 0
    f32x4.splat
    local.get 1
    f32x4.splat
    local.get 2
    f32x4.splat
    f32x4.relaxed_madd
    f32x4.extract_lane 0))
*/

wasm_fmaf_handle wasm_fmaf_impl;
wasm_fma_handle wasm_fma_impl;

void wasm_init_fma_handles() {
  if (wasm_fma_impl != nullptr)
    return;

  int failed = EM_ASM_INT({
    try {
      const module_wasm = new Uint8Array([
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x02, 0x60,
        0x03, 0x7c, 0x7c, 0x7c, 0x01, 0x7c, 0x60, 0x03, 0x7d, 0x7d, 0x7d, 0x01,
        0x7d, 0x03, 0x03, 0x02, 0x00, 0x01, 0x07, 0x0e, 0x02, 0x03, 0x66, 0x6d,
        0x61, 0x00, 0x00, 0x04, 0x66, 0x6d, 0x61, 0x66, 0x00, 0x01, 0x0a, 0x2b,
        0x02, 0x14, 0x00, 0x20, 0x00, 0xfd, 0x14, 0x20, 0x01, 0xfd, 0x14, 0x20,
        0x02, 0xfd, 0x14, 0xfd, 0x87, 0x02, 0xfd, 0x21, 0x00, 0x0b, 0x14, 0x00,
        0x20, 0x00, 0xfd, 0x13, 0x20, 0x01, 0xfd, 0x13, 0x20, 0x02, 0xfd, 0x13,
        0xfd, 0x85, 0x02, 0xfd, 0x1f, 0x00, 0x0b, 0x00, 0x0c, 0x04, 0x6e, 0x61,
        0x6d, 0x65, 0x02, 0x05, 0x02, 0x00, 0x00, 0x01, 0x00
      ]);
      const module = new WebAssembly.Module(module_wasm);
      const instance = new WebAssembly.Instance(module);
      const fma = addFunction(instance.exports.fma, 'dddd');
      const fmaf = addFunction(instance.exports.fmaf, 'ffff');
      HEAP32[$0 >> 2] = fma;
      HEAP32[$1 >> 2] = fmaf;
    } catch (e) {
      return 1;
    }
  }, &wasm_fma_impl, &wasm_fmaf_impl);

  if (!failed) { // Now check that we're on a system which actually supports FMA
    DCHECK(wasm_fma_impl)
    DCHECK(wasm_fmaf_impl)

    double a = 0x1.0000000001p0;
    double b = 0x1.0000000001p0;
    double c = -0x1.0000000002p0;
    if (wasm_fma_impl(a, b, c) != 0x1p-80) {  // sad
      failed = 1;
    }
  }

  if (failed) {  // fall back to software implementation
    wasm_fma_impl = &fma;
    wasm_fmaf_impl = &fmaf;
  }
}
#endif