//
// Created by alec on 1/19/25.
//

#include "unixlike-fs.h"

#include "posix-fs.h"

unixlike_fs active_fs = {};

typedef struct {
  boolean_attributes attrs;
  size_t length;
  char data[];
} virtual_file;

static void create_virtual_file(bjvm_utf8 file_name, boolean_attributes attributes, char const *data, size_t size);
static fs_result get_attributes(bjvm_utf8 file_name, boolean_attributes *result);

__attribute__((constructor)) static void init() {
  active_fs.fs.create_virtual_file = create_virtual_file;
  active_fs.fs.get_attributes = get_attributes;
  active_fs.synthetic_entries = bjvm_make_hash_table(free, 0.75, 16);

  create_virtual_file(STR("./nio.bjvm_lib"), BA_EXISTS | BA_REGULAR, "", 0);

  if (posix_fs_supported()) {
    posix_fs_init(&active_fs);
  }
}

void create_virtual_file(bjvm_utf8 file_name, boolean_attributes attributes, char const *data, size_t size) {
  virtual_file *file = (virtual_file *)malloc(sizeof(virtual_file) + size);
  file->attrs = attributes;
  file->length = size;
  memcpy(file->data, data, size);

  void *result = bjvm_hash_table_insert(&active_fs.synthetic_entries, file_name.chars, file_name.len, file);
  if (result) free(result);
}

fs_result get_attributes(bjvm_utf8 file_name, boolean_attributes *result) {
  virtual_file *virtual_file = bjvm_hash_table_lookup(&active_fs.synthetic_entries, file_name.chars, file_name.len);
  if (virtual_file) {
    *result = virtual_file->attrs;
    return FS_OK;
  }

  return FS_DOES_NOT_EXIST;
}

unixlike_fs const* unix_get_active_fs(void) {
  return &active_fs;
}