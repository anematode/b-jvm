//
// Created by Cowpox on 12/10/24.
//

#include "exceptions.h"
#include "reflection.h"

#include "../bjvm.h"
#include <emscripten/emscripten.h>

#include <roundrobin_scheduler.h>

EMSCRIPTEN_KEEPALIVE
vm *ffi_create_vm(const char *classpath, write_bytes stdout_, write_bytes stderr_) {
  vm_options options = default_vm_options();
  options.classpath = (slice){.chars = (char *)classpath, .len = (int)strlen(classpath)};
  options.write_stdout = stdout_;
  options.write_stderr = stderr_;
  options.stdio_override_param = nullptr;

  vm *vm = create_vm(options);
  return vm;
}

EMSCRIPTEN_KEEPALIVE
vm_thread *ffi_create_thread(vm *vm) {
  vm_thread *thr = create_thread(vm, default_thread_options());
  return thr;
}

EMSCRIPTEN_KEEPALIVE
classdesc *ffi_get_class(vm_thread *thr, const char *name) {
  classdesc *clazz = bootstrap_lookup_class(thr, (slice){.chars = (char *)name, .len = (int)strlen(name)});
  if (!clazz)
    return nullptr;
  initialize_class_t ctx = {.args = {thr, clazz}};
  future_t fut = initialize_class(&ctx);
  DCHECK(fut.status == FUTURE_READY);
  return clazz;
}

EMSCRIPTEN_KEEPALIVE
obj_header *ffi_get_current_exception(vm_thread *thr) { return thr->current_exception; }

EMSCRIPTEN_KEEPALIVE
void ffi_clear_current_exception(vm_thread *thr) { thr->current_exception = nullptr; }

EMSCRIPTEN_KEEPALIVE
classdesc *ffi_get_classdesc(obj_header *obj) { return obj->descriptor; }

bool check_casts(vm_thread *thread, cp_method *method, stack_value *args) {
  // Perform the moral equivalent of a checkcast instruction
  int argc = method_argc(method);
  for (int i = 0; i < argc; i++) {
    field_descriptor *arg;
    if (method->access_flags & ACCESS_STATIC) {
      arg = method->descriptor->args + i;
    } else {
      arg = i != 0 ? method->descriptor->args + (i - 1) : nullptr;
    }

    if (arg && field_to_kind(arg) != TYPE_KIND_REFERENCE)
      continue;
    if (!args[i].obj) // null
      continue;

    classdesc *class;
    if (arg) {
      INIT_STACK_STRING(str, 1000);
      str = unparse_field_descriptor(str, arg);
      class = load_class_of_field_descriptor(thread, str);
    } else {
      class = method->my_class;
    }

    printf("A: %.*s, B: %.*s\n", fmt_slice(class->name), fmt_slice(args[i].obj->descriptor->name));
    if (!instanceof(args[i].obj->descriptor, class)) {
      raise_class_cast_exception(thread, args[i].obj->descriptor, class);
      return true;
    }
  }
  return false;
}

EMSCRIPTEN_KEEPALIVE
rr_scheduler *ffi_create_rr_scheduler(vm *vm) {
  rr_scheduler *scheduler = malloc(sizeof(rr_scheduler));
  rr_scheduler_init(scheduler, vm);
  vm->scheduler = scheduler;
  return scheduler;
}

EMSCRIPTEN_KEEPALIVE
u32 ffi_rr_scheduler_wait_for_us(rr_scheduler *scheduler) { return rr_scheduler_may_sleep_us(scheduler); }

EMSCRIPTEN_KEEPALIVE
scheduler_status_t ffi_rr_scheduler_step(rr_scheduler *scheduler) { return rr_scheduler_step(scheduler); }

EMSCRIPTEN_KEEPALIVE
bool ffi_rr_record_is_ready(execution_record *record) { return record->status == SCHEDULER_RESULT_DONE; }

EMSCRIPTEN_KEEPALIVE
execution_record *ffi_rr_schedule(vm_thread *thread, cp_method *method, stack_value *args) {
  call_interpreter_t call = {.args = {thread, method, args}};
  CHECK(thread->vm->scheduler && "No scheduler set");
  return rr_scheduler_run(thread->vm->scheduler, call);
}

EMSCRIPTEN_KEEPALIVE
stack_value *ffi_get_execution_record_result_pointer(execution_record *record) {
  if (record->js_handle != -1) {
    void *result = (void *)deref_js_handle(record->vm, record->js_handle);
    record->returned.obj = result;
  }
  return &record->returned;
}

EMSCRIPTEN_KEEPALIVE
scheduler_status_t ffi_execute_immediately(execution_record *record) {
  return rr_scheduler_execute_immediately(record);
}

EMSCRIPTEN_KEEPALIVE
void ffi_free_execution_record(execution_record *record) { free_execution_record(record); }

void ffi_free_rr_scheduler(rr_scheduler *scheduler) { rr_scheduler_uninit(scheduler); }

EMSCRIPTEN_KEEPALIVE
call_interpreter_t *ffi_async_run(vm_thread *thread, cp_method *method, stack_value *args) {
  call_interpreter_t *ctx = malloc(sizeof(call_interpreter_t));

  if (check_casts(thread, method, args)) {
    return nullptr;
  }

  *ctx = (call_interpreter_t){.args = {thread, method, args}};
  return ctx;
}

EMSCRIPTEN_KEEPALIVE
object ffi_allocate_object(vm_thread *thr, cp_method *method) {
  classdesc *clazz = method->my_class;
  return AllocateObject(thr, clazz, clazz->instance_bytes);
}

EMSCRIPTEN_KEEPALIVE
bool ffi_run_step(call_interpreter_t *ctx, stack_value *result) {
  future_t fut = call_interpreter(ctx);
  if (fut.status == FUTURE_READY && result) {
    *result = ctx->_result;
  }
  return fut.status == FUTURE_READY;
}

EMSCRIPTEN_KEEPALIVE
void ffi_free_async_run_ctx(call_interpreter_t *ctx) { free(ctx); }

#define INSERT_METHOD()                                                                                                \
  char key[sizeof(void *)];                                                                                            \
  memcpy(key, &method, sizeof(void *));                                                                                \
  if (hash_table_insert(&field_names, key, sizeof(void *), (void *)1)) {                                               \
    continue;                                                                                                          \
  }

// Returns a TS ClassInfo as JSON
EMSCRIPTEN_KEEPALIVE
char *ffi_get_class_json(classdesc *desc) {
  cp_field **fields = nullptr;
  cp_method **methods = nullptr;

  printf("Class name: %.*s\n", fmt_slice(desc->name));

  // Collect fields from the class and its super classes
  for (classdesc *s = desc; s->super_class; s = s->super_class->classdesc) {
    for (int i = 0; i < s->fields_count; i++) {
      cp_field *f = &s->fields[i];
      if (f->access_flags & ACCESS_PUBLIC) {
        arrput(fields, f);
      }
    }
  }

  string_hash_table field_names = make_hash_table(nullptr, 0.75, 16);

  // Collect methods from the vtable/itable. Only include non-abstract methods. Keep track of duplicate methods.
  for (int i = 0; i < arrlen(desc->vtable.methods); i++) {
    cp_method *method = desc->vtable.methods[i];
    if (method->access_flags & ACCESS_ABSTRACT) {
      continue;
    }
    INSERT_METHOD()
    arrput(methods, method);
  }

  for (int i = 0; i < arrlen(desc->itables.entries); i++) {
    itable itable = desc->itables.entries[i];
    for (int j = 0; j < arrlen(itable.methods); j++) {
      itable_method_t method = itable.methods[j];
      if (method & ITABLE_METHOD_BIT_INVALID) {
        continue;
      }
      INSERT_METHOD()
      arrput(methods, (cp_method *)method);
    }
  }

  // Also collect static methods and constructors
  for (int i = 0; i < desc->methods_count; i++) {
    cp_method *method = &desc->methods[i];
    if ((method->access_flags & ACCESS_STATIC && !method->is_clinit) || method->is_ctor) {
      INSERT_METHOD()
      arrput(methods, method);
    }
  }

  free_hash_table(field_names);

  int field_index = 0;
  int method_index = 0;

  string_builder out;
  string_builder_init(&out);
  string_builder_append(&out, R"({"binaryName":"%s","fields":[)", desc->name.chars);

  for (int i = 0; i < arrlen(fields); i++) {
    cp_field *f = fields[i];
    if (i > 0)
      string_builder_append(&out, ",");
    string_builder_append(&out, R"({"name":"%s","type":"%s","accessFlags":%d,"byteOffset":%d,"index":%d})",
                          f->name.chars, field_to_kind(&f->parsed_descriptor), f->access_flags, f->byte_offset,
                          field_index++);
  }

  string_builder_append(&out, R"(],"methods":[)");

  for (int i = 0; i < arrlen(methods); i++) {
    cp_method *m = methods[i];
    if (i > 0)
      string_builder_append(&out, ",");
    string_builder_append(
        &out, R"({"name":"%s","index":%d,"descriptor":"%s","methodPointer":%d,"accessFlags":%d,"parameterNames":[)",
        m->name.chars, method_index++, m->unparsed_descriptor.chars, (intptr_t)m, m->access_flags);

    for (int attrib_i = 0; attrib_i < m->attributes_count; ++attrib_i) {
      if (m->attributes[attrib_i].kind == ATTRIBUTE_KIND_METHOD_PARAMETERS) {
        attribute_method_parameters *params = &m->attributes[attrib_i].method_parameters;

        for (int j = 0; j < params->count; j++) {
          if (j > 0)
            string_builder_append(&out, ",");
          string_builder_append(&out, "\"%s\"", params->params[j].name.chars);
        }

        goto found;
      }
    }

    for (int j = 0; j < m->descriptor->args_count; j++) {
      if (j > 0)
        string_builder_append(&out, ",");
      string_builder_append(&out, "\"arg%d\"", j);
    }

  found:
    string_builder_append(&out, "]}");
  }

  string_builder_append(&out, "]}");
  char *result = strdup(out.data);
  string_builder_free(&out);
  return result;
}

int main() {}