
#ifndef THIRD_PARTY_ZLIB_COMPRESSION_UTILS_H_
#define THIRD_PARTY_ZLIB_COMPRESSION_UTILS_H_

#include <string>
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
// Compresses the data in |input| using gzip, storing the result in |output|.
// Returns true for success.
bool GzipCompress(Vector<const byte> input, std::string* output);

// Uncompresses the data in |input| using gzip, storing the result in |output|.
// |input| and |output| are allowed to be the same string (in-place operation).
// Returns true for success.
bool GzipUncompress(const std::string& input, std::string* output);

// Returns the uncompressed size from GZIP-compressed |compressed_data|.
uint32_t GetUncompressedSize(const std::string& compressed_data);

}  // namespace internal
}  // namespace v8

#endif  // THIRD_PARTY_ZLIB_COMPRESSION_UTILS_H