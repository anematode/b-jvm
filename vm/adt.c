//
// Created by alec on 12/18/24.
//

#include "adt.h"
#include "../vendor/stb_ds.h"
#include "util.h"

#include <analysis.h>
#include <stdarg.h>
#include <stdlib.h>

void arena_init(arena *a) { a->begin = nullptr; }

constexpr size_t ARENA_REGION_BYTES = 1 << 12;
// NOLINTNEXTLINE(misc-no-recursion)
void *arena_alloc(arena *a, size_t count, size_t bytes) {
  size_t allocate = align_up(count * bytes, _Alignof(arena_region));
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

slice arena_make_str(arena *a, const char *bytes, int len) {
  char *copy = arena_alloc(a, len + 1, sizeof(char));
  memcpy(copy, bytes, len);
  copy[len] = '\0';
  return (slice){.chars = copy, .len = len};
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

void string_builder_init(string_builder *builder) {
  builder->data = nullptr;
  builder->write_pos = 0;
}

void string_builder_append(string_builder *builder, const char *fmt, ...) {
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

void string_builder_free(string_builder *builder) { arrfree(builder->data); }

string_hash_table make_hash_table(void (*free_fn)(void *), double load_factor, size_t initial_capacity) {
  string_hash_table table;
  table.free = free_fn;
  table.entries = calloc(initial_capacity, sizeof(hash_table_entry));
  table.entries_count = 0;
  table.entries_cap = initial_capacity;
  table.load_factor = load_factor;
  return table;
}

hash_table_iterator hash_table_get_iterator(const string_hash_table *tbl) {
  hash_table_iterator iter;
  iter.current_base = tbl->entries;
  hash_table_entry *end = tbl->entries + tbl->entries_cap;
  // advance to first nonzero entry
  while (iter.current_base < end && iter.current_base->key == nullptr)
    iter.current_base++;
  iter.current = iter.current_base;
  iter.end = end;
  return iter;
}

bool hash_table_iterator_has_next(hash_table_iterator iter, char **key, size_t *key_len, void **value) {
  if (iter.current != iter.end) {
    if (key)
      *key = iter.current->key;
    if (key_len)
      *key_len = iter.current->key_len;
    if (value)
      *value = iter.current->data;
    return true;
  }
  return false;
}

bool hash_table_iterator_next(hash_table_iterator *iter) {
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

static u32 fxhash_string(const char *key, size_t len) {
  constexpr u64 FXHASH_CONST = 0x517cc1b727220a95ULL;
  u64 hash = 0;
  for (size_t i = 0; i + 7 < len; i += 8) {
    u64 word;
    memcpy(&word, key + i, 8);
    hash = ((hash << 5 | hash >> 59) ^ word) * FXHASH_CONST;
  }
  if (len & 7) {
    u64 word = 0;
    memcpy(&word, key + len - (len & 7), len & 7);
    hash = ((hash << 5 | hash >> 59) ^ word) * FXHASH_CONST;
  }

  return (u32)hash;
}

hash_table_entry *find_hash_table_entry(const string_hash_table *tbl, const char *key, size_t len, bool *equal,
                                        bool *on_chain, hash_table_entry **prev_entry) {
  if (unlikely(tbl->entries_cap == 0))
    return nullptr;
  u32 hash = fxhash_string(key, len);
  size_t index = hash % tbl->entries_cap;
  hash_table_entry *ent = &tbl->entries[index], *prev = nullptr;
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

void *hash_table_delete(string_hash_table *tbl, const char *key, int len) {
  bool equal, on_chain;
  len = len == -1 ? (int)strlen(key) : len;
  hash_table_entry *prev, *ent = find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
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

void *hash_table_insert_impl(string_hash_table *tbl, char *key, int len, void *value, bool copy_key);

// NOLINTNEXTLINE(misc-no-recursion)
void hash_table_rehash(string_hash_table *tbl, size_t new_capacity) {
  string_hash_table new_table = make_hash_table(tbl->free, tbl->load_factor, new_capacity);
  hash_table_iterator iter = hash_table_get_iterator(tbl);
  char *key;
  size_t len;
  void *value;
  while (hash_table_iterator_has_next(iter, &key, &len, &value)) {
    hash_table_insert_impl(&new_table, key, (int)len, value, false /* don't copy key */);
    hash_table_entry *ent = iter.current;
    bool entry_on_chain = iter.current != iter.current_base;
    hash_table_iterator_next(&iter);
    if (entry_on_chain)
      free(ent);
  }
  free(tbl->entries); // Don't need to free the linked lists, keys etc. as they
  // were moved over
  *tbl = new_table;
}

// NOLINTNEXTLINE(misc-no-recursion)
void *hash_table_insert_impl(string_hash_table *tbl, char *key, int len_, void *value, bool copy_key) {
  DCHECK(len_ >= -1);
  size_t len = len_ == -1 ? (int)strlen(key) : len_;

  bool equal, on_chain;
  if ((double)tbl->entries_count + 1 >= tbl->load_factor * (double)tbl->entries_cap) {
    hash_table_rehash(tbl, tbl->entries_cap * 2);
  }

  [[maybe_unused]] hash_table_entry *prev;
  hash_table_entry *ent = find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
  if (equal) {
    void *ret_val = ent->data;
    ent->data = value;
    if (!copy_key)
      free(key);
    return ret_val;
  }
  if (on_chain || ent->key != nullptr) {
    ent->next = malloc(sizeof(hash_table_entry));
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

void *hash_table_insert(string_hash_table *tbl, const char *key, int len, void *value) {
  return hash_table_insert_impl(tbl, (char *)key /* key copied */, len, value, true);
}

void *hash_table_lookup(const string_hash_table *tbl, const char *key, int len) {
  bool equal, on_chain;
  len = len == -1 ? (int)strlen(key) : len;
  hash_table_entry *_prev, *entry = find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  return equal ? entry->data : nullptr;
}

bool hash_table_contains(const string_hash_table *tbl, const char *key, int len) {
  bool equal, on_chain;
  len = len == -1 ? (int)strlen(key) : len;
  hash_table_entry *_prev;
  find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  return equal;
}

void free_hash_table(string_hash_table tbl) {
  hash_table_iterator it = hash_table_get_iterator(&tbl);
  char *key;
  size_t len;
  void *value;
  while (hash_table_iterator_has_next(it, &key, &len, &value)) {
    hash_table_entry *ent = it.current;
    bool needs_free = it.current != it.current_base;
    free(key);
    if (tbl.free)
      tbl.free(value);
    hash_table_iterator_next(&it);
    if (needs_free)
      free(ent);
  }
  free(tbl.entries);
  tbl.entries_cap = tbl.entries_count = 0; // good form
}

void hash_table_reserve(string_hash_table *tbl, size_t new_capacity) {
  // Make large enough so the load factor is less than the desired load factor
  const size_t new = (size_t)((double)new_capacity * tbl->load_factor) + 2;
  hash_table_rehash(tbl, new);
}
