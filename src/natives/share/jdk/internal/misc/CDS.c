#include <natives-dsl.h>

DECLARE_NATIVE("jdk/internal/misc", CDS, isDumpingClassList0, "()Z") { return (bjvm_stack_value) { .i = 0 }; }
DECLARE_NATIVE("jdk/internal/misc", CDS, isDumpingArchive0, "()Z") { return (bjvm_stack_value) { .i = 0 }; }
DECLARE_NATIVE("jdk/internal/misc", CDS, isSharingEnabled0, "()Z") { return (bjvm_stack_value) { .i = 0 }; }
DECLARE_NATIVE("jdk/internal/misc", CDS, getRandomSeedForDumping, "()J") { return (bjvm_stack_value) { .l = 0 }; }

DECLARE_NATIVE("jdk/internal/misc", CDS, initializeFromArchive, "(Ljava/lang/Class;)V") { return value_null(); }
