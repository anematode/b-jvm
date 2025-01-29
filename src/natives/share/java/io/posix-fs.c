//
// Created by alec on 1/19/25.
//

#include "posix-fs.h"

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define POSIX_SUPPORTED
#endif

#ifdef POSIX_SUPPORTED
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

typedef struct {
  fs_result (*original_get_attributes)(bjvm_utf8 file_name, boolean_attributes *result);
} attributes;

bool posix_fs_supported(void) { return true; }
void posix_fs_init(unixlike_fs *fs) {
  fs->inherited_data = malloc(sizeof(attributes));
  /// todo: cleanup the inheritance stuff
  ((attributes*)fs->inherited_data)->original_get_attributes = fs->fs.get_attributes;
  fs->fs.get_attributes = posix_fs_get_attributes;
}

static boolean_attributes _convert_attrs(char const *name, struct stat *st) {
  boolean_attributes attrs = BA_EXISTS;

  if (S_ISREG(st->st_mode))
    attrs |= BA_REGULAR;
  if (S_ISDIR(st->st_mode))
    attrs |= BA_DIRECTORY;
  if (name[0] == '.')
    attrs |= BA_HIDDEN;

  return attrs;
}

static int _posix_stat(bjvm_utf8 file_name, struct stat *st) {
  int err = stat(file_name.chars, st);
  return (err == 0) ? 0 : errno;
}

static fs_result _posix_convert_error(int e) {
  switch (e) {
  case 0:
    UNREACHABLE();

  case EACCES:
    return FS_PERMISSION_DENIED;

  case ENAMETOOLONG:
  case ENOTDIR:
    return FS_INVALID_PATH;

  case ENOENT:
    return FS_DOES_NOT_EXIST;

  default:
    return FS_GENERIC_ERROR;
  }
}

fs_result posix_fs_get_attributes(bjvm_utf8 file_name, boolean_attributes *attrs) {
  struct stat st;
  int err;

  attributes *self = unix_get_active_fs()->inherited_data;
  if (self->original_get_attributes) {
    err = self->original_get_attributes(file_name, attrs);
    if (err == FS_OK)
      return FS_OK;
  }

  if (file_name.len == 0) {
    err = ENOENT;
    goto error;
  }

  err = _posix_stat(file_name, &st);
  if (err)
    goto error;

  *attrs = _convert_attrs(file_name.chars, &st);
  return 0;

error:
  *attrs = 0;
  err = _posix_convert_error(err);
  if (err == FS_DOES_NOT_EXIST) // this is fine because it's reflected in attrs
    err = FS_OK;

  return err;
}

#else
bool posix_fs_supported(void) { return false; }

void posix_fs_init(unixlike_fs const **fs) { UNREACHABLE("POSIX fs not supported on this target platform"); }
#endif
