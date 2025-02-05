#include "objects.h"

#include <cached_classdescs.h>

#include <arrays.h>
#include <bjvm.h>

// https://github.com/openjdk/jdk11u-dev/blob/be6956b15653f0d870efae89fc1b5df657cca45f/src/java.base/share/classes/java/lang/StringLatin1.java#L52
static bool do_latin1(const u16 *chars, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (chars[i] >> 8 != 0)
      return false;
  }
  return true;
}

static int convert_modified_utf8_to_chars(const char *bytes, int len, u16 **result,
                                   int *result_len, bool sloppy) {
  *result = malloc(len * sizeof(short)); // conservatively large
  int i = 0, j = 0;

  u16 idxs[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  for (int i = 0; i < 16; ++i)
    idxs[i] += (u16)-256;

  for (; i < len; ++i) {
    // "Code points in the range '\u0001' to '\u007F' are represented by a
    // single byte"
    u8 byte = bytes[i];
    if (byte >= 0x01 && byte <= 0x7F) {
      (*result)[j++] = byte;
    } else if ((bytes[i] & 0xE0) == 0xC0) {
      // "Code points in the range '\u0080' to '\u07FF' are represented by two
      // bytes"
      if (i >= len - 1)
        goto inval;
      (*result)[j++] = (((u16)byte & 0x1F) << 6) | ((u16)bytes[i + 1] & 0x3F);
      i++;
    } else if ((bytes[i] & 0xF0) == 0xE0) {
      // "Code points in the range '\u0800' to '\uFFFF' are represented by three
      // bytes"
      if (i >= len - 2)
        goto inval;
      (*result)[j++] = (short)((byte & 0x0F) << 12) |
                       ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
      i += 2;
    } else if (sloppy && byte == 0) {
      break;
    } else {
      // "No byte may have the value (byte)0 or lie in the range (byte)0xf0 -
      // (byte)0xff."
      goto inval;
    }
  }
  *result_len = j;
  return 0;

  inval:
    free(*result);
  return -1; // invalid UTF-8 sequence
}

// TODO restore implementation calling <init> when we can figure it out
bjvm_obj_header *make_jstring_modified_utf8(bjvm_thread *thread, slice string) {
  bjvm_handle *str = bjvm_make_handle(thread, new_object(thread, thread->vm->cached_classdescs->string));

#define S ((struct bjvm_native_String *)str->obj)

  bjvm_obj_header *result = nullptr;

  u16 *chars = nullptr;
  int len;
  if (convert_modified_utf8_to_chars(string.chars, string.len, &chars, &len, true) == -1)
    return nullptr;

  if (do_latin1(chars, len)) {
    S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, len);
    if (!S->value)
      goto oom;
    for (int i = 0; i < len; ++i) {
      ByteArrayStore(S->value, i, (s8)chars[i]);
    }
    S->coder = STRING_CODER_LATIN1; // LATIN1
    result = (void *)S;
  } else {
    S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, 2 * len);
    if (!S->value)
      goto oom;
    memcpy(ArrayData(S->value), chars, len * sizeof(short));
    S->coder = STRING_CODER_UTF16; // UTF-16
    result = (void *)S;
  }

oom:
  free(chars);
  bjvm_drop_handle(thread, str);
  return result;
}

object make_jstring_cstr(bjvm_thread *thread, char const* cstr) {
  bjvm_handle *str = bjvm_make_handle(thread, new_object(thread, thread->vm->cached_classdescs->string));

#define S ((struct bjvm_native_String *)str->obj)

  bjvm_obj_header *result = nullptr;

  size_t len_ = strlen(cstr);
  DCHECK(len_ < INT32_MAX);
  s32 len = (s32)len_;

  S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, len);
  if (!S->value)
    goto oom;
  ByteArrayStoreBlock(S->value, 0, len, (u8*)cstr);
  S->coder = STRING_CODER_LATIN1; // LATIN1
  result = (void *)S;

  oom:
  bjvm_drop_handle(thread, str);
  return result;
}

static object lookup_interned_jstring(bjvm_thread *thread, object s) {
  object raw = RawStringData(thread, s);

  u8 *data = ArrayData(raw);
  s32 len = *ArrayLength(raw);

  return bjvm_hash_table_lookup(&thread->vm->interned_strings, (char const*)data, len);
}

static void insert_interned_jstring(bjvm_thread *thread, object s) {
  object raw = RawStringData(thread, s);

  u8 *data = ArrayData(raw);
  s32 len = *ArrayLength(raw);

  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, (char const*) data, len, s);
}

object MakeJStringFromModifiedUTF8(bjvm_thread *thread, slice data, bool intern) {
  object obj = make_jstring_modified_utf8(thread, data);
  if (!obj)
    return nullptr;

  object interned = lookup_interned_jstring(thread, obj);
  if (interned)
    return interned;

  if (intern) {
    insert_interned_jstring(thread, obj);
  }

  return obj;
}
bjvm_obj_header *MakeJStringFromCString(bjvm_thread *thread, char const* data, bool intern) {
  object obj = make_jstring_cstr(thread, data);
  if (!obj)
    return nullptr;

  object interned = lookup_interned_jstring(thread, obj);
  if (interned)
    return interned;

  if (intern) {
    insert_interned_jstring(thread, obj);
  }

  return obj;
}

bjvm_obj_header *MakeJStringFromData(bjvm_thread *thread, slice data, string_coder_kind encoding) {
  bjvm_handle *str = bjvm_make_handle(thread, new_object(thread, thread->vm->cached_classdescs->string));

#define S ((struct bjvm_native_String *)str->obj)

  bjvm_obj_header *result = nullptr;

  DCHECK(data.len < INT32_MAX);
  s32 len = (s32)data.len;

  S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, len);
  if (!S->value)
    goto oom;
  ByteArrayStoreBlock(S->value, 0, len, (u8 const*)data.chars);
  S->coder = encoding; // LATIN1
  result = (void *)S;

  oom:
  bjvm_drop_handle(thread, str);
  return result;
}

bjvm_obj_header *InternJString(bjvm_thread *thread, object s) {
  object lookup_result = lookup_interned_jstring(thread, s);
  if (lookup_result)
    return lookup_result;

  object raw = RawStringData(thread, s);

  u8 *data = ArrayData(raw);
  s32 len = *ArrayLength(raw);

  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, (char const*)data, len, s);
  return s;
}

u64 hash_code_rng = 0;

u64 ObjNextHashCode() {
  hash_code_rng = hash_code_rng * 0x5DEECE66D + 0xB;
  return hash_code_rng >> 32;
}

bjvm_obj_header *MakeJavaString(bjvm_thread *thread, slice slice) { return make_jstring_modified_utf8(thread, slice); }