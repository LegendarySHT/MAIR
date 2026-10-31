// Stand-in for oss-fuzz's projects/libpng/libpng_read_fuzzer.cc.
//
// This libpng checkout no longer ships contrib/oss-fuzz/libpng_read_fuzzer.cc
// and the oss-fuzz copy could not be downloaded here, so the same entry point
// is provided locally: feed the whole input to libpng's simplified read API,
// which is what the upstream harness exercises.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "png.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8 || size > (1u << 20)) return 0;
  png_image image;
  std::memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, data, size)) return 0;
  image.format = PNG_FORMAT_RGBA;
  size_t buffer_size = PNG_IMAGE_SIZE(image);
  if (buffer_size > (1u << 24)) {
    png_image_free(&image);
    return 0;
  }
  png_bytep buffer = static_cast<png_bytep>(std::malloc(buffer_size));
  if (buffer != nullptr) {
    png_image_finish_read(&image, nullptr, buffer, 0, nullptr);
    std::free(buffer);
  }
  png_image_free(&image);
  return 0;
}
