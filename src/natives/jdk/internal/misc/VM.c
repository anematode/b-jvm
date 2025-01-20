#include <natives.h>

DECLARE_NATIVE("jdk/internal/misc", VM, initialize, "()V") { return value_null(); }

DECLARE_NATIVE("jdk/internal/misc", PreviewFeatures, isPreviewEnabled, "()Z") {
  return (bjvm_stack_value){.i = 0}; // :(
}