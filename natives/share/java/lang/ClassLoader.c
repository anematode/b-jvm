#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", ClassLoader, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", ClassLoader, findLoadedClass0, "(Ljava/lang/String;)Ljava/lang/Class;") {
  DCHECK(argc == 1);
  heap_string read = AsHeapString(args[0].handle->obj, on_oom);
  // Replace . with /
  for (u32 i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  classdesc *cd = hash_table_lookup(&thread->vm->classes, read.chars, read.len);
  free_heap_str(read);
  return (stack_value){.obj = cd ? (void *)get_class_mirror(thread, cd) : nullptr};

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, findBootstrapClass, "(Ljava/lang/String;)Ljava/lang/Class;") {
  DCHECK(argc == 1);
  heap_string read = AsHeapString(args[0].handle->obj, on_oom);
  // Replace . with /
  for (u32 i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  classdesc *cd = bootstrap_lookup_class_impl(thread, hslc(read), false);
  free_heap_str(read);
  return (stack_value){.obj = cd ? (void *)get_class_mirror(thread, cd) : nullptr};

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, findBuiltinLib, "(Ljava/lang/String;)Ljava/lang/String;") {
  return value_null();
}

static int incr = 0;

enum { CREATION_ANONYMOUS = 8 };

stack_value define_class(vm_thread *thread, handle *loader, handle *parent_class, handle *name, u8 *data_bytes,
                         int offset, int length, handle *pd, bool initialize, int flags, handle *source) {
  DCHECK(offset == 0);

  heap_string name_str = AsHeapString(name->obj, on_oom);
  // Replace . with / and then append . <random string>
  for (u32 i = 0; i < name_str.len; ++i)
    if (name_str.chars[i] == '.')
      name_str.chars[i] = '/';

  INIT_STACK_STRING(cf_name, 1000);
  if (flags & CREATION_ANONYMOUS) {
    cf_name = bprintf(cf_name, "%.*s.%d", fmt_slice(name_str), incr++);
  } else {
    cf_name = bprintf(cf_name, "%.*s", fmt_slice(name_str));
  }

  // Now append some random stuff to the name
  classdesc *result = define_bootstrap_class(thread, cf_name, data_bytes, length);
  result->classloader = loader->obj;

  free_heap_str(name_str);
  if (initialize) {
    initialize_class_t pox = {.args = {thread, result}};
    future_t fut = initialize_class(&pox);
    CHECK(fut.status == FUTURE_READY);
  }
  if (result) {
    return (stack_value){.obj = (void *)get_class_mirror(thread, result)};
  }

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass2,
               "(Ljava/lang/ClassLoader;Ljava/lang/String;Ljava/nio/ByteBuffer;IILjava/security/ProtectionDomain;Ljava/"
               "lang/String;)Ljava/lang/Class;") {
  handle *loader = args[0].handle; // ClassLoader
  handle *name = args[1].handle;   // String
  handle *b = args[2].handle;      // ByteBuffer
  int offset = args[3].i;          // int
  int length = args[4].i;          // int
  handle *pd = args[5].handle;     // ProtectionDomain
  // handle *source = args[6].handle;   // String

  u8 *data_bytes = (u8 *)LoadFieldLong(b->obj, "address");

  return define_class(thread, loader, nullptr, name, data_bytes, offset, length, pd, false, 0, nullptr);
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass1,
               "(Ljava/lang/ClassLoader;Ljava/lang/String;[BIILjava/security/ProtectionDomain;Ljava/lang/String;)Ljava/"
               "lang/Class;") {
  handle *loader = args[0].handle;
  handle *name = args[1].handle;
  handle *data = args[2].handle;
  int offset = args[3].i;
  int length = args[4].i;
  handle *pd = args[5].handle;

  DCHECK(length <= ArrayLength(data->obj));
  u8 *data_bytes = ArrayData(data->obj);

  return define_class(thread, loader, nullptr, name, data_bytes, offset, length, pd, false, 0, nullptr);
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass0,
               "(Ljava/lang/ClassLoader;Ljava/lang/Class;Ljava/lang/String;[BIILjava/security/ProtectionDomain;ZILjava/"
               "lang/Object;)Ljava/lang/Class;") {
  handle *loader = args[0].handle;
  handle *parent_class = args[1].handle;
  handle *name = args[2].handle;
  handle *data = args[3].handle;
  int offset = args[4].i;
  int length = args[5].i;
  handle *pd = args[6].handle;
  bool initialize = args[7].i;
  int flags = args[8].i;
  handle *source = args[9].handle;

  DCHECK(length <= ArrayLength(data->obj));
  u8 *data_bytes = ArrayData(data->obj);

  return define_class(thread, loader, parent_class, name, data_bytes, offset, length, pd, initialize, flags, source);
}