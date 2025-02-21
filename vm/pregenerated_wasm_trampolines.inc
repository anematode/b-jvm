static s32 jit_tramp_I(s32 (*f)(vm_thread *, cp_method * ), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method);
}
static s32 interpreter_tramp_I(vm_thread *thread, cp_method *method ) {
  stack_value values[0];
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_II(s32 (*f)(vm_thread *, cp_method *,  s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i);
}
static s32 interpreter_tramp_II(vm_thread *thread, cp_method *method,  s32 arg0) {
  stack_value values[3];
  values[0].i = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_III(s32 (*f)(vm_thread *, cp_method *,  s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i);
}
static s32 interpreter_tramp_III(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_V(void (*f)(vm_thread *, cp_method * ), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method);
}
static void interpreter_tramp_V(vm_thread *thread, cp_method *method ) {
  stack_value values[0];
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VI(void (*f)(vm_thread *, cp_method *,  s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i);
}
static void interpreter_tramp_VI(vm_thread *thread, cp_method *method,  s32 arg0) {
  stack_value values[3];
  values[0].i = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i);
}
static s32 interpreter_tramp_IIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VII(void (*f)(vm_thread *, cp_method *,  s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i);
}
static void interpreter_tramp_VII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i);
}
static void interpreter_tramp_VIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i);
}
static s32 interpreter_tramp_IIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i);
}
static void interpreter_tramp_VIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_J(s64 (*f)(vm_thread *, cp_method * ), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method);
}
static s64 interpreter_tramp_J(vm_thread *thread, cp_method *method ) {
  stack_value values[0];
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i);
}
static s32 interpreter_tramp_IIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIJI(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i);
}
static s32 interpreter_tramp_IIJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i);
}
static s32 interpreter_tramp_IIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJ(s32 (*f)(vm_thread *, cp_method *,  s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l);
}
static s32 interpreter_tramp_IJ(vm_thread *thread, cp_method *method,  s64 arg0) {
  stack_value values[3];
  values[0].l = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JI(s64 (*f)(vm_thread *, cp_method *,  s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i);
}
static s64 interpreter_tramp_JI(vm_thread *thread, cp_method *method,  s32 arg0) {
  stack_value values[3];
  values[0].i = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l);
}
static s32 interpreter_tramp_IIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i);
}
static void interpreter_tramp_VIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DD(double (*f)(vm_thread *, cp_method *,  double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d);
}
static double interpreter_tramp_DD(vm_thread *thread, cp_method *method,  double arg0) {
  stack_value values[6];
  values[0].d = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VJ(void (*f)(vm_thread *, cp_method *,  s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l);
}
static void interpreter_tramp_VJ(vm_thread *thread, cp_method *method,  s64 arg0) {
  stack_value values[3];
  values[0].l = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i);
}
static s32 interpreter_tramp_IIJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l);
}
static s32 interpreter_tramp_IIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JJ(s64 (*f)(vm_thread *, cp_method *,  s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l);
}
static s64 interpreter_tramp_JJ(vm_thread *thread, cp_method *method,  s64 arg0) {
  stack_value values[3];
  values[0].l = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JJJ(s64 (*f)(vm_thread *, cp_method *,  s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l);
}
static s64 interpreter_tramp_JJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIJI(void (*f)(vm_thread *, cp_method *,  s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i);
}
static void interpreter_tramp_VIJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i);
}
static void interpreter_tramp_VIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJI(s32 (*f)(vm_thread *, cp_method *,  s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i);
}
static s32 interpreter_tramp_IJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l);
}
static s64 interpreter_tramp_JIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IF(s32 (*f)(vm_thread *, cp_method *,  float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f);
}
static s32 interpreter_tramp_IF(vm_thread *thread, cp_method *method,  float arg0) {
  stack_value values[5];
  values[0].f = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_ID(s32 (*f)(vm_thread *, cp_method *,  double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d);
}
static s32 interpreter_tramp_ID(vm_thread *thread, cp_method *method,  double arg0) {
  stack_value values[6];
  values[0].d = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JII(s64 (*f)(vm_thread *, cp_method *,  s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i);
}
static s64 interpreter_tramp_JII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s32 interpreter_tramp_IIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJI(void (*f)(vm_thread *, cp_method *,  s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i);
}
static void interpreter_tramp_VJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l);
}
static s64 interpreter_tramp_JIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static float jit_tramp_F(float (*f)(vm_thread *, cp_method * ), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method);
}
static float interpreter_tramp_F(vm_thread *thread, cp_method *method ) {
  stack_value values[0];
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s64 jit_tramp_JJI(s64 (*f)(vm_thread *, cp_method *,  s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i);
}
static s64 interpreter_tramp_JJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IJJ(s32 (*f)(vm_thread *, cp_method *,  s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l);
}
static s32 interpreter_tramp_IJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_D(double (*f)(vm_thread *, cp_method * ), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method);
}
static double interpreter_tramp_D(vm_thread *thread, cp_method *method ) {
  stack_value values[0];
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static double jit_tramp_DDD(double (*f)(vm_thread *, cp_method *,  double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d);
}
static double interpreter_tramp_DDD(vm_thread *thread, cp_method *method,  double arg0, double arg1) {
  stack_value values[14];
  values[0].d = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i);
}
static s64 interpreter_tramp_JIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i);
}
static s32 interpreter_tramp_IIIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIJJ(void (*f)(vm_thread *, cp_method *,  s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].l);
}
static void interpreter_tramp_VIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i);
}
static void interpreter_tramp_VIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJJI(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i);
}
static s32 interpreter_tramp_IJJI(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i);
}
static void interpreter_tramp_VIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VJJ(void (*f)(vm_thread *, cp_method *,  s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].l);
}
static void interpreter_tramp_VJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1) {
  stack_value values[8];
  values[0].l = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FI(float (*f)(vm_thread *, cp_method *,  s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i);
}
static float interpreter_tramp_FI(vm_thread *thread, cp_method *method,  s32 arg0) {
  stack_value values[3];
  values[0].i = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i);
}
static s32 interpreter_tramp_IIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJII(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i);
}
static s32 interpreter_tramp_IJII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DI(double (*f)(vm_thread *, cp_method *,  s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i);
}
static double interpreter_tramp_DI(vm_thread *thread, cp_method *method,  s32 arg0) {
  stack_value values[3];
  values[0].i = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIJ(s64 (*f)(vm_thread *, cp_method *,  s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l);
}
static s64 interpreter_tramp_JIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIJ(void (*f)(vm_thread *, cp_method *,  s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l);
}
static void interpreter_tramp_VIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FIIJF(float (*f)(vm_thread *, cp_method *,  s32, s32, s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].f);
}
static float interpreter_tramp_FIIJF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, float arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static double jit_tramp_DIIJD(double (*f)(vm_thread *, cp_method *,  s32, s32, s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].d);
}
static double interpreter_tramp_DIIJD(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, double arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IID(s32 (*f)(vm_thread *, cp_method *,  s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d);
}
static s32 interpreter_tramp_IID(vm_thread *thread, cp_method *method,  s32 arg0, double arg1) {
  stack_value values[11];
  values[0].i = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIID(s32 (*f)(vm_thread *, cp_method *,  s32, s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].d);
}
static s32 interpreter_tramp_IIID(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIF(s32 (*f)(vm_thread *, cp_method *,  s32, s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].f);
}
static s32 interpreter_tramp_IIIF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIF(s32 (*f)(vm_thread *, cp_method *,  s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].f);
}
static s32 interpreter_tramp_IIF(vm_thread *thread, cp_method *method,  s32 arg0, float arg1) {
  stack_value values[10];
  values[0].i = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JJII(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i);
}
static s64 interpreter_tramp_JJII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static float jit_tramp_FF(float (*f)(vm_thread *, cp_method *,  float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f);
}
static float interpreter_tramp_FF(vm_thread *thread, cp_method *method,  float arg0) {
  stack_value values[5];
  values[0].f = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VIJII(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i);
}
static void interpreter_tramp_VIJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIJFF(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].f, args[4].f);
}
static s32 interpreter_tramp_IIIJFF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, float arg3, float arg4) {
  stack_value values[27];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].f = arg3;
  values[4].f = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i);
}
static s32 interpreter_tramp_IIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8) {
  stack_value values[43];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIJJJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].l);
}
static s32 interpreter_tramp_IIIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIJDD(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].d, args[4].d);
}
static s32 interpreter_tramp_IIIJDD(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, double arg3, double arg4) {
  stack_value values[29];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].d = arg3;
  values[4].d = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i);
}
static s64 interpreter_tramp_JIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIJJ(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l);
}
static s32 interpreter_tramp_IIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FFF(float (*f)(vm_thread *, cp_method *,  float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].f);
}
static float interpreter_tramp_FFF(vm_thread *thread, cp_method *method,  float arg0, float arg1) {
  stack_value values[12];
  values[0].f = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VJII(void (*f)(vm_thread *, cp_method *,  s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i, args[2].i);
}
static void interpreter_tramp_VJII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIF(void (*f)(vm_thread *, cp_method *,  s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].f);
}
static void interpreter_tramp_VIF(vm_thread *thread, cp_method *method,  s32 arg0, float arg1) {
  stack_value values[10];
  values[0].i = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i);
}
static void interpreter_tramp_VIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIIJ(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l);
}
static s64 interpreter_tramp_JIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIJIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s32 interpreter_tramp_IIIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FIIJ(float (*f)(vm_thread *, cp_method *,  s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l);
}
static float interpreter_tramp_FIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VIJJI(void (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i);
}
static void interpreter_tramp_VIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIJJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l);
}
static void interpreter_tramp_VIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIJD(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].d);
}
static void interpreter_tramp_VIIJD(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, double arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIJIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l);
}
static s32 interpreter_tramp_IIJIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIJF(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].f);
}
static void interpreter_tramp_VIIJF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, float arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DIIJ(double (*f)(vm_thread *, cp_method *,  s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l);
}
static double interpreter_tramp_DIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIJI(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i);
}
static s64 interpreter_tramp_JIJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIIJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l);
}
static void interpreter_tramp_VIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJIII(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i);
}
static s32 interpreter_tramp_IJIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIJJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].i);
}
static s64 interpreter_tramp_JIIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIIJII(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i);
}
static void interpreter_tramp_VIIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i);
}
static void interpreter_tramp_VIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8) {
  stack_value values[43];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIJJJ(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].l);
}
static s32 interpreter_tramp_IIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VD(void (*f)(vm_thread *, cp_method *,  double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].d);
}
static void interpreter_tramp_VD(vm_thread *thread, cp_method *method,  double arg0) {
  stack_value values[6];
  values[0].d = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIIJJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].l);
}
static s64 interpreter_tramp_JIIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DIJD(double (*f)(vm_thread *, cp_method *,  s32, s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].d);
}
static double interpreter_tramp_DIJD(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, double arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VIJIJJ(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l, args[4].l);
}
static void interpreter_tramp_VIJIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIJF(void (*f)(vm_thread *, cp_method *,  s32, s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].f);
}
static void interpreter_tramp_VIJF(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, float arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DIIJDD(double (*f)(vm_thread *, cp_method *,  s32, s32, s64, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].d, args[4].d);
}
static double interpreter_tramp_DIIJDD(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, double arg3, double arg4) {
  stack_value values[29];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].d = arg3;
  values[4].d = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static float jit_tramp_FIJF(float (*f)(vm_thread *, cp_method *,  s32, s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].f);
}
static float interpreter_tramp_FIJF(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, float arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static float jit_tramp_FIJ(float (*f)(vm_thread *, cp_method *,  s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l);
}
static float interpreter_tramp_FIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static double jit_tramp_DIJ(double (*f)(vm_thread *, cp_method *,  s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l);
}
static double interpreter_tramp_DIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].l = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VIJD(void (*f)(vm_thread *, cp_method *,  s32, s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].d);
}
static void interpreter_tramp_VIJD(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, double arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FIIJFF(float (*f)(vm_thread *, cp_method *,  s32, s32, s64, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].f, args[4].f);
}
static float interpreter_tramp_FIIJFF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, float arg3, float arg4) {
  stack_value values[27];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].f = arg3;
  values[4].f = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIJIII(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i, args[4].i);
}
static s32 interpreter_tramp_IIJIII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIJJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].i);
}
static void interpreter_tramp_VIIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JJJI(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i);
}
static s64 interpreter_tramp_JJJI(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i);
}
static s32 interpreter_tramp_IIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9) {
  stack_value values[48];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJIJ(void (*f)(vm_thread *, cp_method *,  s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i, args[2].l);
}
static void interpreter_tramp_VJIJ(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JJIJI(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l, args[3].i);
}
static s64 interpreter_tramp_JJIJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIJDD(s32 (*f)(vm_thread *, cp_method *,  s32, s64, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].d, args[3].d);
}
static s32 interpreter_tramp_IIJDD(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, double arg2, double arg3) {
  stack_value values[24];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIJFF(s32 (*f)(vm_thread *, cp_method *,  s32, s64, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].f, args[3].f);
}
static s32 interpreter_tramp_IIJFF(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, float arg2, float arg3) {
  stack_value values[22];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].f = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JD(s64 (*f)(vm_thread *, cp_method *,  double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d);
}
static s64 interpreter_tramp_JD(vm_thread *thread, cp_method *method,  double arg0) {
  stack_value values[6];
  values[0].d = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIJIJ(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l);
}
static void interpreter_tramp_VIJIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JJJJ(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l);
}
static s64 interpreter_tramp_JJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DII(double (*f)(vm_thread *, cp_method *,  s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i);
}
static double interpreter_tramp_DII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VF(void (*f)(vm_thread *, cp_method *,  float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].f);
}
static void interpreter_tramp_VF(vm_thread *thread, cp_method *method,  float arg0) {
  stack_value values[5];
  values[0].f = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FII(float (*f)(vm_thread *, cp_method *,  s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i);
}
static float interpreter_tramp_FII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1) {
  stack_value values[8];
  values[0].i = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIJJI(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i);
}
static s32 interpreter_tramp_IIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JJIII(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i);
}
static s64 interpreter_tramp_JJIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DJIDI(double (*f)(vm_thread *, cp_method *,  s64, s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].d, args[3].i);
}
static double interpreter_tramp_DJIDI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, double arg2, s32 arg3) {
  stack_value values[21];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VIJIIII(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i, args[4].i, args[5].i);
}
static void interpreter_tramp_VIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FFFF(float (*f)(vm_thread *, cp_method *,  float, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].f, args[2].f);
}
static float interpreter_tramp_FFFF(vm_thread *thread, cp_method *method,  float arg0, float arg1, float arg2) {
  stack_value values[19];
  values[0].f = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static double jit_tramp_DDDD(double (*f)(vm_thread *, cp_method *,  double, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d, args[2].d);
}
static double interpreter_tramp_DDDD(vm_thread *thread, cp_method *method,  double arg0, double arg1, double arg2) {
  stack_value values[22];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIJJII(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i, args[4].i);
}
static s64 interpreter_tramp_JIJJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DJ(double (*f)(vm_thread *, cp_method *,  s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l);
}
static double interpreter_tramp_DJ(vm_thread *thread, cp_method *method,  s64 arg0) {
  stack_value values[3];
  values[0].l = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i);
}
static void interpreter_tramp_VIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIJJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].i);
}
static s32 interpreter_tramp_IIIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIFI(void (*f)(vm_thread *, cp_method *,  s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].f, args[2].i);
}
static void interpreter_tramp_VIFI(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, s32 arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIF(void (*f)(vm_thread *, cp_method *,  s32, s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].f);
}
static void interpreter_tramp_VIIF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DIJDD(double (*f)(vm_thread *, cp_method *,  s32, s64, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].d, args[3].d);
}
static double interpreter_tramp_DIJDD(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, double arg2, double arg3) {
  stack_value values[24];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static float jit_tramp_FJ(float (*f)(vm_thread *, cp_method *,  s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l);
}
static float interpreter_tramp_FJ(vm_thread *thread, cp_method *method,  s64 arg0) {
  stack_value values[3];
  values[0].l = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s64 jit_tramp_JIIIIIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i);
}
static s64 interpreter_tramp_JIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIIJIJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].l);
}
static void interpreter_tramp_VIIJIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIJIJJJ(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l, args[4].l, args[5].l);
}
static void interpreter_tramp_VIJIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3, s64 arg4, s64 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].l = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].l, args[6].i);
}
static void interpreter_tramp_VIIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s64 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VJJJ(void (*f)(vm_thread *, cp_method *,  s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].l, args[2].l);
}
static void interpreter_tramp_VJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FIJFF(float (*f)(vm_thread *, cp_method *,  s32, s64, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].f, args[3].f);
}
static float interpreter_tramp_FIJFF(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, float arg2, float arg3) {
  stack_value values[22];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].f = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s64 jit_tramp_JIJJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].l);
}
static s64 interpreter_tramp_JIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l);
}
static s32 interpreter_tramp_IIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l);
}
static void interpreter_tramp_VIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIIJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i);
}
static s64 interpreter_tramp_JIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIJIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IIIJIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DDI(double (*f)(vm_thread *, cp_method *,  double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].i);
}
static double interpreter_tramp_DDI(vm_thread *thread, cp_method *method,  double arg0, s32 arg1) {
  stack_value values[11];
  values[0].d = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static void jit_tramp_VIID(void (*f)(vm_thread *, cp_method *,  s32, s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].d);
}
static void interpreter_tramp_VIID(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJIJ(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l);
}
static s32 interpreter_tramp_IJIJ(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i);
}
static s64 interpreter_tramp_JIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VID(void (*f)(vm_thread *, cp_method *,  s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].d);
}
static void interpreter_tramp_VID(vm_thread *thread, cp_method *method,  s32 arg0, double arg1) {
  stack_value values[11];
  values[0].i = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIJIJJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].l, args[5].l);
}
static void interpreter_tramp_VIIJIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4, s64 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].l = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JJJJI(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l, args[3].i);
}
static s64 interpreter_tramp_JJJJI(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIID(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d);
}
static s32 interpreter_tramp_IIIID(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i);
}
static s32 interpreter_tramp_IIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10) {
  stack_value values[53];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJJJII(void (*f)(vm_thread *, cp_method *,  s64, s64, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].l, args[2].l, args[3].i, args[4].i);
}
static void interpreter_tramp_VJJJII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIJIJJJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i, args[5].l, args[6].l, args[7].l);
}
static void interpreter_tramp_VIIIJIJJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4, s64 arg5, s64 arg6, s64 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].l = arg6;
  values[7].l = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FFI(float (*f)(vm_thread *, cp_method *,  float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].i);
}
static float interpreter_tramp_FFI(vm_thread *thread, cp_method *method,  float arg0, s32 arg1) {
  stack_value values[10];
  values[0].f = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s64 jit_tramp_JIIIIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s64 interpreter_tramp_JIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIJIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i, args[5].l, args[6].i, args[7].i);
}
static s32 interpreter_tramp_IIIIJIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4, s64 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FFD(float (*f)(vm_thread *, cp_method *,  float, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].d);
}
static float interpreter_tramp_FFD(vm_thread *thread, cp_method *method,  float arg0, double arg1) {
  stack_value values[13];
  values[0].f = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VIIIJIJJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i, args[5].l, args[6].l);
}
static void interpreter_tramp_VIIIJIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4, s64 arg5, s64 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].l = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i);
}
static void interpreter_tramp_VIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10) {
  stack_value values[53];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].l, args[5].i);
}
static void interpreter_tramp_VIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIF(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f);
}
static s32 interpreter_tramp_IIIIF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIIIIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i);
}
static void interpreter_tramp_VIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11) {
  stack_value values[58];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VJIIII(void (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].i);
}
static void interpreter_tramp_VJIIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IDI(s32 (*f)(vm_thread *, cp_method *,  double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].i);
}
static s32 interpreter_tramp_IDI(vm_thread *thread, cp_method *method,  double arg0, s32 arg1) {
  stack_value values[11];
  values[0].d = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIIJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].l);
}
static void interpreter_tramp_VIIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IDD(s32 (*f)(vm_thread *, cp_method *,  double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d);
}
static s32 interpreter_tramp_IDD(vm_thread *thread, cp_method *method,  double arg0, double arg1) {
  stack_value values[14];
  values[0].d = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJJI(void (*f)(vm_thread *, cp_method *,  s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].l, args[2].i);
}
static void interpreter_tramp_VJJI(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJJJ(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l);
}
static s32 interpreter_tramp_IJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIJJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l);
}
static s32 interpreter_tramp_IIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJIII(void (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i);
}
static void interpreter_tramp_VJIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].l);
}
static void interpreter_tramp_VIIIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s64 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIID(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d);
}
static void interpreter_tramp_VIIID(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IFF(s32 (*f)(vm_thread *, cp_method *,  float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].f);
}
static s32 interpreter_tramp_IFF(vm_thread *thread, cp_method *method,  float arg0, float arg1) {
  stack_value values[12];
  values[0].f = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].l);
}
static s32 interpreter_tramp_IIIIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s64 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIF(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f);
}
static void interpreter_tramp_VIIIF(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIFF(void (*f)(vm_thread *, cp_method *,  s32, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].f, args[2].f);
}
static void interpreter_tramp_VIFF(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, float arg2) {
  stack_value values[17];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DF(double (*f)(vm_thread *, cp_method *,  float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f);
}
static double interpreter_tramp_DF(vm_thread *thread, cp_method *method,  float arg0) {
  stack_value values[5];
  values[0].f = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static float jit_tramp_FD(float (*f)(vm_thread *, cp_method *,  double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d);
}
static float interpreter_tramp_FD(vm_thread *thread, cp_method *method,  double arg0) {
  stack_value values[6];
  values[0].d = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VIIJIIJJ(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].l, args[6].l);
}
static void interpreter_tramp_VIIJIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s64 arg5, s64 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].l = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIJJII(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i, args[4].i);
}
static s32 interpreter_tramp_IIJJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIIIIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i, args[12].i, args[13].i, args[14].i, args[15].i, args[16].i, args[17].i);
}
static s32 interpreter_tramp_IIIIIIIIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11, s32 arg12, s32 arg13, s32 arg14, s32 arg15, s32 arg16, s32 arg17) {
  stack_value values[88];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  values[12].i = arg12;
  values[13].i = arg13;
  values[14].i = arg14;
  values[15].i = arg15;
  values[16].i = arg16;
  values[17].i = arg17;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].l, args[7].i);
}
static void interpreter_tramp_VIIIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s64 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].l = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JJIJ(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l);
}
static s64 interpreter_tramp_JJIJ(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2) {
  stack_value values[13];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIFFI(void (*f)(vm_thread *, cp_method *,  s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].f, args[2].f, args[3].i);
}
static void interpreter_tramp_VIFFI(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, float arg2, s32 arg3) {
  stack_value values[22];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VJF(void (*f)(vm_thread *, cp_method *,  s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].f);
}
static void interpreter_tramp_VJF(vm_thread *thread, cp_method *method,  s64 arg0, float arg1) {
  stack_value values[10];
  values[0].l = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i);
}
static s32 interpreter_tramp_IIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11) {
  stack_value values[58];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].l, args[5].i, args[6].i);
}
static s32 interpreter_tramp_IIIIIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJF(s32 (*f)(vm_thread *, cp_method *,  s64, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].f);
}
static s32 interpreter_tramp_IJF(vm_thread *thread, cp_method *method,  s64 arg0, float arg1) {
  stack_value values[10];
  values[0].l = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJD(void (*f)(vm_thread *, cp_method *,  s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].d);
}
static void interpreter_tramp_VJD(vm_thread *thread, cp_method *method,  s64 arg0, double arg1) {
  stack_value values[11];
  values[0].l = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].l, args[8].i);
}
static void interpreter_tramp_VIIIIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s64 arg7, s32 arg8) {
  stack_value values[43];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].l = arg7;
  values[8].i = arg8;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIJIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i, args[5].i, args[6].i, args[7].i);
}
static s32 interpreter_tramp_IIIIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIJIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IIJIJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJJJJ(void (*f)(vm_thread *, cp_method *,  s64, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].l, args[2].l, args[3].l);
}
static void interpreter_tramp_VJJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJD(s32 (*f)(vm_thread *, cp_method *,  s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].d);
}
static s32 interpreter_tramp_IJD(vm_thread *thread, cp_method *method,  s64 arg0, double arg1) {
  stack_value values[11];
  values[0].l = arg0;
  values[1].d = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIJIII(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i);
}
static void interpreter_tramp_VIIJIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIIIIIIJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].l, args[9].i);
}
static void interpreter_tramp_VIIIIIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s64 arg8, s32 arg9) {
  stack_value values[48];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].l = arg8;
  values[9].i = arg9;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIIIIJIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].l, args[7].i, args[8].i, args[9].i, args[10].i);
}
static s32 interpreter_tramp_IIIIIIIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s64 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10) {
  stack_value values[53];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].l = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i, args[12].i, args[13].i);
}
static s32 interpreter_tramp_IIIIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11, s32 arg12, s32 arg13) {
  stack_value values[68];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  values[12].i = arg12;
  values[13].i = arg13;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIJIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].l, args[6].i, args[7].i, args[8].i, args[9].i);
}
static s32 interpreter_tramp_IIIIIIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s64 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9) {
  stack_value values[48];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JF(s64 (*f)(vm_thread *, cp_method *,  float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f);
}
static s64 interpreter_tramp_JF(vm_thread *thread, cp_method *method,  float arg0) {
  stack_value values[5];
  values[0].f = arg0;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIIIJII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].l, args[6].i, args[7].i);
}
static s32 interpreter_tramp_IIIIIIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s64 arg5, s32 arg6, s32 arg7) {
  stack_value values[38];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].l = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VJIIIII(void (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i);
}
static void interpreter_tramp_VJIIIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIJJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].l);
}
static s32 interpreter_tramp_IIIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIJIIIIIIII(void (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i);
}
static void interpreter_tramp_VIJIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9) {
  stack_value values[48];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIJJIJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i, args[4].l, args[5].l);
}
static s64 interpreter_tramp_JIJJIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3, s64 arg4, s64 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].l = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIIIIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i, args[12].i, args[13].i, args[14].i, args[15].i);
}
static s32 interpreter_tramp_IIIIIIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11, s32 arg12, s32 arg13, s32 arg14, s32 arg15) {
  stack_value values[78];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  values[12].i = arg12;
  values[13].i = arg13;
  values[14].i = arg14;
  values[15].i = arg15;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIIIIIIIIIIIIIIIIIII(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i, args[7].i, args[8].i, args[9].i, args[10].i, args[11].i, args[12].i, args[13].i, args[14].i, args[15].i, args[16].i, args[17].i, args[18].i, args[19].i);
}
static s32 interpreter_tramp_IIIIIIIIIIIIIIIIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7, s32 arg8, s32 arg9, s32 arg10, s32 arg11, s32 arg12, s32 arg13, s32 arg14, s32 arg15, s32 arg16, s32 arg17, s32 arg18, s32 arg19) {
  stack_value values[98];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  values[7].i = arg7;
  values[8].i = arg8;
  values[9].i = arg9;
  values[10].i = arg10;
  values[11].i = arg11;
  values[12].i = arg12;
  values[13].i = arg13;
  values[14].i = arg14;
  values[15].i = arg15;
  values[16].i = arg16;
  values[17].i = arg17;
  values[18].i = arg18;
  values[19].i = arg19;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DDDDD(double (*f)(vm_thread *, cp_method *,  double, double, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d, args[2].d, args[3].d);
}
static double interpreter_tramp_DDDDD(vm_thread *thread, cp_method *method,  double arg0, double arg1, double arg2, double arg3) {
  stack_value values[30];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIJIII(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i, args[4].i);
}
static s64 interpreter_tramp_JIJIII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIJII(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i);
}
static s64 interpreter_tramp_JIJII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IFI(s32 (*f)(vm_thread *, cp_method *,  float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].f, args[1].i);
}
static s32 interpreter_tramp_IFI(vm_thread *thread, cp_method *method,  float arg0, s32 arg1) {
  stack_value values[10];
  values[0].f = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VDI(void (*f)(vm_thread *, cp_method *,  double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].d, args[1].i);
}
static void interpreter_tramp_VDI(vm_thread *thread, cp_method *method,  double arg0, s32 arg1) {
  stack_value values[11];
  values[0].d = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s64 jit_tramp_JIIJII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i);
}
static s64 interpreter_tramp_JIIJII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIIJIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i);
}
static s64 interpreter_tramp_JIIJIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DIII(double (*f)(vm_thread *, cp_method *,  s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i);
}
static double interpreter_tramp_DIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IIIIIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].l);
}
static s32 interpreter_tramp_IIIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIJIJI(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l, args[4].i);
}
static s64 interpreter_tramp_JIJIJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IJDD(s32 (*f)(vm_thread *, cp_method *,  s64, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].d, args[2].d);
}
static s32 interpreter_tramp_IJDD(vm_thread *thread, cp_method *method,  s64 arg0, double arg1, double arg2) {
  stack_value values[19];
  values[0].l = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DIIII(double (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i);
}
static double interpreter_tramp_DIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIJIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].i, args[4].i, args[5].i);
}
static s64 interpreter_tramp_JIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIIJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IIIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FIII(float (*f)(vm_thread *, cp_method *,  s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i);
}
static float interpreter_tramp_FIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2) {
  stack_value values[13];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static void jit_tramp_VIDI(void (*f)(vm_thread *, cp_method *,  s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].d, args[2].i);
}
static void interpreter_tramp_VIDI(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, s32 arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IDDII(s32 (*f)(vm_thread *, cp_method *,  double, double, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d, args[2].i, args[3].i);
}
static s32 interpreter_tramp_IDDII(vm_thread *thread, cp_method *method,  double arg0, double arg1, s32 arg2, s32 arg3) {
  stack_value values[24];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JJJIJI(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i, args[3].l, args[4].i);
}
static s64 interpreter_tramp_JJJIJI(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DIDI(double (*f)(vm_thread *, cp_method *,  s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d, args[2].i);
}
static double interpreter_tramp_DIDI(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, s32 arg2) {
  stack_value values[16];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IIIDDI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].d, args[3].d, args[4].i);
}
static s32 interpreter_tramp_IIIDDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2, double arg3, s32 arg4) {
  stack_value values[29];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIJJJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].l, args[5].i);
}
static s64 interpreter_tramp_JIIJJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static void jit_tramp_VIIDI(void (*f)(vm_thread *, cp_method *,  s32, s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].d, args[3].i);
}
static void interpreter_tramp_VIIDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2, s32 arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DIIDI(double (*f)(vm_thread *, cp_method *,  s32, s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].d, args[3].i);
}
static double interpreter_tramp_DIIDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2, s32 arg3) {
  stack_value values[21];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IIIIJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i);
}
static s32 interpreter_tramp_IIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FIIII(float (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i);
}
static float interpreter_tramp_FIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIIJJJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IIIJJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJIJI(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l, args[3].i);
}
static s32 interpreter_tramp_IJIJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJIJIJI(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l, args[3].i, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IJIJIJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DIIIDI(double (*f)(vm_thread *, cp_method *,  s32, s32, s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d, args[4].i);
}
static double interpreter_tramp_DIIIDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3, s32 arg4) {
  stack_value values[26];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IIIIJJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IIIIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DIIIDDI(double (*f)(vm_thread *, cp_method *,  s32, s32, s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d, args[4].d, args[5].i);
}
static double interpreter_tramp_DIIIDDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3, double arg4, s32 arg5) {
  stack_value values[34];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  values[4].d = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIIIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i);
}
static s64 interpreter_tramp_JIIIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIDDI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d, args[4].d, args[5].i);
}
static s32 interpreter_tramp_IIIIDDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3, double arg4, s32 arg5) {
  stack_value values[34];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  values[4].d = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DIDDI(double (*f)(vm_thread *, cp_method *,  s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d, args[2].d, args[3].i);
}
static double interpreter_tramp_DIDDI(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, double arg2, s32 arg3) {
  stack_value values[24];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IJJIII(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i, args[3].i, args[4].i);
}
static s32 interpreter_tramp_IJJIII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIDDI(s32 (*f)(vm_thread *, cp_method *,  s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d, args[2].d, args[3].i);
}
static s32 interpreter_tramp_IIDDI(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, double arg2, s32 arg3) {
  stack_value values[24];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIIJJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].l, args[5].i);
}
static s64 interpreter_tramp_JIIIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIFFI(s32 (*f)(vm_thread *, cp_method *,  s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].f, args[2].f, args[3].i);
}
static s32 interpreter_tramp_IIFFI(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, float arg2, s32 arg3) {
  stack_value values[22];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JJIIIIII(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s64 interpreter_tramp_JJIIIIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIIIJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].i);
}
static s64 interpreter_tramp_JIIIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JJJIIII(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i, args[3].i, args[4].i, args[5].i);
}
static s64 interpreter_tramp_JJJIIII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIJJI(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].i);
}
static s64 interpreter_tramp_JIJJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s32 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static double jit_tramp_DIIDDI(double (*f)(vm_thread *, cp_method *,  s32, s32, double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].d, args[3].d, args[4].i);
}
static double interpreter_tramp_DIIDDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, double arg2, double arg3, s32 arg4) {
  stack_value values[29];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s64 jit_tramp_JIIJIJ(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].l);
}
static s64 interpreter_tramp_JIIJIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIJIJ(s64 (*f)(vm_thread *, cp_method *,  s32, s64, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].i, args[3].l);
}
static s64 interpreter_tramp_JIJIJ(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s32 arg2, s64 arg3) {
  stack_value values[18];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IIIIFFI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f, args[4].f, args[5].i);
}
static s32 interpreter_tramp_IIIIFFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3, float arg4, s32 arg5) {
  stack_value values[32];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  values[4].f = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static float jit_tramp_FIIFI(float (*f)(vm_thread *, cp_method *,  s32, s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].f, args[3].i);
}
static float interpreter_tramp_FIIFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2, s32 arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static float jit_tramp_FIFI(float (*f)(vm_thread *, cp_method *,  s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].f, args[2].i);
}
static float interpreter_tramp_FIFI(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, s32 arg2) {
  stack_value values[15];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIJJJI(s32 (*f)(vm_thread *, cp_method *,  s32, s64, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].l, args[2].l, args[3].l, args[4].i);
}
static s32 interpreter_tramp_IIJJJI(vm_thread *thread, cp_method *method,  s32 arg0, s64 arg1, s64 arg2, s64 arg3, s32 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIDD(s32 (*f)(vm_thread *, cp_method *,  s32, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d, args[2].d);
}
static s32 interpreter_tramp_IIDD(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, double arg2) {
  stack_value values[19];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIIFI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f, args[4].i);
}
static void interpreter_tramp_VIIIFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3, s32 arg4) {
  stack_value values[25];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static float jit_tramp_FIIFFI(float (*f)(vm_thread *, cp_method *,  s32, s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].f, args[3].f, args[4].i);
}
static float interpreter_tramp_FIIFFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2, float arg3, s32 arg4) {
  stack_value values[27];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  values[3].f = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IIIFFI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].f, args[3].f, args[4].i);
}
static s32 interpreter_tramp_IIIFFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2, float arg3, s32 arg4) {
  stack_value values[27];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  values[3].f = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIFF(s32 (*f)(vm_thread *, cp_method *,  s32, float, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].f, args[2].f);
}
static s32 interpreter_tramp_IIFF(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, float arg2) {
  stack_value values[17];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VIIJJJI(void (*f)(vm_thread *, cp_method *,  s32, s32, s64, s64, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].l, args[3].l, args[4].l, args[5].i);
}
static void interpreter_tramp_VIIJJJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s64 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIIDI(void (*f)(vm_thread *, cp_method *,  s32, s32, s32, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].i, args[3].d, args[4].i);
}
static void interpreter_tramp_VIIIDI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, double arg3, s32 arg4) {
  stack_value values[26];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].d = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static void jit_tramp_VIIFI(void (*f)(vm_thread *, cp_method *,  s32, s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].i, args[1].i, args[2].f, args[3].i);
}
static void interpreter_tramp_VIIFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, float arg2, s32 arg3) {
  stack_value values[20];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].f = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static double jit_tramp_DJJD(double (*f)(vm_thread *, cp_method *,  s64, s64, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].d);
}
static double interpreter_tramp_DJJD(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, double arg2) {
  stack_value values[16];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static float jit_tramp_FIIIFI(float (*f)(vm_thread *, cp_method *,  s32, s32, s32, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f, args[4].i);
}
static float interpreter_tramp_FIIIFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3, s32 arg4) {
  stack_value values[25];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  values[4].i = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static float jit_tramp_FIFFI(float (*f)(vm_thread *, cp_method *,  s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].f, args[2].f, args[3].i);
}
static float interpreter_tramp_FIFFI(vm_thread *thread, cp_method *method,  s32 arg0, float arg1, float arg2, s32 arg3) {
  stack_value values[22];
  values[0].i = arg0;
  values[1].f = arg1;
  values[2].f = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s64 jit_tramp_JIIJIJI(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].l, args[5].i);
}
static s64 interpreter_tramp_JIIJIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JJIIIJI(s64 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].l, args[5].i);
}
static s64 interpreter_tramp_JJIIIJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static float jit_tramp_FIIIFFI(float (*f)(vm_thread *, cp_method *,  s32, s32, s32, float, float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].f, args[4].f, args[5].i);
}
static float interpreter_tramp_FIIIFFI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, float arg3, float arg4, s32 arg5) {
  stack_value values[32];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].f = arg3;
  values[4].f = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.f;
}
static s32 jit_tramp_IJIIIJI(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IJIIIJI(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIDDD(s32 (*f)(vm_thread *, cp_method *,  s32, double, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].d, args[2].d, args[3].d);
}
static s32 interpreter_tramp_IIDDD(vm_thread *thread, cp_method *method,  s32 arg0, double arg1, double arg2, double arg3) {
  stack_value values[27];
  values[0].i = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VDF(void (*f)(vm_thread *, cp_method *,  double, float), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].d, args[1].f);
}
static void interpreter_tramp_VDF(vm_thread *thread, cp_method *method,  double arg0, float arg1) {
  stack_value values[13];
  values[0].d = arg0;
  values[1].f = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IIIIIIIJ(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].l);
}
static s32 interpreter_tramp_IIIIIIIJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s64 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].l = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s64 jit_tramp_JIIJIIII(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s64 interpreter_tramp_JIIJIIII(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JJJJJJ(s64 (*f)(vm_thread *, cp_method *,  s64, s64, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l, args[3].l, args[4].l);
}
static s64 interpreter_tramp_JJJJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s64 jit_tramp_JIIIJJ(s64 (*f)(vm_thread *, cp_method *,  s32, s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].i, args[3].l, args[4].l);
}
static s64 interpreter_tramp_JIIIJJ(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s32 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.l;
}
static s32 jit_tramp_IDDD(s32 (*f)(vm_thread *, cp_method *,  double, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d, args[2].d);
}
static s32 interpreter_tramp_IDDD(vm_thread *thread, cp_method *method,  double arg0, double arg1, double arg2) {
  stack_value values[22];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VDDDDDDD(void (*f)(vm_thread *, cp_method *,  double, double, double, double, double, double, double), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].d, args[1].d, args[2].d, args[3].d, args[4].d, args[5].d, args[6].d);
}
static void interpreter_tramp_VDDDDDDD(vm_thread *thread, cp_method *method,  double arg0, double arg1, double arg2, double arg3, double arg4, double arg5, double arg6) {
  stack_value values[54];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].d = arg2;
  values[3].d = arg3;
  values[4].d = arg4;
  values[5].d = arg5;
  values[6].d = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}
static s32 jit_tramp_IJJJIIII(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s64, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l, args[3].i, args[4].i, args[5].i, args[6].i);
}
static s32 interpreter_tramp_IJJJIIII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6) {
  stack_value values[33];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  values[6].i = arg6;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJJII(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].i, args[3].i);
}
static s32 interpreter_tramp_IJJII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s32 arg2, s32 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IIIJIJI(s32 (*f)(vm_thread *, cp_method *,  s32, s32, s64, s32, s64, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].i, args[1].i, args[2].l, args[3].i, args[4].l, args[5].i);
}
static s32 interpreter_tramp_IIIJIJI(vm_thread *thread, cp_method *method,  s32 arg0, s32 arg1, s64 arg2, s32 arg3, s64 arg4, s32 arg5) {
  stack_value values[28];
  values[0].i = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static double jit_tramp_DDDI(double (*f)(vm_thread *, cp_method *,  double, double, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].d, args[1].d, args[2].i);
}
static double interpreter_tramp_DDDI(vm_thread *thread, cp_method *method,  double arg0, double arg1, s32 arg2) {
  stack_value values[19];
  values[0].d = arg0;
  values[1].d = arg1;
  values[2].i = arg2;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.d;
}
static s32 jit_tramp_IJIIIII(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IJIIIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJIJIII(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].l, args[3].i, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IJIJIII(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJJJJ(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l, args[3].l);
}
static s32 interpreter_tramp_IJJJJ(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s64 arg3) {
  stack_value values[18];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].l = arg3;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJJJIII(s32 (*f)(vm_thread *, cp_method *,  s64, s64, s64, s32, s32, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].l, args[2].l, args[3].i, args[4].i, args[5].i);
}
static s32 interpreter_tramp_IJJJIII(vm_thread *thread, cp_method *method,  s64 arg0, s64 arg1, s64 arg2, s32 arg3, s32 arg4, s32 arg5) {
  stack_value values[28];
  values[0].l = arg0;
  values[1].l = arg1;
  values[2].l = arg2;
  values[3].i = arg3;
  values[4].i = arg4;
  values[5].i = arg5;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJIIJJ(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s64, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].l, args[4].l);
}
static s32 interpreter_tramp_IJIIJJ(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s64 arg3, s64 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].l = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static s32 jit_tramp_IJIIIJ(s32 (*f)(vm_thread *, cp_method *,  s64, s32, s32, s32, s64), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL return f(thread, method, args[0].l, args[1].i, args[2].i, args[3].i, args[4].l);
}
static s32 interpreter_tramp_IJIIIJ(vm_thread *thread, cp_method *method,  s64 arg0, s32 arg1, s32 arg2, s32 arg3, s64 arg4) {
  stack_value values[23];
  values[0].l = arg0;
  values[1].i = arg1;
  values[2].i = arg2;
  values[3].i = arg3;
  values[4].l = arg4;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
  return result.i;
}
static void jit_tramp_VFI(void (*f)(vm_thread *, cp_method *,  float, s32), vm_thread *thread, cp_method *method, stack_value *args) {
  MUSTTAIL  f(thread, method, args[0].f, args[1].i);
}
static void interpreter_tramp_VFI(vm_thread *thread, cp_method *method,  float arg0, s32 arg1) {
  stack_value values[10];
  values[0].f = arg0;
  values[1].i = arg1;
  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);
}

static void add_pregenerated_trampolines() {
  (void)hash_table_insert(&jit_trampolines, "\x7f", 1, (void *) jit_tramp_I);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f", 1, (void *) interpreter_tramp_I);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f", 2, (void *) jit_tramp_II);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f", 2, (void *) interpreter_tramp_II);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f", 3, (void *) jit_tramp_III);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f", 3, (void *) interpreter_tramp_III);
  (void)hash_table_insert(&jit_trampolines, "\x00", 1, (void *) jit_tramp_V);
  (void)hash_table_insert(&interpreter_trampolines, "\x00", 1, (void *) interpreter_tramp_V);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f", 2, (void *) jit_tramp_VI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f", 2, (void *) interpreter_tramp_VI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f", 4, (void *) jit_tramp_IIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f", 4, (void *) interpreter_tramp_IIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f", 3, (void *) jit_tramp_VII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f", 3, (void *) interpreter_tramp_VII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f", 4, (void *) jit_tramp_VIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f", 4, (void *) interpreter_tramp_VIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f", 5, (void *) jit_tramp_IIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f", 5, (void *) interpreter_tramp_IIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f", 5, (void *) jit_tramp_VIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f", 5, (void *) interpreter_tramp_VIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e", 1, (void *) jit_tramp_J);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e", 1, (void *) interpreter_tramp_J);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7f", 5, (void *) jit_tramp_IIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7f", 5, (void *) interpreter_tramp_IIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7f", 4, (void *) jit_tramp_IIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7f", 4, (void *) interpreter_tramp_IIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f", 6, (void *) jit_tramp_IIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f", 6, (void *) interpreter_tramp_IIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e", 2, (void *) jit_tramp_IJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e", 2, (void *) interpreter_tramp_IJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f", 2, (void *) jit_tramp_JI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f", 2, (void *) interpreter_tramp_JI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e", 3, (void *) jit_tramp_IIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e", 3, (void *) interpreter_tramp_IIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f", 6, (void *) jit_tramp_VIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f", 6, (void *) interpreter_tramp_VIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_IIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_IIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c", 2, (void *) jit_tramp_DD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c", 2, (void *) interpreter_tramp_DD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e", 2, (void *) jit_tramp_VJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e", 2, (void *) interpreter_tramp_VJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7f\x7f", 5, (void *) jit_tramp_IIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7f\x7f", 5, (void *) interpreter_tramp_IIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e", 4, (void *) jit_tramp_IIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e", 4, (void *) interpreter_tramp_IIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e", 2, (void *) jit_tramp_JJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e", 2, (void *) interpreter_tramp_JJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e", 3, (void *) jit_tramp_JJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e", 3, (void *) interpreter_tramp_JJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f", 4, (void *) jit_tramp_VIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f", 4, (void *) interpreter_tramp_VIJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f", 5, (void *) jit_tramp_VIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f", 5, (void *) interpreter_tramp_VIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f", 3, (void *) jit_tramp_IJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f", 3, (void *) interpreter_tramp_IJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7e", 5, (void *) jit_tramp_JIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7e", 5, (void *) interpreter_tramp_JIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7d", 2, (void *) jit_tramp_IF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7d", 2, (void *) interpreter_tramp_IF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7c", 2, (void *) jit_tramp_ID);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7c", 2, (void *) interpreter_tramp_ID);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f", 3, (void *) jit_tramp_JII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f", 3, (void *) interpreter_tramp_JII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_IIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_IIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f", 3, (void *) jit_tramp_VJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f", 3, (void *) interpreter_tramp_VJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7e", 4, (void *) jit_tramp_JIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7e", 4, (void *) interpreter_tramp_JIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d", 1, (void *) jit_tramp_F);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d", 1, (void *) interpreter_tramp_F);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f", 3, (void *) jit_tramp_JJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f", 3, (void *) interpreter_tramp_JJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e", 3, (void *) jit_tramp_IJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e", 3, (void *) interpreter_tramp_IJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7c", 1, (void *) jit_tramp_D);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c", 1, (void *) interpreter_tramp_D);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c\x7c", 3, (void *) jit_tramp_DDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c\x7c", 3, (void *) interpreter_tramp_DDD);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f", 4, (void *) jit_tramp_JIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f", 4, (void *) interpreter_tramp_JIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f", 6, (void *) jit_tramp_IIIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f", 6, (void *) interpreter_tramp_IIIJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7e", 4, (void *) jit_tramp_VIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7e", 4, (void *) interpreter_tramp_VIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_VIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_VIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7f", 4, (void *) jit_tramp_IJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7f", 4, (void *) interpreter_tramp_IJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_VIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_VIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7e", 3, (void *) jit_tramp_VJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7e", 3, (void *) interpreter_tramp_VJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f", 2, (void *) jit_tramp_FI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f", 2, (void *) interpreter_tramp_FI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) jit_tramp_IIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) interpreter_tramp_IIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f", 4, (void *) jit_tramp_IJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f", 4, (void *) interpreter_tramp_IJII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f", 2, (void *) jit_tramp_DI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f", 2, (void *) interpreter_tramp_DI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e", 3, (void *) jit_tramp_JIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e", 3, (void *) interpreter_tramp_JIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e", 3, (void *) jit_tramp_VIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e", 3, (void *) interpreter_tramp_VIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7e\x7d", 5, (void *) jit_tramp_FIIJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7e\x7d", 5, (void *) interpreter_tramp_FIIJF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7e\x7c", 5, (void *) jit_tramp_DIIJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7e\x7c", 5, (void *) interpreter_tramp_DIIJD);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7c", 3, (void *) jit_tramp_IID);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7c", 3, (void *) interpreter_tramp_IID);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7c", 4, (void *) jit_tramp_IIID);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7c", 4, (void *) interpreter_tramp_IIID);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7d", 4, (void *) jit_tramp_IIIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7d", 4, (void *) interpreter_tramp_IIIF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7d", 3, (void *) jit_tramp_IIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7d", 3, (void *) interpreter_tramp_IIF);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7f", 4, (void *) jit_tramp_JJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7f", 4, (void *) interpreter_tramp_JJII);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7d", 2, (void *) jit_tramp_FF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7d", 2, (void *) interpreter_tramp_FF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7f", 5, (void *) jit_tramp_VIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7f", 5, (void *) interpreter_tramp_VIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7d\x7d", 6, (void *) jit_tramp_IIIJFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7d\x7d", 6, (void *) interpreter_tramp_IIIJFF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 10, (void *) jit_tramp_IIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 10, (void *) interpreter_tramp_IIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7e", 6, (void *) jit_tramp_IIIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7e", 6, (void *) interpreter_tramp_IIIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7c\x7c", 6, (void *) jit_tramp_IIIJDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7c\x7c", 6, (void *) interpreter_tramp_IIIJDD);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7f", 5, (void *) jit_tramp_JIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7f", 5, (void *) interpreter_tramp_JIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7e", 4, (void *) jit_tramp_IIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7e", 4, (void *) interpreter_tramp_IIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7d\x7d", 3, (void *) jit_tramp_FFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7d\x7d", 3, (void *) interpreter_tramp_FFF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f\x7f", 4, (void *) jit_tramp_VJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f\x7f", 4, (void *) interpreter_tramp_VJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7d", 3, (void *) jit_tramp_VIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7d", 3, (void *) interpreter_tramp_VIF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) jit_tramp_VIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) interpreter_tramp_VIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e", 4, (void *) jit_tramp_JIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e", 4, (void *) interpreter_tramp_JIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_IIIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_IIIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7e", 4, (void *) jit_tramp_FIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7e", 4, (void *) interpreter_tramp_FIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7e\x7f", 5, (void *) jit_tramp_VIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7e\x7f", 5, (void *) interpreter_tramp_VIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7e", 5, (void *) jit_tramp_VIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7e", 5, (void *) interpreter_tramp_VIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7c", 5, (void *) jit_tramp_VIIJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7c", 5, (void *) interpreter_tramp_VIIJD);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7f\x7e", 5, (void *) jit_tramp_IIJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7f\x7e", 5, (void *) interpreter_tramp_IIJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7d", 5, (void *) jit_tramp_VIIJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7d", 5, (void *) interpreter_tramp_VIIJF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7e", 4, (void *) jit_tramp_DIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7e", 4, (void *) interpreter_tramp_DIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f", 4, (void *) jit_tramp_JIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f", 4, (void *) interpreter_tramp_JIJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e", 4, (void *) jit_tramp_VIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e", 4, (void *) interpreter_tramp_VIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f\x7f", 5, (void *) jit_tramp_IJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f\x7f", 5, (void *) interpreter_tramp_IJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7f", 6, (void *) jit_tramp_JIIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7f", 6, (void *) interpreter_tramp_JIIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f", 6, (void *) jit_tramp_VIIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f", 6, (void *) interpreter_tramp_VIIJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 10, (void *) jit_tramp_VIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 10, (void *) interpreter_tramp_VIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7e\x7e", 5, (void *) jit_tramp_IIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7e\x7e", 5, (void *) interpreter_tramp_IIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7c", 2, (void *) jit_tramp_VD);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7c", 2, (void *) interpreter_tramp_VD);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7e", 6, (void *) jit_tramp_JIIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7e", 6, (void *) interpreter_tramp_JIIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7e\x7c", 4, (void *) jit_tramp_DIJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7e\x7c", 4, (void *) interpreter_tramp_DIJD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7e\x7e", 6, (void *) jit_tramp_VIJIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7e\x7e", 6, (void *) interpreter_tramp_VIJIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7d", 4, (void *) jit_tramp_VIJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7d", 4, (void *) interpreter_tramp_VIJF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7e\x7c\x7c", 6, (void *) jit_tramp_DIIJDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7e\x7c\x7c", 6, (void *) interpreter_tramp_DIIJDD);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7e\x7d", 4, (void *) jit_tramp_FIJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7e\x7d", 4, (void *) interpreter_tramp_FIJF);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7e", 3, (void *) jit_tramp_FIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7e", 3, (void *) interpreter_tramp_FIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7e", 3, (void *) jit_tramp_DIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7e", 3, (void *) interpreter_tramp_DIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7c", 4, (void *) jit_tramp_VIJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7c", 4, (void *) interpreter_tramp_VIJD);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7e\x7d\x7d", 6, (void *) jit_tramp_FIIJFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7e\x7d\x7d", 6, (void *) interpreter_tramp_FIIJFF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7f\x7f\x7f", 6, (void *) jit_tramp_IIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7f\x7f\x7f", 6, (void *) interpreter_tramp_IIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7e\x7f", 6, (void *) jit_tramp_VIIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7e\x7f", 6, (void *) interpreter_tramp_VIIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7f", 4, (void *) jit_tramp_JJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7f", 4, (void *) interpreter_tramp_JJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 11, (void *) jit_tramp_IIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 11, (void *) interpreter_tramp_IIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f\x7e", 4, (void *) jit_tramp_VJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f\x7e", 4, (void *) interpreter_tramp_VJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7e\x7f", 5, (void *) jit_tramp_JJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7e\x7f", 5, (void *) interpreter_tramp_JJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7c\x7c", 5, (void *) jit_tramp_IIJDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7c\x7c", 5, (void *) interpreter_tramp_IIJDD);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7d\x7d", 5, (void *) jit_tramp_IIJFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7d\x7d", 5, (void *) interpreter_tramp_IIJFF);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7c", 2, (void *) jit_tramp_JD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7c", 2, (void *) interpreter_tramp_JD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7e", 5, (void *) jit_tramp_VIJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7e", 5, (void *) interpreter_tramp_VIJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7e", 4, (void *) jit_tramp_JJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7e", 4, (void *) interpreter_tramp_JJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f", 3, (void *) jit_tramp_DII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f", 3, (void *) interpreter_tramp_DII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7d", 2, (void *) jit_tramp_VF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7d", 2, (void *) interpreter_tramp_VF);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f", 3, (void *) jit_tramp_FII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f", 3, (void *) interpreter_tramp_FII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7e\x7f", 5, (void *) jit_tramp_IIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7e\x7f", 5, (void *) interpreter_tramp_IIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7f\x7f", 5, (void *) jit_tramp_JJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7f\x7f", 5, (void *) interpreter_tramp_JJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7e\x7f\x7c\x7f", 5, (void *) jit_tramp_DJIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7e\x7f\x7c\x7f", 5, (void *) interpreter_tramp_DJIDI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_VIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_VIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7d\x7d\x7d", 4, (void *) jit_tramp_FFFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7d\x7d\x7d", 4, (void *) interpreter_tramp_FFFF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c\x7c\x7c", 4, (void *) jit_tramp_DDDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c\x7c\x7c", 4, (void *) interpreter_tramp_DDDD);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7e\x7f\x7f", 6, (void *) jit_tramp_JIJJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7e\x7f\x7f", 6, (void *) interpreter_tramp_JIJJII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7e", 2, (void *) jit_tramp_DJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7e", 2, (void *) interpreter_tramp_DJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f", 6, (void *) jit_tramp_VIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f", 6, (void *) interpreter_tramp_VIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7f", 6, (void *) jit_tramp_IIIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7f", 6, (void *) interpreter_tramp_IIIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7d\x7f", 4, (void *) jit_tramp_VIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7d\x7f", 4, (void *) interpreter_tramp_VIFI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7d", 4, (void *) jit_tramp_VIIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7d", 4, (void *) interpreter_tramp_VIIF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7e\x7c\x7c", 5, (void *) jit_tramp_DIJDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7e\x7c\x7c", 5, (void *) interpreter_tramp_DIJDD);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7e", 2, (void *) jit_tramp_FJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7e", 2, (void *) interpreter_tramp_FJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) jit_tramp_JIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 9, (void *) interpreter_tramp_JIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f\x7e", 6, (void *) jit_tramp_VIIJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f\x7e", 6, (void *) interpreter_tramp_VIIJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7e\x7e\x7e", 7, (void *) jit_tramp_VIJIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7e\x7e\x7e", 7, (void *) interpreter_tramp_VIJIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 8, (void *) jit_tramp_VIIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 8, (void *) interpreter_tramp_VIIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7e\x7e", 4, (void *) jit_tramp_VJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7e\x7e", 4, (void *) interpreter_tramp_VJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7e\x7d\x7d", 5, (void *) jit_tramp_FIJFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7e\x7d\x7d", 5, (void *) interpreter_tramp_FIJFF);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7e\x7e", 5, (void *) jit_tramp_JIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7e\x7e", 5, (void *) interpreter_tramp_JIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e", 5, (void *) jit_tramp_IIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e", 5, (void *) interpreter_tramp_IIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7e", 5, (void *) jit_tramp_VIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7e", 5, (void *) interpreter_tramp_VIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f", 5, (void *) jit_tramp_JIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f", 5, (void *) interpreter_tramp_JIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) jit_tramp_IIIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) interpreter_tramp_IIIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c\x7f", 3, (void *) jit_tramp_DDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c\x7f", 3, (void *) interpreter_tramp_DDI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7c", 4, (void *) jit_tramp_VIID);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7c", 4, (void *) interpreter_tramp_VIID);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7e", 4, (void *) jit_tramp_IJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7e", 4, (void *) interpreter_tramp_IJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f", 6, (void *) jit_tramp_JIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f", 6, (void *) interpreter_tramp_JIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7c", 3, (void *) jit_tramp_VID);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7c", 3, (void *) interpreter_tramp_VID);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f\x7e\x7e", 7, (void *) jit_tramp_VIIJIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f\x7e\x7e", 7, (void *) interpreter_tramp_VIIJIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7e\x7f", 5, (void *) jit_tramp_JJJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7e\x7f", 5, (void *) interpreter_tramp_JJJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7c", 5, (void *) jit_tramp_IIIID);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7c", 5, (void *) interpreter_tramp_IIIID);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 12, (void *) jit_tramp_IIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 12, (void *) interpreter_tramp_IIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7e\x7e\x7f\x7f", 6, (void *) jit_tramp_VJJJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7e\x7e\x7f\x7f", 6, (void *) interpreter_tramp_VJJJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f\x7e\x7e\x7e", 9, (void *) jit_tramp_VIIIJIJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f\x7e\x7e\x7e", 9, (void *) interpreter_tramp_VIIIJIJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7d\x7f", 3, (void *) jit_tramp_FFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7d\x7f", 3, (void *) interpreter_tramp_FFI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_JIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_JIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f\x7e\x7f\x7f", 9, (void *) jit_tramp_IIIIJIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f\x7e\x7f\x7f", 9, (void *) interpreter_tramp_IIIIJIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7d\x7c", 3, (void *) jit_tramp_FFD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7d\x7c", 3, (void *) interpreter_tramp_FFD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f\x7e\x7e", 8, (void *) jit_tramp_VIIIJIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7e\x7f\x7e\x7e", 8, (void *) interpreter_tramp_VIIIJIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 12, (void *) jit_tramp_VIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 12, (void *) interpreter_tramp_VIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7e\x7f", 7, (void *) jit_tramp_VIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7e\x7f", 7, (void *) interpreter_tramp_VIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7d", 5, (void *) jit_tramp_IIIIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7d", 5, (void *) interpreter_tramp_IIIIF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 13, (void *) jit_tramp_VIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 13, (void *) interpreter_tramp_VIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f\x7f\x7f\x7f", 6, (void *) jit_tramp_VJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f\x7f\x7f\x7f", 6, (void *) interpreter_tramp_VJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7c\x7f", 3, (void *) jit_tramp_IDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7c\x7f", 3, (void *) interpreter_tramp_IDI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7e", 6, (void *) jit_tramp_VIIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7e", 6, (void *) interpreter_tramp_VIIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7c\x7c", 3, (void *) jit_tramp_IDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7c\x7c", 3, (void *) interpreter_tramp_IDD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7e\x7f", 4, (void *) jit_tramp_VJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7e\x7f", 4, (void *) interpreter_tramp_VJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7e", 4, (void *) jit_tramp_IJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7e", 4, (void *) interpreter_tramp_IJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7e", 5, (void *) jit_tramp_IIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7e", 5, (void *) interpreter_tramp_IIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f\x7f\x7f", 5, (void *) jit_tramp_VJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f\x7f\x7f", 5, (void *) interpreter_tramp_VJIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7e", 7, (void *) jit_tramp_VIIIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7e", 7, (void *) interpreter_tramp_VIIIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7c", 5, (void *) jit_tramp_VIIID);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7c", 5, (void *) interpreter_tramp_VIIID);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7d\x7d", 3, (void *) jit_tramp_IFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7d\x7d", 3, (void *) interpreter_tramp_IFF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e", 7, (void *) jit_tramp_IIIIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e", 7, (void *) interpreter_tramp_IIIIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7d", 5, (void *) jit_tramp_VIIIF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7d", 5, (void *) interpreter_tramp_VIIIF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7d\x7d", 4, (void *) jit_tramp_VIFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7d\x7d", 4, (void *) interpreter_tramp_VIFF);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7d", 2, (void *) jit_tramp_DF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7d", 2, (void *) interpreter_tramp_DF);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7c", 2, (void *) jit_tramp_FD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7c", 2, (void *) interpreter_tramp_FD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f\x7e\x7e", 8, (void *) jit_tramp_VIIJIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f\x7e\x7e", 8, (void *) interpreter_tramp_VIIJIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7e\x7f\x7f", 6, (void *) jit_tramp_IIJJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7e\x7f\x7f", 6, (void *) interpreter_tramp_IIJJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 19, (void *) jit_tramp_IIIIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 19, (void *) interpreter_tramp_IIIIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 9, (void *) jit_tramp_VIIIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 9, (void *) interpreter_tramp_VIIIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7e", 4, (void *) jit_tramp_JJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7e", 4, (void *) interpreter_tramp_JJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7d\x7d\x7f", 5, (void *) jit_tramp_VIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7d\x7d\x7f", 5, (void *) interpreter_tramp_VIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7d", 3, (void *) jit_tramp_VJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7d", 3, (void *) interpreter_tramp_VJF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 13, (void *) jit_tramp_IIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 13, (void *) interpreter_tramp_IIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f", 8, (void *) jit_tramp_IIIIIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f", 8, (void *) interpreter_tramp_IIIIIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7d", 3, (void *) jit_tramp_IJF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7d", 3, (void *) interpreter_tramp_IJF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7c", 3, (void *) jit_tramp_VJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7c", 3, (void *) interpreter_tramp_VJD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 10, (void *) jit_tramp_VIIIIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 10, (void *) interpreter_tramp_VIIIIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 9, (void *) jit_tramp_IIIIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 9, (void *) interpreter_tramp_IIIIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7f\x7e\x7f\x7f", 7, (void *) jit_tramp_IIJIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7f\x7e\x7f\x7f", 7, (void *) interpreter_tramp_IIJIJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7e\x7e\x7e", 5, (void *) jit_tramp_VJJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7e\x7e\x7e", 5, (void *) interpreter_tramp_VJJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7c", 3, (void *) jit_tramp_IJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7c", 3, (void *) interpreter_tramp_IJD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) jit_tramp_VIIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) interpreter_tramp_VIIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 11, (void *) jit_tramp_VIIIIIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 11, (void *) interpreter_tramp_VIIIIIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 12, (void *) jit_tramp_IIIIIIIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 12, (void *) interpreter_tramp_IIIIIIIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 15, (void *) jit_tramp_IIIIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 15, (void *) interpreter_tramp_IIIIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 11, (void *) jit_tramp_IIIIIIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 11, (void *) interpreter_tramp_IIIIIIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7d", 2, (void *) jit_tramp_JF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7d", 2, (void *) interpreter_tramp_JF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f", 9, (void *) jit_tramp_IIIIIIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7e\x7f\x7f", 9, (void *) interpreter_tramp_IIIIIIJII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7e\x7f\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_VJIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7e\x7f\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_VJIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7e", 6, (void *) jit_tramp_IIIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7e", 6, (void *) interpreter_tramp_IIIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 11, (void *) jit_tramp_VIJIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7e\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 11, (void *) interpreter_tramp_VIJIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7e\x7f\x7e\x7e", 7, (void *) jit_tramp_JIJJIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7e\x7f\x7e\x7e", 7, (void *) interpreter_tramp_JIJJIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 17, (void *) jit_tramp_IIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 17, (void *) interpreter_tramp_IIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 21, (void *) jit_tramp_IIIIIIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f", 21, (void *) interpreter_tramp_IIIIIIIIIIIIIIIIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c\x7c\x7c\x7c", 5, (void *) jit_tramp_DDDDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c\x7c\x7c\x7c", 5, (void *) interpreter_tramp_DDDDD);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f\x7f\x7f", 6, (void *) jit_tramp_JIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f\x7f\x7f", 6, (void *) interpreter_tramp_JIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f\x7f", 5, (void *) jit_tramp_JIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f\x7f", 5, (void *) interpreter_tramp_JIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7d\x7f", 3, (void *) jit_tramp_IFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7d\x7f", 3, (void *) interpreter_tramp_IFI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7c\x7f", 3, (void *) jit_tramp_VDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7c\x7f", 3, (void *) interpreter_tramp_VDI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f", 6, (void *) jit_tramp_JIIJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f", 6, (void *) interpreter_tramp_JIIJII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) jit_tramp_JIIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f\x7f", 7, (void *) interpreter_tramp_JIIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7f", 4, (void *) jit_tramp_DIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7f", 4, (void *) interpreter_tramp_DIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e", 6, (void *) jit_tramp_IIIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e", 6, (void *) interpreter_tramp_IIIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f\x7e\x7f", 6, (void *) jit_tramp_JIJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f\x7e\x7f", 6, (void *) interpreter_tramp_JIJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7c\x7c", 4, (void *) jit_tramp_IJDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7c\x7c", 4, (void *) interpreter_tramp_IJDD);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7f\x7f", 5, (void *) jit_tramp_DIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7f\x7f", 5, (void *) interpreter_tramp_DIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_JIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_JIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 7, (void *) jit_tramp_IIIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7e\x7f", 7, (void *) interpreter_tramp_IIIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7f", 4, (void *) jit_tramp_FIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7f", 4, (void *) interpreter_tramp_FIII);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7c\x7f", 4, (void *) jit_tramp_VIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7c\x7f", 4, (void *) interpreter_tramp_VIDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7c\x7c\x7f\x7f", 5, (void *) jit_tramp_IDDII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7c\x7c\x7f\x7f", 5, (void *) interpreter_tramp_IDDII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7f\x7e\x7f", 6, (void *) jit_tramp_JJJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7f\x7e\x7f", 6, (void *) interpreter_tramp_JJJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7c\x7f", 4, (void *) jit_tramp_DIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7c\x7f", 4, (void *) interpreter_tramp_DIDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7c\x7c\x7f", 6, (void *) jit_tramp_IIIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7c\x7c\x7f", 6, (void *) interpreter_tramp_IIIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) jit_tramp_JIIJJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) interpreter_tramp_JIIJJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7c\x7f", 5, (void *) jit_tramp_VIIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7c\x7f", 5, (void *) interpreter_tramp_VIIDI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7c\x7f", 5, (void *) jit_tramp_DIIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7c\x7f", 5, (void *) interpreter_tramp_DIIDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f", 6, (void *) jit_tramp_IIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7f", 6, (void *) interpreter_tramp_IIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7f\x7f", 5, (void *) jit_tramp_FIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7f\x7f", 5, (void *) interpreter_tramp_FIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) jit_tramp_IIIJJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) interpreter_tramp_IIIJJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7e\x7f", 5, (void *) jit_tramp_IJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7e\x7f", 5, (void *) interpreter_tramp_IJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7e\x7f\x7e\x7f", 7, (void *) jit_tramp_IJIJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7e\x7f\x7e\x7f", 7, (void *) interpreter_tramp_IJIJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7f\x7c\x7f", 6, (void *) jit_tramp_DIIIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7f\x7c\x7f", 6, (void *) interpreter_tramp_DIIIDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7e\x7f", 7, (void *) jit_tramp_IIIIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7e\x7e\x7f", 7, (void *) interpreter_tramp_IIIIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7f\x7c\x7c\x7f", 7, (void *) jit_tramp_DIIIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7f\x7c\x7c\x7f", 7, (void *) interpreter_tramp_DIIIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_JIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_JIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7c\x7c\x7f", 7, (void *) jit_tramp_IIIIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7c\x7c\x7f", 7, (void *) interpreter_tramp_IIIIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7c\x7c\x7f", 5, (void *) jit_tramp_DIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7c\x7c\x7f", 5, (void *) interpreter_tramp_DIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7f\x7f\x7f", 6, (void *) jit_tramp_IJJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7f\x7f\x7f", 6, (void *) interpreter_tramp_IJJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7c\x7c\x7f", 5, (void *) jit_tramp_IIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7c\x7c\x7f", 5, (void *) interpreter_tramp_IIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7e\x7f", 7, (void *) jit_tramp_JIIIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7e\x7f", 7, (void *) interpreter_tramp_JIIIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7d\x7d\x7f", 5, (void *) jit_tramp_IIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7d\x7d\x7f", 5, (void *) interpreter_tramp_IIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_JJIIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7f\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_JJIIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7f", 6, (void *) jit_tramp_JIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7f", 6, (void *) interpreter_tramp_JIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_JJJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_JJJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7e\x7f", 5, (void *) jit_tramp_JIJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7e\x7f", 5, (void *) interpreter_tramp_JIJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7f\x7f\x7c\x7c\x7f", 6, (void *) jit_tramp_DIIDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7f\x7f\x7c\x7c\x7f", 6, (void *) interpreter_tramp_DIIDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7e", 6, (void *) jit_tramp_JIIJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7e", 6, (void *) interpreter_tramp_JIIJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7e\x7f\x7e", 5, (void *) jit_tramp_JIJIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7e\x7f\x7e", 5, (void *) interpreter_tramp_JIJIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7d\x7d\x7f", 7, (void *) jit_tramp_IIIIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7d\x7d\x7f", 7, (void *) interpreter_tramp_IIIIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7d\x7f", 5, (void *) jit_tramp_FIIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7d\x7f", 5, (void *) interpreter_tramp_FIIFI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7d\x7f", 4, (void *) jit_tramp_FIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7d\x7f", 4, (void *) interpreter_tramp_FIFI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7e\x7e\x7e\x7f", 6, (void *) jit_tramp_IIJJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7e\x7e\x7e\x7f", 6, (void *) interpreter_tramp_IIJJJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7c\x7c", 4, (void *) jit_tramp_IIDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7c\x7c", 4, (void *) interpreter_tramp_IIDD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7d\x7f", 6, (void *) jit_tramp_VIIIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7d\x7f", 6, (void *) interpreter_tramp_VIIIFI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7d\x7d\x7f", 6, (void *) jit_tramp_FIIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7d\x7d\x7f", 6, (void *) interpreter_tramp_FIIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7d\x7d\x7f", 6, (void *) jit_tramp_IIIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7d\x7d\x7f", 6, (void *) interpreter_tramp_IIIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7d\x7d", 4, (void *) jit_tramp_IIFF);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7d\x7d", 4, (void *) interpreter_tramp_IIFF);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) jit_tramp_VIIJJJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7e\x7e\x7e\x7f", 7, (void *) interpreter_tramp_VIIJJJI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7f\x7c\x7f", 6, (void *) jit_tramp_VIIIDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7f\x7c\x7f", 6, (void *) interpreter_tramp_VIIIDI);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7f\x7f\x7d\x7f", 5, (void *) jit_tramp_VIIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7f\x7f\x7d\x7f", 5, (void *) interpreter_tramp_VIIFI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7e\x7e\x7c", 4, (void *) jit_tramp_DJJD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7e\x7e\x7c", 4, (void *) interpreter_tramp_DJJD);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7f\x7d\x7f", 6, (void *) jit_tramp_FIIIFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7f\x7d\x7f", 6, (void *) interpreter_tramp_FIIIFI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7d\x7d\x7f", 5, (void *) jit_tramp_FIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7d\x7d\x7f", 5, (void *) interpreter_tramp_FIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7e\x7f", 7, (void *) jit_tramp_JIIJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7e\x7f", 7, (void *) interpreter_tramp_JIIJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7f\x7f\x7f\x7e\x7f", 7, (void *) jit_tramp_JJIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7f\x7f\x7f\x7e\x7f", 7, (void *) interpreter_tramp_JJIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7d\x7f\x7f\x7f\x7d\x7d\x7f", 7, (void *) jit_tramp_FIIIFFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7d\x7f\x7f\x7f\x7d\x7d\x7f", 7, (void *) interpreter_tramp_FIIIFFI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7e\x7f", 7, (void *) jit_tramp_IJIIIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7e\x7f", 7, (void *) interpreter_tramp_IJIIIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7c\x7c\x7c", 5, (void *) jit_tramp_IIDDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7c\x7c\x7c", 5, (void *) interpreter_tramp_IIDDD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7c\x7d", 3, (void *) jit_tramp_VDF);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7c\x7d", 3, (void *) interpreter_tramp_VDF);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e", 8, (void *) jit_tramp_IIIIIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7e", 8, (void *) interpreter_tramp_IIIIIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_JIIJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7e\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_JIIJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7e\x7e\x7e\x7e\x7e", 6, (void *) jit_tramp_JJJJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7e\x7e\x7e\x7e\x7e", 6, (void *) interpreter_tramp_JJJJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7e", 6, (void *) jit_tramp_JIIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7e\x7f\x7f\x7f\x7e\x7e", 6, (void *) interpreter_tramp_JIIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7c\x7c\x7c", 4, (void *) jit_tramp_IDDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7c\x7c\x7c", 4, (void *) interpreter_tramp_IDDD);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7c\x7c\x7c\x7c\x7c\x7c\x7c", 8, (void *) jit_tramp_VDDDDDDD);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7c\x7c\x7c\x7c\x7c\x7c\x7c", 8, (void *) interpreter_tramp_VDDDDDDD);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7e\x7f\x7f\x7f\x7f", 8, (void *) jit_tramp_IJJJIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7e\x7f\x7f\x7f\x7f", 8, (void *) interpreter_tramp_IJJJIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7f\x7f", 5, (void *) jit_tramp_IJJII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7f\x7f", 5, (void *) interpreter_tramp_IJJII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7e\x7f", 7, (void *) jit_tramp_IIIJIJI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7f\x7f\x7e\x7f\x7e\x7f", 7, (void *) interpreter_tramp_IIIJIJI);
  (void)hash_table_insert(&jit_trampolines, "\x7c\x7c\x7c\x7f", 4, (void *) jit_tramp_DDDI);
  (void)hash_table_insert(&interpreter_trampolines, "\x7c\x7c\x7c\x7f", 4, (void *) interpreter_tramp_DDDI);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7f\x7f", 7, (void *) jit_tramp_IJIIIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7f\x7f", 7, (void *) interpreter_tramp_IJIIIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7e\x7f\x7f\x7f", 7, (void *) jit_tramp_IJIJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7e\x7f\x7f\x7f", 7, (void *) interpreter_tramp_IJIJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7e\x7e", 5, (void *) jit_tramp_IJJJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7e\x7e", 5, (void *) interpreter_tramp_IJJJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7e\x7e\x7f\x7f\x7f", 7, (void *) jit_tramp_IJJJIII);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7e\x7e\x7f\x7f\x7f", 7, (void *) interpreter_tramp_IJJJIII);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f\x7e\x7e", 6, (void *) jit_tramp_IJIIJJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f\x7e\x7e", 6, (void *) interpreter_tramp_IJIIJJ);
  (void)hash_table_insert(&jit_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7e", 6, (void *) jit_tramp_IJIIIJ);
  (void)hash_table_insert(&interpreter_trampolines, "\x7f\x7e\x7f\x7f\x7f\x7e", 6, (void *) interpreter_tramp_IJIIIJ);
  (void)hash_table_insert(&jit_trampolines, "\x00\x7d\x7f", 3, (void *) jit_tramp_VFI);
  (void)hash_table_insert(&interpreter_trampolines, "\x00\x7d\x7f", 3, (void *) interpreter_tramp_VFI);
}
