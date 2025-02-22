#include <natives-dsl.h>
#include <zlib.h>

DECLARE_NATIVE("java/util/zip", Inflater, initIDs, "()V") {
  // Unused (HS probably uses this for setup).
  return value_null();
}

#define ZS thread->vm->z_streams
DECLARE_NATIVE("java/util/zip", Inflater, init, "(Z)J") {
  // Create an inflater.
  assert(argc == 1);
  bool nowrap = args[0].i;
  z_stream *stream = calloc(1, sizeof(z_stream));
  stream->zalloc = Z_NULL;
  stream->zfree = Z_NULL;
  stream->opaque = Z_NULL;
  int ret = inflateInit2(stream, nowrap ? -MAX_WBITS : MAX_WBITS);
  // Add to vm->zstreams
  arrput(ZS, stream);
  if (ret != Z_OK) {
    free(stream);
    return (stack_value){.l = 0};
  }
  return (stack_value){.l = (s64)stream};
}

/**
 * For reference, the JDK code reads:
 * int read = (int) (result & 0x7fff_ffffL);
 * int written = (int) (result >>> 31 & 0x7fff_ffffL);
 * if ((result >>> 62 & 1) != 0) {
 *   finished = true;
 * }
 */

// todo: What is the function of needDict?
static s64 make_return_value(int read, int written, bool finished, bool needDict) {
  return (s64)read | (s64)written << 31 | (s64)finished << 62 | (s64)needDict << 63;
}

// long addr, byte[] inputArray, int inputOff, int inputLen, byte[] outputArray, int outputOff, int outputLen
DECLARE_NATIVE("java/util/zip", Inflater, inflateBytesBytes, "(J[BII[BII)J") {
  assert(argc == 7);
  z_stream *stream = (z_stream *)args[0].l;
  u8 *src = (u8 *)ArrayData(args[1].handle->obj) + args[2].i;
  u32 src_len = args[3].i;
  u8 *dst = (u8 *)ArrayData(args[4].handle->obj) + args[5].i;
  u32 dst_len = args[6].i;
  stream->next_in = src;
  stream->avail_in = src_len;
  stream->next_out = dst;
  stream->avail_out = dst_len;
  int ret = inflate(stream, Z_NO_FLUSH);
  int read = (s32)(src_len - stream->avail_in);
  int written = (s32)(dst_len - stream->avail_out);
  if (ret != Z_OK && ret != Z_STREAM_END) { // Error case (todo: test this)
    return (stack_value){.l = make_return_value(read, written, true, false)};
  }
  return (stack_value){.l = make_return_value(read, written, ret == Z_STREAM_END, false)};
}

DECLARE_NATIVE("java/util/zip", Inflater, reset, "(J)V") {
  z_stream *stream = (z_stream *)args[0].l;
  inflateReset(stream);
  return value_null();
}

DECLARE_NATIVE("java/util/zip", Inflater, end, "(J)V") {
  z_stream *stream = (z_stream *)args[0].l;
  inflateEnd(stream);
  // Remove from vm->zstreams
  for (int i = 0; i < arrlen(ZS); ++i) {
    if (ZS[i] == stream) {
      arrdelswap(ZS, i);
      break;
    }
  }
#undef ZS
  free(stream);
  return value_null();
}

// int crc, byte[] b, int off, int len
DECLARE_NATIVE("java/util/zip", CRC32, updateBytes0, "(I[BII)I") {
  assert(argc == 4);
  u32 crc = args[0].i;
  u8 *data = (u8 *)ArrayData(args[1].handle->obj) + args[2].i;
  int len = args[3].i;
  crc = crc32(crc, data, len);
  return (stack_value){.i = (s32)crc};
}