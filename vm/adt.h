//
// Created by alec on 12/18/24.
//

#ifndef ADT_H
#define ADT_H

#include "util.h"
#include <stddef.h>
#include <types.h>

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
__attribute__((malloc, alloc_size(2, 3))) void *arena_alloc(arena *a, size_t count, size_t bytes);
slice arena_make_str(arena *a, const char *bytes, int len);
void arena_uninit(arena *a);

typedef struct {
  u64 *bits;
  u32 size_words;
} heap_bitset;

typedef struct {
  union {
    // Used if the # of bits is less than 63, and the lowest bit = 1
    u64 bits_inl;
    heap_bitset ptr;
  };
} compressed_bitset;

void init_compressed_bitset(compressed_bitset *bs, int bits_capacity);
void free_compressed_bitset(compressed_bitset bits);
bool is_bitset_compressed(compressed_bitset bits);
compressed_bitset empty_bitset();
void list_compressed_bitset_bits(compressed_bitset bits, int **stbds_vector);
bool test_compressed_bitset(compressed_bitset bits, size_t bit_index);
bool test_reset_compressed_bitset(compressed_bitset *bits, size_t bit_index);
bool test_set_compressed_bitset(compressed_bitset *bits, size_t bit_index);

typedef struct hash_table_entry {
  struct hash_table_entry *next;
  char *key;
  u32 key_len;
  void *data;
} hash_table_entry;

typedef struct string_hash_table_iterator {
  hash_table_entry *current_base;
  hash_table_entry *current;
  hash_table_entry *end;
} hash_table_iterator;

// Separate chaining hash map from (char) strings to void* (arbitrary)
// entries. Sigh.
typedef struct string_hash_table {
  void (*free)(void *entry);
  hash_table_entry *entries;

  size_t entries_count;
  size_t entries_cap;
  double load_factor;
} string_hash_table;

typedef struct string_builder {
  char *data;
  int write_pos;
} string_builder;

void string_builder_init(string_builder *builder);
void string_builder_append(string_builder *builder, const char *fmt, ...);
void string_builder_free(string_builder *builder);

string_hash_table make_hash_table(void (*free_fn)(void *), double load_factor, size_t initial_capacity);

hash_table_iterator hash_table_get_iterator(const string_hash_table *tbl);

bool hash_table_iterator_has_next(hash_table_iterator iter, char **key, size_t *key_len, void **value);

bool hash_table_iterator_next(hash_table_iterator *iter);

void hash_table_reserve(string_hash_table *tbl, size_t new_capacity);

bool hash_table_contains(const string_hash_table *tbl, const char *key, int len);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. Ownership of the key is passed into the function.
 */
void *hash_table_insert_impl(string_hash_table *tbl, char *key, int len, void *value, bool copy_key);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. If len = -1, the key is treated as a null-terminated string literal.
 */
[[nodiscard]] void *hash_table_insert(string_hash_table *tbl, const char *key, int len, void *value);

/**
 * Delete the key from the hash table and return the old value, if any. If len =
 * -1, the key is treated as a null-terminated string literal. Pass the result
 * of this function to the free function, as long as it accepts nullptr
 * pointers.
 */
[[nodiscard]] void *hash_table_delete(string_hash_table *tbl, const char *key, int len);

/**
 * Look up the value in the hash table.
 */
void *hash_table_lookup(const string_hash_table *tbl, const char *key, int len);

void free_hash_table(string_hash_table tbl);

#ifdef __cplusplus
}
#endif

#endif // ADT_H
