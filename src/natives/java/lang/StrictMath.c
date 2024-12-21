#include <math.h>
#include <natives.h>

DECLARE_NATIVE("java/lang", StrictMath, log, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = log(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, sin, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = sin(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, cos, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = cos(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, tan, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = tan(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, asin, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = asin(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, acos, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = acos(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, atan, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = atan(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, exp, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = exp(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, log10, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = log10(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, sqrt, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = sqrt(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, cbrt, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = cbrt(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, sinh, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = sinh(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, cosh, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = cosh(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, tanh, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = tanh(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, expm1, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = expm1(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, log1p, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = log1p(args[0].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, atan2, "(DD)D") {
  assert(argc == 2);
  return (bjvm_stack_value){.d = atan2(args[0].d, args[1].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, IEEEremainder, "(DD)D") {
  assert(argc == 2);
  return (bjvm_stack_value){.d = remainder(args[0].d, args[1].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, pow, "(DD)D") {
  assert(argc == 2);
  return (bjvm_stack_value){.d = pow(args[0].d, args[1].d)};
}

DECLARE_NATIVE("java/lang", StrictMath, hypot, "(DD)D") {
  assert(argc == 2);
  return (bjvm_stack_value){.d = hypot(args[0].d, args[1].d)};
}
