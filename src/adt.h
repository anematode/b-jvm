//
// Created by alec on 12/18/24.
//

#ifndef BJVM_ADT_H
#define BJVM_ADT_H

#include <stddef.h>
#include <stdint.h>
typedef struct {
  union {
    // Used if the number of bits in the local variable table/stack is less than
    // 64
    uint64_t bits_inl;
    struct {
      uint64_t *bits;
      uint32_t size_words;
    } ptr;
  };
} bjvm_compressed_bitset;

bjvm_compressed_bitset bjvm_init_compressed_bitset(int bits_capacity);
void bjvm_free_compressed_bitset(bjvm_compressed_bitset bits);
bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits);
bjvm_compressed_bitset bjvm_empty_bitset();
int *bjvm_list_compressed_bitset_bits(bjvm_compressed_bitset bits,
                                      int *existing_buf, int *length,
                                      int *capacity);
bool bjvm_test_compressed_bitset(bjvm_compressed_bitset bits, size_t bit_index);
bool bjvm_test_reset_compressed_bitset(bjvm_compressed_bitset *bits,
                                       size_t bit_index);
bool bjvm_test_set_compressed_bitset(bjvm_compressed_bitset *bits,
                                     size_t bit_index);

typedef struct bjvm_hash_table_entry {
  struct bjvm_hash_table_entry *next;
  wchar_t *key;
  uint32_t key_len;
  void *data;
} bjvm_hash_table_entry;

typedef struct bjvm_string_hash_table_iterator {
  bjvm_hash_table_entry *current_base;
  bjvm_hash_table_entry *current;
  bjvm_hash_table_entry *end;
} bjvm_hash_table_iterator;

// Separate chaining hash map from (wchar_t) strings to void* (arbitrary)
// entries. Sigh.
typedef struct bjvm_string_hash_table {
  void (*free_fn)(void *entry);
  bjvm_hash_table_entry *entries;

  size_t entries_count;
  size_t entries_cap;

  double load_factor;
} bjvm_string_hash_table;

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity);

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl);

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       wchar_t **key, size_t *key_len,
                                       void **value);

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter);

uint32_t bjvm_hash_string(const wchar_t *key, size_t len);

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. Ownership of the key is passed into the function.
 */
void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, wchar_t *key,
                                  int len, void *value, bool copy_key);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. If len = -1, the key is treated as a null-terminated string literal.
 */
[[nodiscard]] void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl,
                                           const wchar_t *key, int len,
                                           void *value);

/**
 * Delete the key from the hash table and return the old value, if any. If len =
 * -1, the key is treated as a null- terminated string literal. Pass the result
 * of this function to the free function, as long as it accepts nullptr pointers.
 */
[[nodiscard]] void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl,
                                           const wchar_t *key, int len);

/**
 * Look up the value in the hash table.
 */
void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len);

void bjvm_free_hash_table(bjvm_string_hash_table tbl);

#endif // BJVM_ADT_H