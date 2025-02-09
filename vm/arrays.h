//
// Created by alec on 12/20/24.
//

#ifndef BJVM_ARRAYS_H
#define BJVM_ARRAYS_H

#include "bjvm.h"
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

static int constexpr kArrayLengthOffset = sizeof(bjvm_obj_header);
/// Array data starts after the length field -- aligning to the size of a
/// pointer Not sure if it's worth the memory savings to align to the size of
/// the element
static int constexpr kArrayDataOffset =
    ALIGN_UP(sizeof(bjvm_obj_header) + sizeof(int), alignof(bjvm_obj_header *));

static int constexpr kArrayHeaderSize =
    kArrayDataOffset - sizeof(bjvm_obj_header);

#undef ALIGN_UP

static int constexpr kArrayMaxDimensions = 255;

static inline bool Is1DPrimitiveArray(bjvm_obj_header *src) {
  return src->descriptor->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY &&
         src->descriptor->dimensions == 1;
}

static inline bool Is1DReferenceArray(bjvm_obj_header *src) {
  return src->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY &&
         src->descriptor->dimensions == 1;
}

static inline int *ArrayLength(bjvm_obj_header *obj) {
  return (int *)((char *)obj + kArrayLengthOffset);
}

static inline void *ArrayData(bjvm_obj_header *obj) {
  return (char *)obj + kArrayDataOffset;
}

static inline bjvm_obj_header *ReferenceArrayLoad(bjvm_obj_header *array,
                                                  int index) {
  DCHECK(array->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
  DCHECK(index >= 0 && index < *ArrayLength(array));

  return *((bjvm_obj_header **)ArrayData(array) + index);
}

static inline void ReferenceArrayStore(bjvm_obj_header *array, int index,
                                       bjvm_obj_header *val) {
  DCHECK(array->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
  DCHECK(index >= 0 && index < *ArrayLength(array));

  *((bjvm_obj_header **)ArrayData(array) + index) = val;
}

static inline void ByteArrayStoreBlock(object array, s32 offset, s32 length, u8 const *data) {
  DCHECK(Is1DPrimitiveArray(array));
  DCHECK(offset >= 0);
  DCHECK(length >= 0);
  DCHECK(data != nullptr);
  DCHECK((*ArrayLength(array) - offset) <= length);

  memcpy((s8*)ArrayData(array) + offset, data, length);
}

#define MAKE_PRIMITIVE_LOAD_STORE(name, type)                                  \
  static inline type name##ArrayLoad(bjvm_obj_header *array, int index) {      \
    DCHECK(Is1DPrimitiveArray(array));                                         \
    DCHECK(index >= 0 && index < *ArrayLength(array));                         \
    return *((type *)ArrayData(array) + index);                                \
  }                                                                            \
  static inline void name##ArrayStore(bjvm_obj_header *array, int index,       \
                                      type val) {                              \
    DCHECK(Is1DPrimitiveArray(array));                                         \
    DCHECK(index >= 0 && index < *ArrayLength(array));                         \
    *((type *)ArrayData(array) + index) = val;                                 \
  }

MAKE_PRIMITIVE_LOAD_STORE(Byte, s8)
MAKE_PRIMITIVE_LOAD_STORE(Short, s16)
MAKE_PRIMITIVE_LOAD_STORE(Char, u16)
MAKE_PRIMITIVE_LOAD_STORE(Int, s32)
MAKE_PRIMITIVE_LOAD_STORE(Long, s64)
MAKE_PRIMITIVE_LOAD_STORE(Float, float)
MAKE_PRIMITIVE_LOAD_STORE(Double, double)

#undef MAKE_PRIMITIVE_LOAD_STORE

bjvm_classdesc *make_array_classdesc(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc);

bjvm_obj_header *CreateArray(bjvm_thread *thread, bjvm_classdesc *desc,
                             int const *dim_sizes, int total_dimensions);

static inline bjvm_obj_header *
CreateObjectArray1D(bjvm_thread *thread, bjvm_classdesc *inner_type, int size) {
  auto desc = make_array_classdesc(thread, inner_type);
  return CreateArray(thread, desc, &size, 1);
}

static inline bjvm_obj_header *CreatePrimitiveArray1D(bjvm_thread *thread,
                                                      bjvm_type_kind inner_type,
                                                      int count) {
  auto desc = make_array_classdesc(
      thread, bjvm_primitive_classdesc(thread, inner_type));
  return CreateArray(thread, desc, &count, 1);
}

static inline bjvm_obj_header *CreateByteArray(bjvm_thread *thread,
                                               u8 *data, int length) {
  bjvm_obj_header *result =
      CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, length);
  if (!result)
    return nullptr;
  memcpy(ArrayData(result), data, length);
  return result;
}

#ifdef __cplusplus
}
#endif

#endif // BJVM_ARRAYS_H
