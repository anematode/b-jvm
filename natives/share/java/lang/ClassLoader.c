#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", ClassLoader, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", ClassLoader, findLoadedClass0, "(Ljava/lang/String;)Ljava/lang/Class;") {
  DCHECK(argc == 1);
  heap_string read = AsHeapString(args[0].handle->obj, on_oom);
  // Replace . with /
  for (u32 i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  bjvm_classdesc *cd = bjvm_hash_table_lookup(&thread->vm->classes, read.chars, read.len);
  free_heap_str(read);
  return (bjvm_stack_value){.obj = cd ? (void *)bjvm_get_class_mirror(thread, cd) : nullptr};

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
  bjvm_classdesc *cd = bootstrap_lookup_class_impl(thread, hslc(read), false);
  free_heap_str(read);
  return (bjvm_stack_value){.obj = cd ? (void *)bjvm_get_class_mirror(thread, cd) : nullptr};

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, findBuiltinLib, "(Ljava/lang/String;)Ljava/lang/String;") {
  return value_null();
}

static int incr = 0;

enum { CREATION_ANONYMOUS = 8 };

bjvm_stack_value define_class(bjvm_thread *thread, bjvm_handle *loader, bjvm_handle *parent_class, bjvm_handle *name,
                              u8 *data_bytes, int offset, int length, bjvm_handle *pd,
                              bool initialize, int flags, bjvm_handle *source) {
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
  // TODO when we do classloaders, obey that
  bjvm_classdesc *result = bjvm_define_bootstrap_class(thread, cf_name, data_bytes, length);

  free_heap_str(name_str);
  if (initialize) {
    bjvm_initialize_class_t pox = {.args = {thread, result}};
    future_t fut = bjvm_initialize_class(&pox);
    CHECK(fut.status == FUTURE_READY);
  }
  if (result) {
    return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, result)};
  }

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass2, "(Ljava/lang/ClassLoader;Ljava/lang/String;Ljava/nio/ByteBuffer;IILjava/security/ProtectionDomain;Ljava/lang/String;)Ljava/lang/Class;") {
  bjvm_handle *loader = args[0].handle;   // ClassLoader
  bjvm_handle *name = args[1].handle;     // String
  bjvm_handle *b = args[2].handle;        // ByteBuffer
  int offset = args[3].i;                 // int
  int length = args[4].i;                 // int
  bjvm_handle *pd = args[5].handle;       // ProtectionDomain
  // bjvm_handle *source = args[6].handle;   // String

  u8 *data_bytes = (u8 *)LoadFieldLong(b->obj, "address");

  return define_class(thread, loader, nullptr, name, data_bytes, offset, length, pd, false, 0, nullptr);
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass1,
               "(Ljava/lang/ClassLoader;Ljava/lang/String;[BIILjava/security/ProtectionDomain;Ljava/lang/String;)Ljava/"
               "lang/Class;") {
  bjvm_handle *loader = args[0].handle;
  bjvm_handle *name = args[1].handle;
  bjvm_handle *data = args[2].handle;
  int offset = args[3].i;
  int length = args[4].i;
  bjvm_handle *pd = args[5].handle;

  DCHECK(length <= *ArrayLength(data->obj));
  u8 *data_bytes = ArrayData(data->obj);

  return define_class(thread, loader, nullptr, name, data_bytes, offset, length, pd, false, 0, nullptr);
}

DECLARE_NATIVE("java/lang", ClassLoader, defineClass0,
               "(Ljava/lang/ClassLoader;Ljava/lang/Class;Ljava/lang/String;[BIILjava/security/ProtectionDomain;ZILjava/"
               "lang/Object;)Ljava/lang/Class;") {
  bjvm_handle *loader = args[0].handle;
  bjvm_handle *parent_class = args[1].handle;
  bjvm_handle *name = args[2].handle;
  bjvm_handle *data = args[3].handle;
  int offset = args[4].i;
  int length = args[5].i;
  bjvm_handle *pd = args[6].handle;
  bool initialize = args[7].i;
  int flags = args[8].i;
  bjvm_handle *source = args[9].handle;

  DCHECK(length <= *ArrayLength(data->obj));
  u8 *data_bytes = ArrayData(data->obj);

  return define_class(thread, loader, parent_class, name, data_bytes, offset, length, pd, initialize, flags, source);
}