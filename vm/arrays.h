//
// Created by alec on 12/20/24.
//

#ifndef ARRAYS_H
#define ARRAYS_H

#include "bjvm.h"
#include <stdint.h>
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static int constexpr kArrayLengthOffset = sizeof(obj_header);
/// Array data starts after the length field -- aligning to the size of a
/// pointer Not sure if it's worth the memory savings to align to the size of
/// the element
static int constexpr kArrayDataOffset = ALIGN_UP(sizeof(obj_header) + sizeof(int), alignof(max_align_t));

static int constexpr kArrayHeaderSize = kArrayDataOffset - sizeof(obj_header);

#undef ALIGN_UP

static int constexpr kArrayMaxDimensions = 255;

static inline bool Is1DPrimitiveArray(obj_header *src) {
  return src->descriptor->kind == CD_KIND_PRIMITIVE_ARRAY && src->descriptor->dimensions == 1;
}

static inline bool Is1DReferenceArray(obj_header *src) {
  return src->descriptor->kind == CD_KIND_ORDINARY_ARRAY && src->descriptor->dimensions == 1;
}

static inline int ArrayLength(obj_header *obj) { return *(int *)((char *)obj + kArrayLengthOffset); }

static inline void *ArrayData(obj_header *obj) { return (char *)obj + kArrayDataOffset; }

static inline obj_header *ReferenceArrayLoad(obj_header *array, int index) {
  DCHECK(array->descriptor->kind == CD_KIND_ORDINARY_ARRAY);
  DCHECK(index >= 0 && index < ArrayLength(array));

  return *((obj_header **)ArrayData(array) + index);
}

static inline void ReferenceArrayStore(obj_header *array, int index, obj_header *val) {
  DCHECK(array->descriptor->kind == CD_KIND_ORDINARY_ARRAY);
  DCHECK(index >= 0 && index < ArrayLength(array));

  *((obj_header **)ArrayData(array) + index) = val;
}

static inline void ByteArrayStoreBlock(object array, s32 offset, s32 length, u8 const *data) {
  DCHECK(Is1DPrimitiveArray(array));
  DCHECK(offset >= 0);
  DCHECK(length >= 0);
  DCHECK(data != nullptr);
  DCHECK((ArrayLength(array) - offset) <= length);

  memcpy((s8 *)ArrayData(array) + offset, data, length);
}

#define MAKE_PRIMITIVE_LOAD_STORE(name, type)                                                                          \
  static inline type name##ArrayLoad(obj_header *array, int index) {                                                   \
    DCHECK(Is1DPrimitiveArray(array));                                                                                 \
    DCHECK(index >= 0 && index < ArrayLength(array));                                                                  \
    return *((type *)ArrayData(array) + index);                                                                        \
  }                                                                                                                    \
  static inline void name##ArrayStore(obj_header *array, int index, type val) {                                        \
    DCHECK(Is1DPrimitiveArray(array));                                                                                 \
    DCHECK(index >= 0 && index < ArrayLength(array));                                                                  \
    *((type *)ArrayData(array) + index) = val;                                                                         \
  }

MAKE_PRIMITIVE_LOAD_STORE(Byte, s8)
MAKE_PRIMITIVE_LOAD_STORE(Short, s16)
MAKE_PRIMITIVE_LOAD_STORE(Char, u16)
MAKE_PRIMITIVE_LOAD_STORE(Int, s32)
MAKE_PRIMITIVE_LOAD_STORE(Long, s64)
MAKE_PRIMITIVE_LOAD_STORE(Float, float)
MAKE_PRIMITIVE_LOAD_STORE(Double, double)

#undef MAKE_PRIMITIVE_LOAD_STORE

classdesc *make_array_classdesc(vm_thread *thread, classdesc *classdesc);

obj_header *CreateArray(vm_thread *thread, classdesc *desc, int const *dim_sizes, int total_dimensions);

__attribute__((noinline)) obj_header *CreateObjectArray1D(vm_thread *thread, classdesc *inner_type, int size);

obj_header *CreatePrimitiveArray1D(vm_thread *thread, type_kind inner_type, int count);

obj_header *CreateByteArray(vm_thread *thread, u8 *data, int length);

#ifdef __cplusplus
}
#endif

#endif // ARRAYS_H
