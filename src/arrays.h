//
// Created by alec on 12/20/24.
//

#ifndef BJVM_ARRAYS_H
#define BJVM_ARRAYS_H

#include "bjvm.h"

#define ALIGN_UP(x, align) (((x) + (align)-1) & ~((align)-1))

static int constexpr kArrayLengthOffset = sizeof(bjvm_obj_header);
/// Array data starts after the length field -- aligning to the size of a pointer
/// Not sure if it's worth the memory savings to align to the size of the element
static int constexpr kArrayDataOffset = ALIGN_UP(sizeof(bjvm_obj_header) + sizeof(int), sizeof(bjvm_obj_header *));

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

static inline bjvm_obj_header *ReferenceArrayLoad(bjvm_obj_header *array, int index) {
	assert(Is1DReferenceArray(array));
	assert(index >= 0 && index < *ArrayLength(array));

	return *((bjvm_obj_header **)ArrayData(array) + index);
}

bjvm_classdesc *make_array_classdesc(bjvm_thread *thread,
									 bjvm_classdesc *classdesc);

bjvm_obj_header *CreateArray(bjvm_thread *thread, bjvm_classdesc *desc, int *dim_sizes, int dims);

static inline bjvm_obj_header *CreateObjectArray1D(bjvm_thread *thread, bjvm_classdesc *desc, int size) {
	return CreateArray(thread, desc, &size, 1);
}

static inline bjvm_obj_header *CreatePrimitiveArray1D(bjvm_thread *thread, bjvm_type_kind array_type, int count) {
	return CreateArray(thread, bjvm_primitive_classdesc(thread, array_type), &count, 1);
}

#endif //BJVM_ARRAYS_H
