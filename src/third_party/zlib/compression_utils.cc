#include "src/third_party/zlib/compression_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "third_party/zlib/zlib.h"

namespace {
// This code is taken almost verbatim from
// third_party/zlib/google/compression_utils.cc. Some lines were changed to be
// compatible with V8's repository.

// The difference in bytes between a zlib header and a gzip header.
const size_t kGzipZlibHeaderDifferenceBytes = 16;

// Pass an integer greater than the following get a gzip header instead of a
// zlib header when calling deflateInit2() and inflateInit2().
const int kWindowBitsToGetGzipHeader = 16;

// This describes the amount of memory zlib uses to compress data. It can go
// from 1 to 9, with 8 being the default. For details, see:
// http://www.zlib.net/manual.html (search for memLevel).
const int kZlibMemoryLevel = 8;

// This code is taken almost verbatim from third_party/zlib/compress.c. The only
// difference is deflateInit2() is called which sets the window bits to be > 16.
// That causes a gzip header to be emitted rather than a zlib header.
int GzipCompressHelper(Bytef* dest, uLongf* dest_length, const Bytef* source,
                       uLong source_length, void* (*malloc_fn)(size_t),
                       void (*free_fn)(void*)) {
  z_stream stream;

  stream.next_in = bit_cast<Bytef*>(source);
  stream.avail_in = static_cast<uInt>(source_length);
  stream.next_out = dest;
  stream.avail_out = static_cast<uInt>(*dest_length);
  if (static_cast<uLong>(stream.avail_out) != *dest_length) return Z_BUF_ERROR;

  // Cannot convert capturing lambdas to function pointers directly, hence the
  // structure.
  struct MallocFreeFunctions {
    void* (*malloc_fn)(size_t);
    void (*free_fn)(void*);
  } malloc_free = {malloc_fn, free_fn};

  if (malloc_fn) {
    DCHECK(free_fn);
    auto zalloc = [](void* opaque, uInt items, uInt size) {
      return reinterpret_cast<MallocFreeFunctions*>(opaque)->malloc_fn(items *
                                                                       size);
    };
    auto zfree = [](void* opaque, void* address) {
      return reinterpret_cast<MallocFreeFunctions*>(opaque)->free_fn(address);
    };

    stream.zalloc = static_cast<alloc_func>(zalloc);
    stream.zfree = static_cast<free_func>(zfree);
    stream.opaque = static_cast<voidpf>(&malloc_free);
  } else {
    stream.zalloc = static_cast<alloc_func>(0);
    stream.zfree = static_cast<free_func>(0);
    stream.opaque = static_cast<voidpf>(0);
  }

  gz_header gzip_header;
  memset(&gzip_header, 0, sizeof(gzip_header));
  int err = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         MAX_WBITS + kWindowBitsToGetGzipHeader,
                         kZlibMemoryLevel, Z_DEFAULT_STRATEGY);
  if (err != Z_OK) return err;

  err = deflateSetHeader(&stream, &gzip_header);
  if (err != Z_OK) return err;

  err = deflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    deflateEnd(&stream);
    return err == Z_OK ? Z_BUF_ERROR : err;
  }
  *dest_length = stream.total_out;

  err = deflateEnd(&stream);
  return err;
}

// This code is taken almost verbatim from third_party/zlib/uncompr.c. The only
// difference is inflateInit2() is called which sets the window bits to be > 16.
// That causes a gzip header to be parsed rather than a zlib header.
int GzipUncompressHelper(Bytef* dest, uLongf* dest_length, const Bytef* source,
                         uLong source_length) {
  z_stream stream;

  stream.next_in = bit_cast<Bytef*>(source);
  stream.avail_in = static_cast<uInt>(source_length);
  if (static_cast<uLong>(stream.avail_in) != source_length) return Z_BUF_ERROR;

  stream.next_out = dest;
  stream.avail_out = static_cast<uInt>(*dest_length);
  if (static_cast<uLong>(stream.avail_out) != *dest_length) return Z_BUF_ERROR;

  stream.zalloc = static_cast<alloc_func>(0);
  stream.zfree = static_cast<free_func>(0);

  int err = inflateInit2(&stream, MAX_WBITS + kWindowBitsToGetGzipHeader);
  if (err != Z_OK) return err;

  err = inflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    inflateEnd(&stream);
    if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
      return Z_DATA_ERROR;
    return err;
  }
  *dest_length = stream.total_out;

  err = inflateEnd(&stream);
  return err;
}
}  // namespace

namespace v8 {
namespace internal {
bool GzipCompress(Vector<const byte> input, std::string* output) {
  // Not using std::vector<> because allocation failures are recoverable,
  // which is hidden by std::vector<>.
  static_assert(sizeof(Bytef) == 1, "");
  const uLongf input_size = static_cast<uLongf>(input.size());

  uLongf compressed_data_size =
      kGzipZlibHeaderDifferenceBytes + compressBound(input_size);
  Bytef* compressed_data =
      reinterpret_cast<Bytef*>(malloc(compressed_data_size));

  if (GzipCompressHelper(compressed_data, &compressed_data_size,
                         bit_cast<const Bytef*>(input.begin()), input_size,
                         nullptr, nullptr) != Z_OK) {
    free(compressed_data);
    return false;
  }

  Bytef* resized_data =
      reinterpret_cast<Bytef*>(realloc(compressed_data, compressed_data_size));
  if (!resized_data) {
    free(compressed_data);
    return false;
  }
  output->assign(resized_data, resized_data + compressed_data_size);
  DCHECK_EQ(input_size, GetUncompressedSize(*output));

  free(resized_data);
  return true;
}

Vector<byte> GzipUncompress(const std::string& input) {
  uLongf uncompressed_size = static_cast<uLongf>(GetUncompressedSize(input));
  Vector<byte> uncompressed_output = Vector<byte>::New(uncompressed_size);

  if (GzipUncompressHelper(bit_cast<Bytef*>(uncompressed_output.begin()),
                           &uncompressed_size,
                           bit_cast<const Bytef*>(input.data()),
                           static_cast<uLongf>(input.length())) == Z_OK) {
    return uncompressed_output;
  }
  return Vector<byte>(nullptr, 0);
}

uint32_t GetUncompressedSize(const std::string& compressed_data) {
  // The uncompressed size is stored in the last 4 bytes of |input| in LE.
  uint32_t size;
  if (compressed_data.length() < sizeof(size)) return 0;
  memcpy(&size,
         &compressed_data.data()[compressed_data.length() - sizeof(size)],
         sizeof(size));
#if defined(V8_TARGET_LITTLE_ENDIAN)
  return size;
#else
  return ByteReverse(size);
#endif
}

}  // namespace internal
}  // namespace v8