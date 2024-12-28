
#ifndef CLASSPATH_H
#define CLASSPATH_H

#include "adt.h"
#include "util.h"
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char *compressed_data; // pointer into the JAR memory
  uint32_t compressed_size;
  uint32_t claimed_uncompressed_size;
  bool is_compressed;
} bjvm_jar_entry;

// JAR that's mapped into memory (or, on the web, fully downloaded and plopped
// into memory to simulate mmapping). Long-term we'll develop a system which
// lets us load chunks on demand.
typedef struct {
  // Map of complete file name to bjvm_jar_entry
  bjvm_string_hash_table entries;

  char *data;
  uint32_t size_bytes;
  bool is_mmap; // true = mmap, false = heap allocation
} bjvm_mapped_jar;

typedef struct {
  // Fully qualified name to the file or folder
  heap_string name;
  // if this is a JAR, put it here; otherwise it's a folder
  bjvm_mapped_jar *jar;
} bjvm_classpath_entry;

// Classes will be sought for in JARs and folders in the order they're added.
typedef struct {
  bjvm_classpath_entry *entries;
  int entries_len;
  int entries_cap;
} bjvm_classpath;

// Returns 0 if all elements in the path were loaded ok.
[[nodiscard]] char *bjvm_init_classpath(bjvm_classpath *cp, bjvm_utf8 path);
void bjvm_free_classpath(bjvm_classpath *cp);

int bjvm_lookup_classpath(bjvm_classpath *cp, bjvm_utf8 filename,
                          uint8_t **bytes, size_t *len);

#ifdef __cplusplus
}
#endif

#endif // CLASSPATH_H
