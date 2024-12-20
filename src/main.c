//
// Created by Cowpox on 12/10/24.
//

#include <stdio.h>

#include "bjvm.h"
#include "natives.h"

int main() {
	bjvm_native_t const* const (*natives);

	size_t cnt = bjvm_get_natives_list(&natives);
	printf("Found %zu natives\n", cnt);
	return 0;
}