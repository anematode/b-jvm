#include <monitors.h>
#include <natives-dsl.h>
#include <roundrobin_scheduler.h>

DECLARE_NATIVE("java/lang", Object, hashCode, "()I") {
  return (stack_value){.i = (s32)get_object_hash_code(thread->vm, obj->obj)};
}

// Check whether the class is cloneable.
static bool cloneable(vm *vm, classdesc *cd) { return instanceof(cd, cached_classes(vm)->cloneable); }

DECLARE_NATIVE("java/lang", Object, clone, "()Ljava/lang/Object;") {
  switch (obj->obj->descriptor->kind) {
  case CD_KIND_ORDINARY_ARRAY: {
    obj_header *new_array = CreateObjectArray1D(thread, obj->obj->descriptor->one_fewer_dim, ArrayLength(obj->obj));
    if (new_array) {
      memcpy(ArrayData(new_array), ArrayData(obj->obj), ArrayLength(obj->obj) * sizeof(void *));
    }
    return (stack_value){.obj = new_array};
  }
  case CD_KIND_ORDINARY: {
    // Check if the object is Cloneable
    if (!cloneable(thread->vm, obj->obj->descriptor)) {
      raise_vm_exception_no_msg(thread, STR("java/lang/CloneNotSupportedException"));
      return value_null();
    }

    obj_header *new_obj = new_object(thread, obj->obj->descriptor);
    if (new_obj) {
      memcpy(new_obj + 1, obj->obj + 1, obj->obj->descriptor->instance_bytes - sizeof(obj_header));
    }
    return (stack_value){.obj = new_obj};
  }
  case CD_KIND_PRIMITIVE_ARRAY: {
    obj_header *new_array =
        CreatePrimitiveArray1D(thread, obj->obj->descriptor->primitive_component, ArrayLength(obj->obj));
    if (!new_array) {
      return value_null();
    }
    memcpy(ArrayData(new_array), ArrayData(obj->obj),
           ArrayLength(obj->obj) * sizeof_type_kind(obj->obj->descriptor->primitive_component));
    return (stack_value){.obj = new_array};
  }
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang", Object, getClass, "()Ljava/lang/Class;") {
  return (stack_value){.obj = (void *)get_class_mirror(thread, obj->obj->descriptor)};
}

DECLARE_NATIVE("java/lang", Object, notifyAll, "()V") {
  u32 hold_count = current_thread_hold_count(thread, obj->obj);
  if (hold_count == 0) {
    raise_vm_exception(thread, STR("java/lang/IllegalMonitorStateException"),
                       STR("Thread does not hold monitor before waiting"));
    return value_null();
  }

  rr_scheduler *scheduler = thread->vm->scheduler;
  assert(scheduler && "Cannot synchronize without a scheduler!");
  monitor_notify_all(scheduler, obj->obj);

  return value_null();
}

DECLARE_NATIVE("java/lang", Object, notify, "()V") {
  u32 hold_count = current_thread_hold_count(thread, obj->obj);
  if (hold_count == 0) {
    raise_vm_exception(thread, STR("java/lang/IllegalMonitorStateException"),
                       STR("Thread does not hold monitor before waiting"));
    return value_null();
  }

  rr_scheduler *scheduler = thread->vm->scheduler;
  assert(scheduler && "Cannot synchronize without a scheduler!");
  monitor_notify_one(scheduler, obj->obj);

  return value_null();
}

DECLARE_ASYNC_NATIVE("java/lang", Object, wait0, "(J)V", locals(u32 hold_count; rr_wakeup_info wakeup_info),
                     invoked_methods(invoked_method(monitor_reacquire_hold_count))) {
  assert(argc == 1);
  s64 timeoutMillis = args[0].l;
  assert(timeoutMillis >= 0); // this is always checked before calling this private method

  if (thread->thread_obj->interrupted) {
    thread->thread_obj->interrupted = false; // throw and reset flag
    raise_vm_exception(thread, STR("java/lang/InterruptedException"), STR("Thread interrupted before monitor waiting"));
    ASYNC_RETURN_VOID();
  }

  self->hold_count = monitor_release_all_hold_count(thread, obj->obj);
  if (self->hold_count == 0) {
    raise_vm_exception(thread, STR("java/lang/IllegalMonitorStateException"),
                       STR("Thread does not hold monitor before waiting"));
    ASYNC_RETURN_VOID();
  }

  self->wakeup_info.kind = RR_MONITOR_WAIT;
  self->wakeup_info.wakeup_us = timeoutMillis == 0 ? 0 : get_unix_us() + timeoutMillis * 1000;
  self->wakeup_info.monitor_wakeup.monitor = obj; // already handlized
  self->wakeup_info.monitor_wakeup.ready = false;
  ASYNC_YIELD((void *)&self->wakeup_info);

  // wake up: re-acquire the monitor
  AWAIT(monitor_reacquire_hold_count, thread, obj->obj, self->hold_count);
  assert(get_async_result(monitor_reacquire_hold_count) == 0);

  if (thread->thread_obj->interrupted) {
    thread->thread_obj->interrupted = false; // throw and reset flag
    raise_vm_exception(thread, STR("java/lang/InterruptedException"), STR("Thread interrupted before monitor waiting"));
    ASYNC_RETURN_VOID();
  }

  ASYNC_END_VOID();
}
