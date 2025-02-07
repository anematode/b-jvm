#include "analysis.h"

#include <linkage.h>
#include <natives-dsl.h>

// Returns true if the frame is currently constructing the given object.
static bool is_frame_constructing(bjvm_stack_frame *frame, object obj) {
  if (!frame->method->is_ctor || bjvm_is_frame_native(frame) || frame->num_locals < 1) {
    return false;
  }
  return frame_locals(frame)[0].obj == obj;
}

// Implementation-dependent field where we can store the stack trace
bjvm_obj_header **backtrace_object(bjvm_obj_header *throwable) {
  return &((struct bjvm_native_Throwable *)throwable)->backtrace;
}

DECLARE_NATIVE("java/lang", Throwable, fillInStackTrace,
               "(I)Ljava/lang/Throwable;") {
  // Called in the constructor of Throwable. We therefore need to ignore
  // frames which are constructing the current object, which we can do by
  // inspecting the stack.
  bjvm_classdesc *StackTraceElement =
      bootstrap_lookup_class(thread, STR("java/lang/StackTraceElement"));
  bjvm_link_class(thread, StackTraceElement);

  // Find the first frame which is not an initializer of the current exception
  int i = (int)thread->frames_count - 3 /* skip fillInStackTrace(void) and fillInStackTrace(I) */;
  for (; i >= 0; --i) {
    bjvm_stack_frame *frame = thread->frames[i];
    if (!is_frame_constructing(frame, obj->obj)) {
      break;
    }
  }

  // Create stack trace of the appropriate height
  bjvm_handle *stack_trace = bjvm_make_handle(
      thread, CreateObjectArray1D(thread, StackTraceElement, i + 1));
  if (!stack_trace->obj) // Failed to allocate
    return value_null();

  ((struct bjvm_native_Throwable *)obj->obj)->depth = i + 1;

  for (int j = 0; i >= 0; --i, ++j) {
    // Check that all frames have an non-null method
    for (int i = 0; i < thread->frames_count; ++i) {
      DCHECK(bjvm_get_frame_method(thread->frames[i]));
    }

    bjvm_stack_frame *frame = thread->frames[i];
    // Create the stack trace element
    bjvm_handle *e =
        bjvm_make_handle(thread, new_object(thread, StackTraceElement));
    if (!e->obj) // Failed to allocate StackTraceElement
      goto cleanup;

    bjvm_cp_method *method = bjvm_get_frame_method(frame);

#define E ((struct bjvm_native_StackTraceElement *)e->obj)
    int line =
        bjvm_is_frame_native(frame)
            ? -1
            : bjvm_get_line_number(method->code, frame->plain.program_counter);
    E->declaringClassObject = (void*) bjvm_get_class_mirror(thread, method->my_class);
    E->declaringClass =
        MakeJStringFromModifiedUTF8(thread, hslc(method->my_class->name), true);
    E->methodName = MakeJStringFromModifiedUTF8(thread, method->name, true);
    bjvm_attribute_source_file *sf = method->my_class->source_file;
    E->fileName = sf ? MakeJStringFromModifiedUTF8(thread, sf->name, true) : nullptr;
    E->lineNumber = line;
    *((void **)ArrayData(stack_trace->obj) + j) = e->obj;
#undef E
    bjvm_drop_handle(thread, e);
  }

cleanup:
  *backtrace_object(obj->obj) = stack_trace->obj;
  bjvm_drop_handle(thread, stack_trace);

  return (bjvm_stack_value){.obj = obj->obj};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceDepth, "()I") {
  DCHECK(argc == 0);
  return (bjvm_stack_value){.i = *ArrayLength(*backtrace_object(obj->obj))};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceElement,
               "(I)Ljava/lang/StackTraceElement;") {
  DCHECK(argc == 1);
  bjvm_obj_header *stack_trace = *backtrace_object(obj->obj);
  int index = args[0].i;
  if (index < 0 || index >= *ArrayLength(stack_trace)) {
    return value_null();
  }
  bjvm_obj_header *element =
      *((bjvm_obj_header **)ArrayData(stack_trace) + index);
  return (bjvm_stack_value){.obj = element};
}

DECLARE_NATIVE("java/lang", StackTraceElement, initStackTraceElements, "([Ljava/lang/StackTraceElement;Ljava/lang/Object;I)V") {
  bjvm_handle *stack_trace = args[1].handle;
  int depth = *ArrayLength(stack_trace->obj);
  int array_length = *ArrayLength(args[0].handle->obj);
  if (array_length < depth) {
    depth = array_length;
  }
  for (int i = 0; i < depth; ++i) {
    bjvm_obj_header *element = *((bjvm_obj_header **)ArrayData(stack_trace->obj) + i);
    ReferenceArrayStore(args[0].handle->obj, i, element);
  }
  return value_null();
}