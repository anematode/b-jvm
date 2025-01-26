
#include <natives.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

static bjvm_obj_header* create_unix_exception(bjvm_thread *thread, int errno_code) {
  bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, STR("sun/nio/fs/UnixException"));
  bjvm_obj_header *obj = new_object(thread, classdesc);

  bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("(I)V"), true, false);
  int result = bjvm_thread_run_leaf(thread, method, (bjvm_stack_value[]){{.obj = obj}, {.i = errno_code}}, nullptr);
  assert(result == 0);

  return obj;
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, init, "()I") {
  return value_null();
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, getcwd, "()[B") {
  INIT_STACK_STRING(cwd, 1024);
  char* p = getcwd(cwd.chars, 1024);
  if (p == nullptr) {
    return value_null();
  }
  cwd.len = strlen(cwd.chars);
  bjvm_obj_header *array = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, cwd.len);
  if (!array) {
    return value_null();
  }
  memcpy(ArrayData(array), cwd.chars, cwd.len);
  return (bjvm_stack_value){.obj = array};
}


DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, stat0, "(JLsun/nio/fs/UnixFileAttributes;)I") {
  struct stat st;

  if (!args[1].handle)
    return value_null();

  uintptr_t buf = args[0].l;
  int result = stat((char*)buf, &st);
  if (result)
    return (bjvm_stack_value){.i = errno};

  bjvm_obj_header *attrs = args[1].handle->obj;

#define MapAttrLong(name, value) StoreFieldLong(attrs, STR(#name), value)
#define MapAttrInt(name, value) StoreFieldInt(attrs, STR(#name), value)

  MapAttrInt(st_mode, st.st_mode);
  MapAttrLong(st_ino, st.st_ino);
  MapAttrLong(st_dev, st.st_dev);
  MapAttrLong(st_rdev, st.st_rdev);
  MapAttrInt(st_nlink, st.st_nlink);
  MapAttrInt(st_uid, st.st_uid);
  MapAttrInt(st_gid, st.st_gid);
  MapAttrLong(st_size, st.st_size);

#ifdef __APPLE__
#define suffix(x) x ## espec
#else
#define suffix(x) x
#endif

  MapAttrLong(st_atime_sec, st.suffix(st_atim).tv_sec);
  MapAttrLong(st_atime_nsec, st.suffix(st_atim).tv_nsec);

  MapAttrLong(st_mtime_sec, st.suffix(st_mtim).tv_sec);
  MapAttrLong(st_mtime_nsec, st.suffix(st_mtim).tv_nsec);

  MapAttrLong(st_ctime_sec, st.suffix(st_ctim).tv_sec);
  MapAttrLong(st_ctime_nsec, st.suffix(st_ctim).tv_nsec);

#ifdef __APPLE__
  MapAttrLong(st_birthtime_sec, st.st_birthtimespec.tv_sec);
  MapAttrLong(st_birthtime_nsec, st.st_birthtimespec.tv_nsec);
#endif

#undef MapAttrLong
  #undef MapAttrInt

  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, open0, "(JII)I") {
  int result = open((char const*)args[0].l, args[1].i, args[2].i);
  if (result >= 0) return (bjvm_stack_value){.i = result};

  thread->current_exception = create_unix_exception(thread, errno);
  return value_null();
}

DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, size0, "(Ljava/io/FileDescriptor;)J") {
  assert(args[0].handle->obj);
  int fd = LoadFieldInt(args[0].handle->obj, "fd");
  struct stat st;
  int result = fstat(fd, &st);
  if (result) {
    thread->current_exception = create_unix_exception(thread, errno);
    return value_null();
  }
  return (bjvm_stack_value){.l = st.st_size};
}

DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, allocationGranularity0, "()J") {
  return (bjvm_stack_value){.l = 4096};
}

// (fd: FileDescriptor, prot:Int, pos: Long, len: Long, isSync: Boolean) -> Long
DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, map0, "(Ljava/io/FileDescriptor;IJJZ)J") {
  assert(args[0].handle->obj);
  int fd = LoadFieldInt(args[0].handle->obj, "fd");
  int prot = args[1].i;
  off_t pos = args[2].l;
  off_t len = args[3].l;
  int flags = args[4].i ? MAP_SHARED : MAP_PRIVATE;
  void* result = mmap(nullptr, len, prot | PROT_READ, flags, fd, pos);
  if (result == MAP_FAILED) {
    thread->current_exception = create_unix_exception(thread, errno);
    return value_null();
  }
  return (bjvm_stack_value){.l = (int64_t)result};
}