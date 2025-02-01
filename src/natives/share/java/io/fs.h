#ifndef FS_H
#define FS_H

#include <stdint.h>

typedef enum : int32_t {
  BA_EXISTS = 0x01,
  BA_REGULAR = 0x02,
  BA_DIRECTORY = 0x04,
  BA_HIDDEN = 0x08
} boolean_attributes;

typedef enum {
  FS_OK = 0,
  FS_PERMISSION_DENIED,
  FS_INVALID_PATH,
  FS_DOES_NOT_EXIST,
  FS_INVALID_OPERATION,
  FS_GENERIC_ERROR,
} fs_result;

typedef struct {
  fs_result (*get_attributes)(slice file_name, boolean_attributes *result);
  void (*create_virtual_file)(slice file_name, boolean_attributes attributes, char const* data, size_t size);
} fs;

static inline slice fs_result_to_string(fs_result result) {
  switch (result) {
  case FS_OK:
    return STR("FS_OK");
  case FS_DOES_NOT_EXIST:
    return STR("No such file or directory");
  case FS_INVALID_PATH:
    return STR("Invalid path");
  case FS_INVALID_OPERATION:
    return STR("Invalid operation on entity");
  default:
  case FS_GENERIC_ERROR:
    return STR("Unknown error");
  }
}

#endif