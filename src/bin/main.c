//
// Created by Cowpox on 12/10/24.
//

#include <emscripten/emscripten.h>
#include "../bjvm.h"

EMSCRIPTEN_KEEPALIVE
bjvm_vm *bjvm_ffi_create_vm(const char* classpath, bjvm_write_byte stdout_, bjvm_write_byte stderr_) {
  bjvm_vm_options options = bjvm_default_vm_options();
  options.classpath = (bjvm_utf8){ .chars=(char*)classpath, .len=(int)strlen(classpath)};
  options.write_stdout = +stdout_;
  options.write_stderr = +stderr_;
  options.write_byte_param = nullptr;

  bjvm_vm *vm = bjvm_create_vm(options);
  return vm;
}

EMSCRIPTEN_KEEPALIVE
bjvm_thread *bjvm_ffi_create_thread(bjvm_vm *vm) {
  bjvm_thread *thr = bjvm_create_thread(vm, bjvm_default_thread_options());
  return thr;
}

EMSCRIPTEN_KEEPALIVE
bjvm_async_run_ctx *bjvm_ffi_run_main(bjvm_thread *thr) {
  bjvm_classdesc *desc = bootstrap_lookup_class(thr, STR("Main"));
  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method;
  bjvm_initialize_class(thr, desc);

  method = bjvm_method_lookup(desc, STR("main"),
                                   STR("([Ljava/lang/String;)V"), false, false);

  bjvm_async_run_ctx *ctx = bjvm_thread_async_run(thr, method, args, nullptr);
  return ctx;
}

EMSCRIPTEN_KEEPALIVE
bjvm_classdesc *bjvm_ffi_get_class(bjvm_thread *thr, const char *name) {
  bjvm_classdesc *clazz = bootstrap_lookup_class(thr, (bjvm_utf8){.chars=(char*)name, .len=(int)strlen(name)});
  if (!clazz) return nullptr;
  bjvm_initialize_class(thr, clazz);  // TODO step this
  return clazz;
}

EMSCRIPTEN_KEEPALIVE
bjvm_obj_header *bjvm_ffi_get_current_exception(bjvm_thread *thr) {
  return thr->current_exception;
}

EMSCRIPTEN_KEEPALIVE
void bjvm_ffi_clear_current_exception(bjvm_thread *thr) {
  thr->current_exception = nullptr;
}

EMSCRIPTEN_KEEPALIVE
bjvm_classdesc *bjvm_ffi_get_classdesc(bjvm_obj_header *obj) {
  return obj->descriptor;
}

EMSCRIPTEN_KEEPALIVE
bool bjvm_ffi_run_step(bjvm_async_run_ctx *ctx) {
  return bjvm_async_run_step(ctx);
}


// Returns a TS ClassInfo as JSON
EMSCRIPTEN_KEEPALIVE
char *bjvm_ffi_get_class_json(bjvm_classdesc *desc) {
  bjvm_cp_field** fields = nullptr;
  bjvm_cp_method** methods = nullptr;

  // Collect fields from the class and its super classes
  for (bjvm_classdesc *s = desc; s->super_class; s = s->super_class->classdesc) {
    for (int i = 0; i < s->fields_count; i++) {
      bjvm_cp_field *f = &s->fields[i];
      if (f->access_flags & BJVM_ACCESS_PUBLIC) {
        arrput(fields, f);
      }
    }
  }

  // Collect methods from the vtable/itable. Only include non-abstract methods
  for (int i = 0; i < arrlen(desc->vtable.methods); i++) {
    bjvm_cp_method *method = desc->vtable.methods[i];
    if (method->access_flags & BJVM_ACCESS_ABSTRACT) {
      continue;
    }
    arrput(methods, method);
  }

  for (int i = 0; i < arrlen(desc->itables.entries); i++) {
    bjvm_itable itable = desc->itables.entries[i];
    for (int j = 0; j < arrlen(itable.methods); j++) {
      bjvm_itable_method_t method = itable.methods[j];
      if (method & BJVM_ITABLE_METHOD_BIT_INVALID) {
        continue;
      }
      arrput(methods, (bjvm_cp_method *)method);
    }
  }

  // Also collect static methods
  for (int i = 0; i < desc->methods_count; i++) {
    bjvm_cp_method *method = &desc->methods[i];
    if (method->access_flags & BJVM_ACCESS_STATIC && !(utf8_equals(method->name, "<clinit>"))) {
      arrput(methods, method);
    }
  }

  bjvm_string_builder out;
  bjvm_string_builder_init(&out);
  bjvm_string_builder_append(&out, R"({"binaryName":"%s","fields":[)", desc->name.chars);

  for (int i = 0; i < arrlen(fields); i++) {
    bjvm_cp_field *f = fields[i];
    if (i > 0)
      bjvm_string_builder_append(&out, ",");
    bjvm_string_builder_append(&out, R"({"name":"%s","type":"%s","accessFlags":%d,"byteOffset":%d})",
      f->name.chars, field_to_kind(&f->parsed_descriptor), f->access_flags, f->byte_offset);
  }

  bjvm_string_builder_append(&out, R"(],"methods":[)");

  for (int i = 0; i < arrlen(methods); i++) {
    bjvm_cp_method *m = methods[i];
    if (i > 0)
      bjvm_string_builder_append(&out, ",");
    bjvm_string_builder_append(&out, R"({"name":"%s","descriptor":"%s","methodPointer":%d,"accessFlags":%d,"parameterNames":[)",
      m->name.chars, m->unparsed_descriptor.chars, (intptr_t)m, m->access_flags);

    for (int attrib_i = 0; attrib_i < m->attributes_count; ++attrib_i) {
      if (m->attributes[attrib_i].kind == BJVM_ATTRIBUTE_KIND_METHOD_PARAMETERS) {
        bjvm_attribute_method_parameters *params = &m->attributes[attrib_i].method_parameters;

        for (int j = 0; j < params->count; j++) {
          if (j > 0)
            bjvm_string_builder_append(&out, ",");
          bjvm_string_builder_append(&out, "\"%s\"", params->params[j].name.chars);
        }

        goto found;
      }
    }

    for (int j = 0; j < m->descriptor->args_count; j++) {
      if (j > 0)
        bjvm_string_builder_append(&out, ",");
      bjvm_string_builder_append(&out, "\"arg%d\"", j);
    }

    found:
    bjvm_string_builder_append(&out, "]}");
  }

  bjvm_string_builder_append(&out, "]}");
  char *result = strdup(out.data);
  bjvm_string_builder_free(&out);
  return result;
}

EMSCRIPTEN_KEEPALIVE
int print_error(void *ctx) {
  bjvm_async_run_ctx *run_ctx = (bjvm_async_run_ctx *)ctx;
  bjvm_thread *thr = run_ctx->thread;
  if (thr->current_exception) {
    printf("Exception was raised!\n");
    // bjvm_thread_run printStackTrace
    bjvm_stack_value args[1] = {{.obj = thr->current_exception}};
    bjvm_cp_method *method = bjvm_method_lookup(
            thr->current_exception->descriptor, STR("printStackTrace"), STR("()V"), true, false);
    thr->current_exception = nullptr;
    bjvm_thread_run(thr, method, args, nullptr);
  }
  return 0;
}

int main() {

}