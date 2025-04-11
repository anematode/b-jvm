//
// Created by alec on 12/18/24.
//

#ifndef ADT_H
#define ADT_H

#include "util.h"

#include "../vendor/stb_ds.h"
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arena_region {
  struct arena_region *next; // null if final segment
  size_t used, capacity;
  alignas(16) char data[]; // todo: make the alignment portable instead of hard coding
} arena_region;

typedef struct {
  arena_region *begin;
} arena;

void arena_init(arena *a);
__attribute__((malloc, alloc_size(2, 3))) void *arena_alloc(arena *a, size_t count, size_t bytes);
slice arena_make_str(arena *a, const char *bytes, int len);
void arena_uninit(arena *a);

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
