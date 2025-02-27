#include <natives-dsl.h>

DECLARE_NATIVE("java/io", Console, istty, "()Z") { return (stack_value){.i = 1}; }

DECLARE_NATIVE("java/io", Console, encoding, "()Ljava/lang/String;") { return value_null(); }