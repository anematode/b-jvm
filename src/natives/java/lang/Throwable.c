#include <natives.h>

bool frame_mentions_object(bjvm_stack_frame *frame, bjvm_obj_header *obj) {
  // TODO filter by bitset to avoid false positives
  for (int i = 0; i < frame->max_locals + frame->max_stack; ++i) {
    if (frame->values[i].obj == obj) {
      return true;
    }
  }
  return false;
}

DECLARE_NATIVE("java/lang", Throwable, fillInStackTrace, "(I)Ljava/lang/Throwable;") {
  // Called in the constructor of Throwable. We therefore need to ignore
  // frames which are constructing the current object, which we can do by
  // inspecting the stack.

  bjvm_classdesc *StackTraceElement = bootstrap_class_create(
      thread, str("java/lang/StackTraceElement"));
  bjvm_link_class(thread, StackTraceElement);

  int i = thread->frames_count - 1;
  for (; i >= 0; --i) {
    bjvm_stack_frame *frame = thread->frames[i];
    // The first frame in which the exceptioned object is not mentioned
    if (!frame_mentions_object(frame, obj)) {
      break;
    }
  }

  // Create stack trace of length i + 1
  ++i;
  bjvm_obj_header *stack_trace = create_object_array(thread, StackTraceElement, i + 1);
  for (int j = 0; i >= 0; --i, ++j) {
    bjvm_stack_frame *frame = thread->frames[i];

    bjvm_obj_header *class_name = bjvm_intern_string(thread, hslc(frame->method->my_class->name));
    bjvm_obj_header *method_name = bjvm_intern_string(thread, frame->method->name);
    bjvm_obj_header *file_name = bjvm_intern_string(thread, frame->method->my_class->source_file->name);
    int line = bjvm_get_line_number(frame->method->code, frame->program_counter);

    struct bjvm_native_StackTraceElement *e = (void*)new_object(thread, StackTraceElement);
    e->declaringClass = class_name;
    e->methodName = method_name;
    e->fileName = file_name;
    e->lineNumber = line;
    *((struct bjvm_native_StackTraceElement **)array_data(stack_trace) + j) = e;
  }

  // Look up field "stackTrace" and set it to the stack trace
  bjvm_cp_field *field = bjvm_easy_field_lookup(obj->descriptor, str("backtrace"), str("Ljava/lang/Object;"));
  store_stack_value((void*)obj + field->byte_offset, (bjvm_stack_value) {.obj = stack_trace}, BJVM_TYPE_KIND_REFERENCE);

  return (bjvm_stack_value) {.obj = obj};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceDepth, "()I") {
  assert(argc == 0);

  bjvm_cp_field *field = bjvm_easy_field_lookup(obj->descriptor, str("backtrace"), str("Ljava/lang/Object;"));
  bjvm_obj_header *stack_trace = *(bjvm_obj_header **)((void*)obj + field->byte_offset);
  int length = *array_length(stack_trace);

  return (bjvm_stack_value) {.i = length};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceElement, "(I)Ljava/lang/StackTraceElement;") {
  assert(argc == 1);
  bjvm_cp_field *field = bjvm_easy_field_lookup(obj->descriptor, str("backtrace"), str("Ljava/lang/Object;"));
  bjvm_obj_header *stack_trace = *(bjvm_obj_header **)((void*)obj + field->byte_offset);
  return (bjvm_stack_value) {.obj = *((bjvm_obj_header **)array_data(stack_trace) + args[0].i)};
}