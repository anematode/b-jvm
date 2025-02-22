#include <natives-dsl.h>

DECLARE_NATIVE("jdk/internal/misc", VM, initialize, "()V") { return value_null(); }

DECLARE_NATIVE("jdk/internal/misc", PreviewFeatures, isPreviewEnabled, "()Z") { return (stack_value){.i = 1}; }