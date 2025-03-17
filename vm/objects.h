#include "bjvm.h"
#include "cached_classdescs.h"

#include <gc.h>

#ifndef OBJECTS_H
#define OBJECTS_H

#ifdef __cplusplus
extern "C" {
#endif

u64 ObjNextHashCode();

typedef enum : s32 { STRING_CODER_LATIN1 = 0, STRING_CODER_UTF16 = 1 } string_coder_kind;

obj_header *MakeJStringFromModifiedUTF8(vm_thread *thread, slice data, bool intern);
obj_header *MakeJStringFromCString(vm_thread *thread, char const *data, bool intern);
obj_header *MakeJStringFromData(vm_thread *thread, slice data, string_coder_kind encoding);
obj_header *InternJString(vm_thread *thread, obj_header *str);

/// Helper for java.lang.String#length
static inline int JavaStringLength(vm_thread *thread, obj_header *string) {
  DCHECK(utf8_equals(string->descriptor->name, "java/lang/String"));

  auto method = method_lookup(string->descriptor, STR("length"), STR("()I"), false, false);
  stack_value args[1];
  stack_value result = call_interpreter_synchronous(thread, method, args);

  return result.i;
}

/// Extracts the inner array of the given java.lang.String.
/// The data are either UTF-16 or latin1 encoded.
static inline obj_header *RawStringData([[maybe_unused]] vm_thread const *thread, obj_header const *string) {
  DCHECK(utf8_equals(string->descriptor->name, "java/lang/String"));
  return ((struct native_String *)string)->value;
}

static inline object AllocateObject(vm_thread *thread, classdesc *descriptor, size_t allocation_size) {
  DCHECK(descriptor);
  DCHECK(descriptor->state >= CD_STATE_LINKED); // important to know the size
  printf("allocating an object of %.*s ! \n", descriptor->name.len, descriptor->name.chars);
  object obj = (object)bump_allocate(thread, allocation_size);
  if (obj) {
    obj->header_word.expanded_data = (monitor_data *)(uintptr_t)IS_MARK_WORD;
    obj->descriptor = descriptor;
    DCHECK(size_of_object(obj) <= allocation_size);
  }
  return obj;
}

// Get the hash code of the object, computing it if it is not already computed.
s32 get_object_hash_code(vm *vm, object o);

[[maybe_unused]] static void __obj_store_field(obj_header *thing, slice field_name, stack_value value, slice desc) {
  cp_field *field = field_lookup(thing->descriptor, field_name, desc);
  DCHECK(field);
  DCHECK(!(field->access_flags & ACCESS_STATIC));
  set_field(thing, field, value);
}

[[maybe_unused]] static stack_value __obj_load_field(obj_header *thing, slice field_name, slice desc) {
  cp_field *field = field_lookup(thing->descriptor, field_name, desc);
  DCHECK(field);
  DCHECK(!(field->access_flags & ACCESS_STATIC));
  return get_field(thing, field);
}

[[maybe_unused]] static object __LoadFieldObject(obj_header *thing, slice desc, slice name) {
  return __obj_load_field(thing, name, desc).obj;
}

[[maybe_unused]] static void __StoreFieldObject(obj_header *thing, slice desc, slice name, object value) {
  __obj_store_field(thing, name, (stack_value){.obj = value}, desc);
}

[[maybe_unused]] static void __StoreStaticFieldObject(classdesc *clazz, slice desc, slice name, object value) {
  cp_field *field = field_lookup(clazz, name, desc);
  DCHECK(field);
  DCHECK(field->access_flags & ACCESS_STATIC);
  set_static_field(field, (stack_value){.obj = value});
}

#define StoreFieldObject(obj, type, name, value) __StoreFieldObject(obj, STR("L" type ";"), STR(name), value)
#define StoreStaticFieldObject(obj, type, name, value)                                                                 \
  __StoreStaticFieldObject(obj, STR("L" type ";"), STR(name), value)
#define LoadFieldObject(obj, type, name) __LoadFieldObject(obj, STR("L" type ";"), STR(name))

#define GeneratePrimitiveStoreField(type_cap, type, stack_field, desc, modifier)                                       \
  [[maybe_unused]] static void __StoreField##type_cap(obj_header *thing, slice name, type value) {                     \
    __obj_store_field(thing, name, (stack_value){.stack_field = value modifier}, STR(#desc));                          \
  }

#define GeneratePrimitiveLoadField(type_cap, type, stack_field, desc)                                                  \
  [[maybe_unused]] static type __LoadField##type_cap(obj_header *thing, slice name) {                                  \
    return __obj_load_field(thing, name, STR(#desc)).stack_field;                                                      \
  }

GeneratePrimitiveStoreField(Byte, jbyte, i, B, );
GeneratePrimitiveStoreField(Char, jchar, i, C, );
GeneratePrimitiveStoreField(Int, jint, i, I, );
GeneratePrimitiveStoreField(Long, jlong, l, J, );
GeneratePrimitiveStoreField(Float, jfloat, f, F, );
GeneratePrimitiveStoreField(Double, jdouble, d, D, );
GeneratePrimitiveStoreField(Boolean, jboolean, i, Z, &1);

GeneratePrimitiveLoadField(Byte, jbyte, i, B);
GeneratePrimitiveLoadField(Char, jchar, i, C);
GeneratePrimitiveLoadField(Int, jint, i, I);
GeneratePrimitiveLoadField(Long, jlong, l, J);
GeneratePrimitiveLoadField(Float, jfloat, f, F);
GeneratePrimitiveLoadField(Double, jdouble, d, D);
GeneratePrimitiveLoadField(Boolean, jboolean, i, Z);

#define StoreFieldByte(obj, name, value) __StoreFieldByte(obj, STR(name), value)
#define StoreFieldChar(obj, name, value) __StoreFieldChar(obj, STR(name), value)
#define StoreFieldInt(obj, name, value) __StoreFieldInt(obj, STR(name), value)
#define StoreFieldLong(obj, name, value) __StoreFieldLong(obj, STR(name), value)
#define StoreFieldFloat(obj, name, value) __StoreFieldFloat(obj, STR(name), value)
#define StoreFieldDouble(obj, name, value) __StoreFieldDouble(obj, STR(name), value)
#define StoreFieldBoolean(obj, name, value) __StoreFieldBoolean(obj, STR(name), value)

#define LoadFieldByte(obj, name) __LoadFieldByte(obj, STR(name))
#define LoadFieldChar(obj, name) __LoadFieldChar(obj, STR(name))
#define LoadFieldInt(obj, name) __LoadFieldInt(obj, STR(name))
#define LoadFieldLong(obj, name) __LoadFieldLong(obj, STR(name))
#define LoadFieldFloat(obj, name) __LoadFieldFloat(obj, STR(name))
#define LoadFieldDouble(obj, name) __LoadFieldDouble(obj, STR(name))
#define LoadFieldBoolean(obj, name) __LoadFieldBoolean(obj, STR(name))

#undef GenerateStoreField
#undef GeneratePrimitiveLoadField

#ifdef __cplusplus
}
#endif

#endif