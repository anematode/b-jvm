#include "analysis.h"

#include <natives.h>

bool frame_mentions_object(bjvm_stack_frame *frame,
                           const bjvm_obj_header *obj) {
  for (int i = 0; i < frame->max_locals + frame->max_stack; ++i) {
    if (frame->values[i].obj == obj) {
      bjvm_compressed_bitset refs =
          ((bjvm_code_analysis *)frame->method->code_analysis)
              ->insn_index_to_references[frame->program_counter];
      if (bjvm_test_compressed_bitset(refs, i)) {
        return true;
      }
    }
  }
  return false;
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
      bootstrap_class_create(thread, STR("java/lang/StackTraceElement"));
  bjvm_link_class(thread, StackTraceElement);

  int i = thread->lang_exception_frame;
  if (i == -1) {
    // Not a lang exception, skip frames involved in constructing the object
    // TODO cleaner way of doing this?
    i = (int)thread->frames_count - 1;
    for (; i >= 0; --i) {
      bjvm_stack_frame *frame = thread->frames[i];
      // The first frame in which the exception object is not mentioned
      if (!frame_mentions_object(frame, obj->obj))
        break;
    }
    ++i;
  }

  // Create stack trace of the appropriate height
  bjvm_handle *stack_trace = bjvm_make_handle(
      thread, CreateObjectArray1D(thread, StackTraceElement, i + 1, true));
  if (!stack_trace->obj) // Failed to allocate
    return value_null();
  for (int j = 0; i >= 0; --i, ++j) {
    bjvm_stack_frame *frame = thread->frames[i];
    // Create the stack trace element
    bjvm_handle *e =
        bjvm_make_handle(thread, new_object(thread, StackTraceElement));
    if (!e->obj) // Failed to allocate StackTraceElement
      goto cleanup;

#define E ((struct bjvm_native_StackTraceElement *)e->obj)
    int line =
        bjvm_get_line_number(frame->method->code, frame->program_counter);
    E->declaringClass =
        bjvm_intern_string(thread, hslc(frame->method->my_class->name));
    E->methodName = bjvm_intern_string(thread, frame->method->name);
    bjvm_attribute_source_file *sf = frame->method->my_class->source_file;
    E->fileName = sf ? bjvm_intern_string(thread, sf->name) : nullptr;
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
  assert(argc == 0);

  return (bjvm_stack_value){.i = *ArrayLength(*backtrace_object(obj->obj))};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceElement,
               "(I)Ljava/lang/StackTraceElement;") {
  assert(argc == 1);
  bjvm_obj_header *stack_trace = *backtrace_object(obj->obj);
  int index = args[0].i;
  if (index < 0 || index >= *ArrayLength(stack_trace)) {
    return value_null();
  }
  bjvm_obj_header *element =
      *((bjvm_obj_header **)ArrayData(stack_trace) + index);
  return (bjvm_stack_value){.obj = element};
}