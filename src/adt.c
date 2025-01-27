//
// Created by alec on 12/18/24.
//

#include "adt.h"
#include "util.h"

#include <stdarg.h>
#include <stdlib.h>

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

static void push_set_bits(uint64_t bits, int offset, int **existing_buf,
                          int *length, int *capacity) {
  while (bits) {
    int shift = __builtin_ctzll(bits);
    bits >>= shift;
    offset += shift;
    *VECTOR_PUSH((*existing_buf), (*length), (*capacity)) = offset;
    bits >>= 1;
    offset += 1;
  }
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
  *length = 0;
  if (bjvm_is_bitset_compressed(bits)) {
    push_set_bits(bits.bits_inl >> 1, 0, &existing_buf, length, capacity);
  } else {
    for (uint32_t i = 0; i < bits.ptr.size_words; ++i)
      push_set_bits(bits.ptr.bits[i], (int)i << 6, &existing_buf, length,
                    capacity);
  }
  return existing_buf;
}

void arena_init(arena *a) { a->begin = nullptr; }

const size_t ARENA_REGION_BYTES = 1 << 12;
void *arena_alloc(arena *a, size_t count, size_t bytes) {
  size_t allocate = (count * bytes + 7) & ~(size_t)7;
  if (allocate > ARENA_REGION_BYTES) {
    arena_region *region = calloc(1, sizeof(arena_region) + allocate);
    region->used = region->capacity = allocate;
    if (a->begin) {
      region->next = a->begin->next;
      a->begin->next = region;
    } else {
      a->begin = region;
    }
    return region->data;
  }
  if (!a->begin || a->begin->used + allocate > a->begin->capacity) {
    arena_region *new = calloc(1, sizeof(arena_region) + ARENA_REGION_BYTES);
    new->capacity = ARENA_REGION_BYTES;
    new->next = a->begin;
    a->begin = new;
    return arena_alloc(a, count, bytes);
  }
  char *result = a->begin->data + a->begin->used;
  a->begin->used += allocate;
  return result;
}

bjvm_utf8 arena_make_str(arena *a, const char *bytes, int len) {
  char *copy = arena_alloc(a, len + 1, sizeof(char));
  memcpy(copy, bytes, len);
  copy[len] = '\0';
  return (bjvm_utf8){.chars = copy, .len = len};
}

void arena_uninit(arena *a) {
  // Walk the list of regions and free them
  arena_region *region = a->begin;
  while (region) {
    arena_region *next = region->next;
    free(region);
    region = next;
  }
  a->begin = nullptr;
}

void bjvm_init_compressed_bitset(bjvm_compressed_bitset *bs,
                                 int bits_capacity) {
  if (bits_capacity > 63) {
    uint32_t size_words = (bits_capacity + 63) / 64;
    uint64_t *buffer = calloc(size_words, sizeof(uint64_t));
    *bs = (bjvm_compressed_bitset){.ptr.bits = buffer,
                                   .ptr.size_words = size_words};
  } else {
    *bs = (bjvm_compressed_bitset){
        .bits_inl = 1 // lowest bit = 1
    };
  }
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

void bjvm_string_builder_init(bjvm_string_builder *builder) {
  builder->data = nullptr;
  builder->write_pos = 0;
}

void bjvm_string_builder_append(bjvm_string_builder *builder, const char *fmt,
                                ...) {
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(nullptr, 0, fmt, args);
  va_end(args);

  bool is_first = !builder->data;
  char *write = arraddnptr(builder->data, len + is_first);
  write -= !is_first;
  va_start(args, fmt);
  vsnprintf(write, len + 1, fmt, args);
  va_end(args);

  builder->write_pos = arrlen(builder->data) - 1;
}

void bjvm_string_builder_free(bjvm_string_builder *builder) {
  arrfree(builder->data);
}

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity) {
  bjvm_string_hash_table table;
  table.free = free_fn;
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

_Pragma( "GCC diagnostic ignored \"-Wattributes\"" )
__attribute__((no_sanitize("unsigned-shift-base"), no_sanitize("unsigned-integer-overflow")))
_Pragma( "GCC diagnostic pop" )
static uint32_t fxhash_string(const char *key, size_t len) {
  const uint64_t FXHASH_CONST = 0x517cc1b727220a95ULL;
  uint64_t hash = 0;
  for (size_t i = 0; i + 7 < len; i += 8) {
    uint64_t word;
    memcpy(&word, key + i, 8);
    hash = ((hash << 5 | hash >> 59) ^ word) * FXHASH_CONST;
  }
  if (len & 7) {
    uint64_t word = 0;
    memcpy(&word, key + len - (len & 7), len & 7);
    hash = ((hash << 5 | hash >> 59) ^ word) * FXHASH_CONST;
  }

  return (uint32_t) hash;
}

bjvm_hash_table_entry *
bjvm_find_hash_table_entry(bjvm_string_hash_table *tbl, const char *key,
                           size_t len, bool *equal, bool *on_chain,
                           bjvm_hash_table_entry **prev_entry) {
  if (unlikely(tbl->entries_cap == 0))
    return nullptr;
  uint32_t hash = fxhash_string(key, len);
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
  len = len == -1 ? (int)strlen(key) : len;
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

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity) {
  bjvm_string_hash_table new_table =
      bjvm_make_hash_table(tbl->free, tbl->load_factor, new_capacity);
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

void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, char *key,
                                  int len_, void *value, bool copy_key) {
  assert(len_ >= -1);
  size_t len = len_ == -1 ? (int)strlen(key) : len_;

  bool equal, on_chain;
  if ((double)tbl->entries_count + 1 >=
      tbl->load_factor * (double)tbl->entries_cap) {
    bjvm_hash_table_rehash(tbl, tbl->entries_cap * 2);
  }

  [[maybe_unused]] bjvm_hash_table_entry *prev;
  bjvm_hash_table_entry *ent =
      bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
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

void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const char *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? (int)strlen(key) : len;
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
    if (tbl.free)
      tbl.free(value);
    bjvm_hash_table_iterator_next(&it);
    if (needs_free)
      free(ent);
  }
  free(tbl.entries);
  tbl.entries_cap = tbl.entries_count = 0; // good form
}

void bjvm_hash_table_reserve(bjvm_string_hash_table *tbl, size_t new_capacity) {
  // Make large enough so the load factor is less than the desired load factor
  const size_t new = (size_t)((double)new_capacity * tbl->load_factor) + 2;
  bjvm_hash_table_rehash(tbl, new);
}
