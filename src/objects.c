#include "objects.h"

uint64_t hash_code_rng = 0;
uint64_t ObjNextHashCode() {
	hash_code_rng = hash_code_rng * 0x5DEECE66D + 0xB;
	return hash_code_rng >> 32;
}