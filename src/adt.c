//
// Created by alec on 12/18/24.
//

#include "adt.h"
#include "util.h"

#include <stdlib.h>
#include <stdarg.h>

bjvm_compressed_bitset bjvm_empty_bitset() {
  return (bjvm_compressed_bitset){.bits_inl = 1};
}

bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits) {
  return (bits.bits_inl & 1) != 0; // pointer is aligned
}

void bjvm_free_compressed_bitset(bjvm_compressed_bitset bits) {
  if (!bjvm_is_bitset_compressed(bits))
    free(bits.ptr.bits);
}

/**
 * List all set bits, starting from 0, in the given bitset. Stores the list into
 * the given buffer, which must have the existing length in words (or
 * existing_buf = nullptr, length = 0). Returns a (possibly reallocated) buffer.
 *
 * Used to follow references during garbage collection.
 */
int *bjvm_list_compressed_bitset_bits(bjvm_compressed_bitset bits,
                                      int *existing_buf, int *length,
                                      int *capacity) {
  if (bjvm_is_bitset_compressed(bits)) {
    for (int i = 1; i < 64; ++i)
      if (bits.bits_inl & (1ULL << i))
        *VECTOR_PUSH(existing_buf, (*length), (*capacity)) = i - 1;
  } else {
    for (uint32_t i = 0; i < bits.ptr.size_words; ++i)
      for (int j = 0; j < 64; ++j)
        if (bits.ptr.bits[i] & (1ULL << j))
          *VECTOR_PUSH(existing_buf, (*length), (*capacity)) = i * 64 + j;
  }
  return existing_buf;
}

bjvm_compressed_bitset bjvm_init_compressed_bitset(int bits_capacity) {
  if (bits_capacity > 63) {
    uint32_t size_words = (bits_capacity + 63) / 64;
    uint64_t *buffer = calloc(size_words, sizeof(uint64_t));
    return (bjvm_compressed_bitset){.ptr.bits = buffer,
                                    .ptr.size_words = size_words};
  }
  return (bjvm_compressed_bitset){
      .bits_inl = 1 // lowest bit = 1
  };
}

bjvm_compressed_bitset
bjvm_copy_compressed_bitset(bjvm_compressed_bitset bits) {
  if (bjvm_is_bitset_compressed(bits)) {
    return bits;
  }
  const size_t buf_size = bits.ptr.size_words * sizeof(uint64_t);
  uint64_t *buffer = malloc(buf_size);
  memcpy(buffer, bits.ptr.bits, buf_size);
  return (bjvm_compressed_bitset){.ptr.bits = buffer,
                                  .ptr.size_words = bits.ptr.size_words};
}

void get_compressed_bitset_word_and_offset(bjvm_compressed_bitset *bits,
                                           size_t bit_index, uint64_t **word,
                                           uint8_t *offset) {
  if (bjvm_is_bitset_compressed(*bits)) {
    assert(bit_index < 63);
    *word = &bits->bits_inl;
    *offset = bit_index + 1;
  } else {
    assert(bit_index < 64 * bits->ptr.size_words);
    *word = &bits->ptr.bits[bit_index >> 6];
    *offset = bit_index & 0x3f;
  }
}

bool bjvm_test_compressed_bitset(const bjvm_compressed_bitset bits,
                                 size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset((bjvm_compressed_bitset *)&bits,
                                        bit_index, &word, &offset);
  return *word & (1ULL << offset);
}

bool bjvm_test_reset_compressed_bitset(bjvm_compressed_bitset *bits,
                                       size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset(bits, bit_index, &word, &offset);
  bool test = *word & (1ULL << offset);
  *word &= ~(1ULL << offset);
  return test;
}

bool bjvm_test_set_compressed_bitset(bjvm_compressed_bitset *bits,
                                     size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset(bits, bit_index, &word, &offset);
  bool test = *word & (1ULL << offset);
  *word |= 1ULL << offset;
  return test;
}

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity) {
  bjvm_string_hash_table table;
  table.free_fn = free_fn;
  table.entries = calloc(initial_capacity, sizeof(bjvm_hash_table_entry));
  table.entries_count = 0;
  table.entries_cap = initial_capacity;
  table.load_factor = load_factor;
  return table;
}

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl) {
  bjvm_hash_table_iterator iter;
  iter.current_base = tbl->entries;
  bjvm_hash_table_entry *end = tbl->entries + tbl->entries_cap;
  // advance to first nonzero entry
  while (iter.current_base < end && iter.current_base->key == nullptr)
    iter.current_base++;
  iter.current = iter.current_base;
  iter.end = end;
  return iter;
}

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       char **key, size_t *key_len,
                                       void **value) {
  if (iter.current != iter.end) {
    *key = iter.current->key;
    *key_len = iter.current->key_len;
    *value = iter.current->data;
    return true;
  }
  return false;
}

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter) {
  if (iter->current_base == iter->end)
    return false;
  if (iter->current->next) {
    iter->current = iter->current->next;
    return true;
  }
  // advance base until a non-null key
  while (++iter->current_base < iter->end && iter->current_base->key == nullptr)
    ;
  iter->current = iter->current_base;
  return iter->current_base != iter->end;
}

uint32_t bjvm_hash_string(const char *key, size_t len) {
  uint64_t hash = 0;
  for (size_t i = 0; i < len; ++i) {
    hash = 31 * hash + key[i]; // yk what I mean ;)
  }
  return hash;
}

bjvm_hash_table_entry *
bjvm_find_hash_table_entry(bjvm_string_hash_table *tbl, const char *key,
                           size_t len, bool *equal, bool *on_chain,
                           bjvm_hash_table_entry **prev_entry) {
  uint32_t hash = bjvm_hash_string(key, len);
  size_t index = hash % tbl->entries_cap;
  bjvm_hash_table_entry *ent = &tbl->entries[index], *prev = nullptr;
  while (ent) {
    *on_chain = prev != nullptr;
    if (ent->key && ent->key_len == len && memcmp(ent->key, key, len) == 0) {
      *equal = true;
      *prev_entry = prev;
      return ent;
    }
    if (!ent->next) {
      *equal = false;
      *prev_entry = prev;
      return ent;
    }
    prev = ent;
    ent = ent->next;
  }
  *equal = false;
  *on_chain = true;
  if (prev_entry)
    *prev_entry = prev;
  return prev;
}

void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl, const char *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? strlen(key) : len;
  bjvm_hash_table_entry *prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
  if (!equal)
    return nullptr;
  tbl->entries_count--;
  void *ret_val = ent->data;
  free(ent->key);
  ent->key = nullptr;
  if (prev) {
    prev->next = ent->next;
    free(ent);
  } else if (ent->next) {
    prev = ent->next;
    *ent = *ent->next;
    free(prev);
  }
  return ret_val;
}

void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, char *key,
                                  int len, void *value, bool copy_key) {
  len = len == -1 ? strlen(key) : len;
  bool equal, on_chain;
  if (tbl->entries_count + 1 >= tbl->load_factor * tbl->entries_cap) {
    bjvm_hash_table_rehash(tbl, tbl->entries_cap * 2);
  }

  bjvm_hash_table_entry *_prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  if (equal) {
    void *ret_val = ent->data;
    ent->data = value;
    if (!copy_key)
      free(key);
    return ret_val;
  }
  if (on_chain || ent->key != nullptr) {
    ent->next = malloc(sizeof(bjvm_hash_table_entry));
    ent = ent->next;
  }
  ent->next = nullptr;
  ent->data = value;
  if (copy_key) {
    char *new_key = malloc(len * sizeof(char));
    memcpy(new_key, key, len);
    ent->key = new_key;
  } else {
    ent->key = key;
  }
  ent->key_len = len;
  tbl->entries_count++;
  return nullptr;
}

void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl, const char *key,
                             int len, void *value) {
  return bjvm_hash_table_insert_impl(tbl, (char *)key /* key copied */, len,
                                     value, true);
}

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity) {
  bjvm_string_hash_table new_table =
      bjvm_make_hash_table(tbl->free_fn, tbl->load_factor, new_capacity);
  bjvm_hash_table_iterator iter = bjvm_hash_table_get_iterator(tbl);
  char *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(iter, &key, &len, &value)) {
    bjvm_hash_table_insert_impl(&new_table, key, len, value,
                                false /* don't copy key */);
    bjvm_hash_table_entry *ent = iter.current;
    bool entry_on_chain = iter.current != iter.current_base;
    bjvm_hash_table_iterator_next(&iter);
    if (entry_on_chain)
      free(ent);
  }
  free(tbl->entries); // Don't need to free the linked lists, keys etc. as they
                      // were moved over
  *tbl = new_table;
}

void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const char *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? strlen(key) : len;
  bjvm_hash_table_entry *_prev,
      *entry =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  return equal ? entry->data : nullptr;
}

void bjvm_free_hash_table(bjvm_string_hash_table tbl) {
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&tbl);
  char *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(it, &key, &len, &value)) {
    bjvm_hash_table_entry *ent = it.current;
    bool needs_free = it.current != it.current_base;
    free(key);
    if (tbl.free_fn)
      tbl.free_fn(value);
    bjvm_hash_table_iterator_next(&it);
    if (needs_free)
      free(ent);
  }
  free(tbl.entries);
  tbl.entries_cap = tbl.entries_count = 0; // good form
}