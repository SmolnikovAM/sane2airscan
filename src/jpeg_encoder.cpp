#include "xerox_airscan_bridge/jpeg_encoder.hpp"

#include <csetjmp>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#include <jpeglib.h>

namespace xab {

namespace {

struct JpegErrorManager {
  jpeg_error_mgr base;
  jmp_buf jump_target;
  char message[JMSG_LENGTH_MAX]{};
};

void jpeg_error_exit(j_common_ptr cinfo) {
  auto *error = reinterpret_cast<JpegErrorManager *>(cinfo->err);
  (*cinfo->err->format_message)(cinfo, error->message);
  longjmp(error->jump_target, 1);
}

} // namespace

std::vector<std::uint8_t> encode_jpeg(const std::vector<std::uint8_t> &pixels,
                                      int width, int height,
                                      PixelFormat format, int quality) {
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("cannot encode JPEG with invalid dimensions");
  }

  const int components = format == PixelFormat::gray8 ? 1 : 3;
  const std::size_t expected =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
      static_cast<std::size_t>(components);
  if (pixels.size() < expected) {
    throw std::runtime_error("not enough pixel data for JPEG encoding");
  }

  jpeg_compress_struct cinfo{};
  JpegErrorManager jerr{};
  cinfo.err = jpeg_std_error(&jerr.base);
  jerr.base.error_exit = jpeg_error_exit;
  volatile bool compressor_created = false;

  // Declared before setjmp so the error path can release the mem-dest buffer
  // (jpeg_mem_dest allocates it immediately). Its address escapes to
  // jpeg_mem_dest, so the compiler keeps it in memory across the call.
  unsigned char *out = nullptr;
  unsigned long out_size = 0;

  if (setjmp(jerr.jump_target) != 0) {
    const std::string message = jerr.message[0] == '\0' ? "JPEG encoding failed"
                                                        : jerr.message;
    if (compressor_created) {
      jpeg_destroy_compress(&cinfo);
    }
    std::free(out);
    throw std::runtime_error(message);
  }

  jpeg_create_compress(&cinfo);
  compressor_created = true;

  jpeg_mem_dest(&cinfo, &out, &out_size);

  cinfo.image_width = static_cast<JDIMENSION>(width);
  cinfo.image_height = static_cast<JDIMENSION>(height);
  cinfo.input_components = components;
  cinfo.in_color_space = format == PixelFormat::gray8 ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  const int row_stride = width * components;
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW row_pointer[1];
    row_pointer[0] =
        const_cast<JSAMPROW>(&pixels[cinfo.next_scanline * row_stride]);
    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);
  std::vector<std::uint8_t> result(out, out + out_size);
  jpeg_destroy_compress(&cinfo);
  std::free(out);
  return result;
}

} // namespace xab
