//
// Created by alec on 12/19/24.
//

#include "natives.h"

DECLARE_NATIVE(java/lang, Object, getClass, "()Ljava/lang/Class;") {
  return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(vm, obj->descriptor)};
}

size_t bjvm_get_natives_list(bjvm_native_t const* const (*natives[])) {
#ifdef __APPLE__
	extern const bjvm_native_t * __start___DATA__native[];
	extern const bjvm_native_t * __stop___DATA__native[];

	auto start = __start___DATA__native;
	auto end = __stop___DATA__native;
#else
	bjvm_native_t const *const *start = (bjvm_native_t const *const *)__start_natives;
	bjvm_native_t const *const *end = (bjvm_native_t const *const *)__stop_natives;
#endif

	*natives = __start___DATA__native;

	return end - start;
}