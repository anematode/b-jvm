// Classpath utilities.

#ifndef CLASSPATH_H
#define CLASSPATH_H

#include "adt.h"
#include "util.h"
#include <stdlib.h>
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char *header; // pointer into the JAR memory
  u32 compressed_size;
  u32 claimed_uncompressed_size;
  bool is_compressed;
} jar_entry;

// JAR that's mapped into memory (or, on the web, fully downloaded and plopped
// into memory to simulate mmapping). Long-term we'll develop a system which
// lets us load chunks on demand.
typedef struct {
  // Map of complete file name to jar_entry
  string_hash_table entries;

  char *data;
  u32 size_bytes;
  bool is_mmap;    // true = mmap, false = heap allocation or WASMFS map
  bool needs_free; // false = WASMFS map
} mapped_jar;

typedef struct {
  // Fully qualified name to the file or folder
  heap_string name;
  // if this is a JAR, put it here; otherwise it's a folder
  mapped_jar *jar;
} classpath_entry;

// Classes will be sought for in JARs and folders in the order they're added.
typedef struct {
  classpath_entry *entries;
  heap_string as_colon_separated;
} classpath;

// Try to initialize the classpath using the colon-separated data in "path". Supported entries are JAR files and
// folders. (TODO index folders instead of rescanning each time)
// Returns nullptr if all elements in the path were loaded ok. Otherwise, returns a heap-allocated error message that
// is the caller's responsibility to free.
[[nodiscard]] char *init_classpath(classpath *cp, slice path);

// Free the classpath. (Does not call free(cp).)
void free_classpath(classpath *cp);

// Look for a classfile in the classpath.
// Example usage:
//   u8 *bytes; size_t len;
//   int failed = lookup_classpath(&cp, STR("java/lang/Object.class"), &bytes, &len);
int lookup_classpath(classpath *cp, slice filename, u8 **bytes, size_t *len);

#ifdef __cplusplus
}
#endif

#endif // CLASSPATH_H
