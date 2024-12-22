#include "analysis.h"

#include <natives.h>

bool frame_mentions_object(bjvm_stack_frame *frame, bjvm_obj_header *obj) {
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

  int i = thread->frames_count - 1;
  for (; i >= 0; --i) {
    bjvm_stack_frame *frame = thread->frames[i];
    // The first frame in which the exception object is not mentioned
    if (!frame_mentions_object(frame, obj))
      break;
  }

  // Create stack trace of the appropriate height
  ++i;
  bjvm_obj_header *stack_trace =
      CreateObjectArray1D(thread, StackTraceElement, i + 1);
  // TODO UAF here if we GC while creating the stack trace
  for (int j = 0; i >= 0; --i, ++j) {
    bjvm_stack_frame *frame = thread->frames[i];
    bjvm_obj_header *class_name =
        bjvm_intern_string(thread, hslc(frame->method->my_class->name));
    bjvm_obj_header *method_name =
        bjvm_intern_string(thread, frame->method->name);
    bjvm_obj_header *file_name =
        bjvm_intern_string(thread, frame->method->my_class->source_file->name);
    int line =
        bjvm_get_line_number(frame->method->code, frame->program_counter);
    struct bjvm_native_StackTraceElement *e =
        (void *)new_object(thread, StackTraceElement);
    e->declaringClass = class_name;
    e->methodName = method_name;
    e->fileName = file_name;
    e->lineNumber = line;
    *((struct bjvm_native_StackTraceElement **)ArrayData(stack_trace) + j) = e;
  }

  *backtrace_object(obj) = stack_trace;
  return (bjvm_stack_value){.obj = obj};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceDepth, "()I") {
  assert(argc == 0);

  return (bjvm_stack_value){.i = *ArrayLength(*backtrace_object(obj))};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceElement,
               "(I)Ljava/lang/StackTraceElement;") {
  assert(argc == 1);
  bjvm_obj_header *stack_trace = *backtrace_object(obj);
  int index = args[0].i;
  if (index < 0 || index >= *ArrayLength(stack_trace)) {
    return value_null();
  }
  bjvm_obj_header *element =
      *((bjvm_obj_header **)ArrayData(stack_trace) + index);
  return (bjvm_stack_value){.obj = element};
}