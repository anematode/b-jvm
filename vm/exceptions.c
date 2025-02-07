#include <exceptions.h>
#include <objects.h>

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj) {
#if AGGRESSIVE_DEBUG
  printf("Raising exception of type %s\n", obj->descriptor->name);
#endif

#define T ((struct bjvm_native_Throwable *)obj)
  if (thread->frames_count > 0) {
    bjvm_stack_frame *frame = thread->frames[thread->frames_count - 1];
    if (!bjvm_is_frame_native(frame)) {
      T->faulting_insn = frame->plain.program_counter;
      T->method = frame->method;
    }
  }
  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int bjvm_raise_vm_exception(bjvm_thread *thread, const slice exception_name, slice msg_utf8) {
  bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, exception_name);
  DCHECK(!thread->current_exception);
  DCHECK(classdesc->state == BJVM_CD_STATE_INITIALIZED && "VM-generated exceptions should be initialised at VM boot");

  // Create the exception object
  bjvm_handle *handle = bjvm_make_handle(thread, new_object(thread, classdesc));

  if (msg_utf8.chars) {
    object msg = MakeJStringFromModifiedUTF8(thread, msg_utf8, false);
    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("(Ljava/lang/String;)V"), true, false);
    call_interpreter_synchronous(thread, method, (bjvm_stack_value[]){{.obj = handle->obj}, {.obj = msg}}); // no return val, it's a constructor
  } else {
    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, (bjvm_stack_value[]){{.obj = handle->obj}}); // no return val, it's a constructor
  }

#ifndef EMSCRIPTEN
  // fprintf(stderr, "Exception: %.*s: %.*s\n", fmt_slice(exception_name), fmt_slice(exception_string));
#endif
  bjvm_raise_exception_object(thread, handle->obj);

  bjvm_drop_handle(thread, handle);
  return 0;
}