#include "objects.h"

uint64_t hash_code_rng = 0;

__attribute__((no_sanitize("unsigned-integer-overflow")))
uint64_t ObjNextHashCode() {
  hash_code_rng = hash_code_rng * 0x5DEECE66D + 0xB;
  return hash_code_rng >> 32;
}

bjvm_obj_header *MakeJavaStringSlice(bjvm_thread *thread, bjvm_utf8 slice) {
  return make_string(thread, slice);
}