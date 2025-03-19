#include "analysis.h"

#include <linkage.h>
#include <natives-dsl.h>

// Returns true if the frame is currently constructing the given object.
static bool is_frame_part_of_throwable_construction(stack_frame *frame, object obj) {
  if (utf8_equals(frame->method->name, "fillInStackTrace")) { // hack for now
    return true;
  }
  if (!frame->method->is_ctor || is_frame_native(frame) || frame->num_locals < 1) {
    return false;
  }
  return frame_locals(frame)[0].obj == obj;
}

// Implementation-dependent field where we can store the stack trace
obj_header **backtrace_object(obj_header *throwable) { return &((struct native_Throwable *)throwable)->backtrace; }

DECLARE_NATIVE("java/lang", Throwable, fillInStackTrace, "(I)Ljava/lang/Throwable;") {
  // Called in the constructor of Throwable. We therefore need to ignore
  // frames which are constructing the current object, which we can do by
  // inspecting the stack.
  classdesc *StackTraceElement = cached_classes(thread->vm)->stack_trace_element;

  // Find the first frame which is not an initializer of the current exception
  stack_frame *frame = thread->stack.top;
  while (frame) {
    if (!is_frame_part_of_throwable_construction(frame, obj->obj)) {
      break;
    }
    frame = frame->prev;
  }

  // Count frames until base
  stack_frame *tmp = frame;
  int n_frames = 0;
  while (tmp) {
    tmp = tmp->prev;
    ++n_frames;
  }

  // Now n_frames is the number of frames in [ frame, frame->prev, ..., first frame ]

  // Create stack trace of the appropriate height
  handle *stack_trace = make_handle(thread, CreateObjectArray1D(thread, StackTraceElement, n_frames));
  if (!stack_trace->obj) // Failed to allocate
    return value_null();

  ((struct native_Throwable *)obj->obj)->depth = n_frames;

  for (int j = 0; n_frames > 0; --n_frames, ++j) {
    DCHECK(frame);

    // Create the stack trace element
    handle *e = make_handle(thread, new_object(thread, StackTraceElement));
    if (!e->obj) // Failed to allocate StackTraceElement
      goto cleanup;

    cp_method *method = frame->method;

#define E ((struct native_StackTraceElement *)e->obj)
    int line = is_frame_native(frame) ? -1 : get_line_number(method->code, frame->program_counter);
    object o = (void *)get_class_mirror(thread, method->my_class);
    if (!o)
      goto oom;
    E->declaringClassObject = o;
    o = MakeJStringFromModifiedUTF8(thread, method->my_class->name, true);
    if (!o)
      goto oom;
    E->declaringClass = o;
    o = MakeJStringFromModifiedUTF8(thread, method->name, true);
    if (!o)
      goto oom;
    E->methodName = o;
    attribute_source_file *sf = method->my_class->source_file;
    if (sf) {
      o = MakeJStringFromModifiedUTF8(thread, sf->name, true);
      if (!o)
        goto oom;
    }

    E->fileName = o;
    E->lineNumber = line;
    *((void **)ArrayData(stack_trace->obj) + j) = e->obj;

#if 0
    fprintf(stderr, "Stack trace element %d: %.*s.%.*s (%s:%d)\n", j, fmt_slice(method->my_class->name), fmt_slice(method->name),
            sf ? sf->name.chars : "unknown", line);
#endif
#undef E
    drop_handle(thread, e);
    frame = frame->prev;
  }

cleanup:
  *backtrace_object(obj->obj) = stack_trace->obj;
oom:
  drop_handle(thread, stack_trace);
  return (stack_value){.obj = obj->obj};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceDepth, "()I") {
  DCHECK(argc == 0);
  return (stack_value){.i = ArrayLength(*backtrace_object(obj->obj))};
}

DECLARE_NATIVE("java/lang", Throwable, getStackTraceElement, "(I)Ljava/lang/StackTraceElement;") {
  DCHECK(argc == 1);
  obj_header *stack_trace = *backtrace_object(obj->obj);
  int index = args[0].i;
  if (index < 0 || index >= ArrayLength(stack_trace)) {
    return value_null();
  }
  obj_header *element = *((obj_header **)ArrayData(stack_trace) + index);
  return (stack_value){.obj = element};
}

DECLARE_NATIVE("java/lang", StackTraceElement, initStackTraceElements,
               "([Ljava/lang/StackTraceElement;Ljava/lang/Object;I)V") {
  handle *stack_trace = args[1].handle;
  int depth = ArrayLength(stack_trace->obj);
  int array_length = ArrayLength(args[0].handle->obj);
  if (array_length < depth) {
    depth = array_length;
  }
  for (int i = 0; i < depth; ++i) {
    obj_header *element = *((obj_header **)ArrayData(stack_trace->obj) + i);
    ReferenceArrayStore(args[0].handle->obj, i, element);
  }
  return value_null();
}