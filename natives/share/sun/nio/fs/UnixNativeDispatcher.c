
#include <natives-dsl.h>
#ifdef EMSCRIPTEN
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#else
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static obj_header *create_unix_exception(vm_thread *thread, int errno_code) {
  classdesc *classdesc = cached_classes(thread->vm)->unix_exception;
  obj_header *obj = new_object(thread, classdesc);

  cp_method *method = method_lookup(classdesc, STR("<init>"), STR("(I)V"), true, false);
  call_interpreter_synchronous(thread, method,
                               (stack_value[]){{.obj = obj}, {.i = errno_code}}); // constructor is void method

  return obj;
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, init, "()I") { return value_null(); }

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, getcwd, "()[B") {
  INIT_STACK_STRING(cwd, 1024);
  char *p = getcwd(cwd.chars, 1024);
  if (p == nullptr) {
    return value_null();
  }
  cwd.len = strlen(cwd.chars);
  obj_header *array = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, cwd.len);
  if (!array) {
    return value_null();
  }
  memcpy(ArrayData(array), cwd.chars, cwd.len);
  return (stack_value){.obj = array};
}

stack_value stat_impl(value *args) {
  struct stat st;

  if (!args[1].handle)
    return value_null();

  uintptr_t buf = args[0].l;
  int result = stat((char *)buf, &st);
  if (result)
    return (stack_value){.i = errno};

  obj_header *attrs = args[1].handle->obj;

#define MapAttrLong(name, value) StoreFieldLong(attrs, (#name), value)
#define MapAttrInt(name, value) StoreFieldInt(attrs, (#name), value)
  MapAttrInt(st_mode, st.st_mode);
  MapAttrLong(st_ino, st.st_ino);
  MapAttrLong(st_dev, st.st_dev);
  MapAttrLong(st_rdev, st.st_rdev);
  MapAttrInt(st_nlink, st.st_nlink);
  MapAttrInt(st_uid, st.st_uid);
  MapAttrInt(st_gid, st.st_gid);
  MapAttrLong(st_size, st.st_size);

#ifdef __APPLE__
  MapAttrLong(st_atime_sec, st.st_atime);
  MapAttrLong(st_atime_nsec, st.st_atimensec);

  MapAttrLong(st_mtime_sec, st.st_mtime);
  MapAttrLong(st_mtime_nsec, st.st_mtimensec);

  MapAttrLong(st_ctime_sec, st.st_ctime);
  MapAttrLong(st_ctime_nsec, st.st_ctimensec);
#else
  MapAttrLong(st_atime_sec, st.st_atim.tv_sec);
  MapAttrLong(st_atime_nsec, st.st_atim.tv_nsec);

  MapAttrLong(st_mtime_sec, st.st_mtim.tv_sec);
  MapAttrLong(st_mtime_nsec, st.st_mtim.tv_nsec);

  MapAttrLong(st_ctime_sec, st.st_ctim.tv_sec);
  MapAttrLong(st_ctime_nsec, st.st_ctim.tv_nsec);
#endif

#ifdef __APPLE__
  MapAttrLong(st_birthtime_sec, st.st_birthtime);
  MapAttrLong(st_birthtime_nsec, st.st_birthtimensec);
#endif

#undef MapAttrLong
#undef MapAttrInt

  return (stack_value){.i = 0};
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, stat0, "(JLsun/nio/fs/UnixFileAttributes;)I") {
  return stat_impl(args);
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, lstat0, "(JLsun/nio/fs/UnixFileAttributes;)V") {
  stat_impl(args);
  return value_null();
}

// DIR *opendir(const char* dirname)
DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, opendir0, "(J)J") {
  DIR *dir = opendir((char const *)args[0].l);
  if (!dir) {
    thread->current_exception = create_unix_exception(thread, errno);
    return value_null();
  }
  return (stack_value){.l = (s64)dir};
}

// struct dirent* readdir(DIR *dirp), return  dirent->d_name
DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, readdir0, "(J)[B") {
  struct dirent *entry = readdir((DIR *)args[0].l);
  if (!entry) {
    return value_null();
  }
  size_t len = strlen(entry->d_name);
  obj_header *array = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, len);
  if (!array) {
    return value_null();
  }
  memcpy(ArrayData(array), entry->d_name, len);
  return (stack_value){.obj = array};
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, closedir, "(J)V") {
  closedir((DIR *)args[0].l);
  return value_null();
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, open0, "(JII)I") {
  int result = open((char const *)args[0].l, args[1].i, args[2].i);
  if (result >= 0)
    return (stack_value){.i = result};

  thread->current_exception = create_unix_exception(thread, errno);
  return value_null();
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, access0, "(JI)I") {
  int result = access((char const *)args[0].l, args[1].i);
  if (result == 0)
    return (stack_value){.i = 0};

  thread->current_exception = create_unix_exception(thread, errno);
  return value_null();
}

DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, size0, "(Ljava/io/FileDescriptor;)J") {
  DCHECK(args[0].handle->obj);
  int fd = LoadFieldInt(args[0].handle->obj, "fd");
  struct stat st;
  int result = fstat(fd, &st);
  if (result) {
    thread->current_exception = create_unix_exception(thread, errno);
    return value_null();
  }
  return (stack_value){.l = st.st_size};
}

DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, allocationGranularity0, "()J") { return (stack_value){.l = 4096}; }

// (fd: FileDescriptor, prot:Int, pos: Long, len: Long, isSync: Boolean) -> Long
DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, map0, "(Ljava/io/FileDescriptor;IJJZ)J") {
  DCHECK(args[0].handle->obj);
  int fd = LoadFieldInt(args[0].handle->obj, "fd");
  int prot = args[1].i;
  off_t pos = args[2].l;
  off_t len = args[3].l;
  int flags = args[4].i ? MAP_SHARED : MAP_PRIVATE;
  void *result = mmap(nullptr, len, prot | PROT_READ, flags, fd, pos);
  if (result == MAP_FAILED) {
    thread->current_exception = create_unix_exception(thread, errno);
    return value_null();
  }
  arrput(thread->vm->mmap_allocations, ((mmap_allocation){result, len}));
  return (stack_value){.l = (s64)result};
}

DECLARE_NATIVE("sun/nio/ch", UnixFileDispatcherImpl, unmap0, "(JJ)V") {
  void *addr = (void *)args[0].l;
  size_t len = args[1].l;
  for (size_t i = 0; i < arrlenu(thread->vm->mmap_allocations); ++i) {
    if (thread->vm->mmap_allocations[i].ptr == addr) {
      arrdelswap(thread->vm->mmap_allocations, i);
      break;
    }
  }
  int result = munmap(addr, len);
  if (result) {
    thread->current_exception = create_unix_exception(thread, errno);
  }
  return value_null();
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, strerror, "(I)[B") {
  char *msg = strerror(args[0].i);
  size_t len = strlen(msg);
  obj_header *array = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, len);
  if (!array) {
    return value_null();
  }
  memcpy(ArrayData(array), msg, len);
  return (stack_value){.obj = array};
}

DECLARE_NATIVE("sun/nio/ch", FileDispatcherImpl, init0, "()V") { return value_null(); }