//
// Created by alec on 12/18/24.
//

#ifndef BJVM_ADT_H
#define BJVM_ADT_H

#include "util.h"

#include "../vendor/stb_ds.h"
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_region {
  struct arena_region *next; // null if final segment
  size_t used, capacity;
  __attribute((aligned(8))) char data[];
} arena_region;

typedef struct {
  arena_region *begin;
} arena;

void arena_init(arena *a);
__attribute__((malloc, alloc_size(2, 3))) void *
arena_alloc(arena *a, size_t count, size_t bytes);
bjvm_utf8 arena_make_str(arena *a, const char *bytes, int len);
void arena_uninit(arena *a);

typedef struct {
  union {
    // Used if the # of bits is less than 63, and the lowest bit = 1
    uint64_t bits_inl;
    struct {
      uint64_t *bits;
      uint32_t size_words;
    } ptr;
  };
} bjvm_compressed_bitset;

void bjvm_init_compressed_bitset(bjvm_compressed_bitset *bs, int bits_capacity);
void bjvm_free_compressed_bitset(bjvm_compressed_bitset bits);
bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits);
bjvm_compressed_bitset bjvm_empty_bitset();
[[nodiscard]] int *bjvm_list_compressed_bitset_bits(bjvm_compressed_bitset bits,
                                                    int *existing_buf,
                                                    int *length, int *capacity);
bool bjvm_test_compressed_bitset(bjvm_compressed_bitset bits, size_t bit_index);
bool bjvm_test_reset_compressed_bitset(bjvm_compressed_bitset *bits,
                                       size_t bit_index);
bool bjvm_test_set_compressed_bitset(bjvm_compressed_bitset *bits,
                                     size_t bit_index);
int bjvm_count_compressed_bitset(bjvm_compressed_bitset bits);

typedef struct bjvm_hash_table_entry {
  struct bjvm_hash_table_entry *next;
  char *key;
  uint32_t key_len;
  void *data;
} bjvm_hash_table_entry;

typedef struct bjvm_string_hash_table_iterator {
  bjvm_hash_table_entry *current_base;
  bjvm_hash_table_entry *current;
  bjvm_hash_table_entry *end;
} bjvm_hash_table_iterator;

// Separate chaining hash map from (char) strings to void* (arbitrary)
// entries. Sigh.
typedef struct bjvm_string_hash_table {
  void (*free)(void *entry);
  bjvm_hash_table_entry *entries;

  size_t entries_count;
  size_t entries_cap;
  double load_factor;
} bjvm_string_hash_table;

typedef struct bjvm_string_builder {
  char *data;
  int write_pos;
} bjvm_string_builder;

void bjvm_string_builder_init(bjvm_string_builder *builder);
void bjvm_string_builder_append(bjvm_string_builder *builder, const char *fmt,
                                ...);
void bjvm_string_builder_free(bjvm_string_builder *builder);

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity);

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl);

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       char **key, size_t *key_len,
                                       void **value);

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter);

void bjvm_hash_table_reserve(bjvm_string_hash_table *tbl, size_t new_capacity);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. Ownership of the key is passed into the function.
 */
void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, char *key,
                                  int len, void *value, bool copy_key);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. If len = -1, the key is treated as a null-terminated string literal.
 */
[[nodiscard]] void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl,
                                           const char *key, int len,
                                           void *value);

/**
 * Delete the key from the hash table and return the old value, if any. If len =
 * -1, the key is treated as a null-terminated string literal. Pass the result
 * of this function to the free function, as long as it accepts nullptr
 * pointers.
 */
[[nodiscard]] void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl,
                                           const char *key, int len);

/**
 * Look up the value in the hash table.
 */
void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const char *key,
                             int len);

void bjvm_free_hash_table(bjvm_string_hash_table tbl);

#ifdef __cplusplus
}
#endif

#endif // BJVM_ADT_H
