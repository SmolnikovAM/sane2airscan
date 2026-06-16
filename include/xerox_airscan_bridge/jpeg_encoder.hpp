#pragma once

#include <cstdint>
#include <vector>

namespace xab {

enum class PixelFormat {
  gray8,
  rgb8,
};

std::vector<std::uint8_t> encode_jpeg(const std::vector<std::uint8_t> &pixels,
                                      int width, int height,
                                      PixelFormat format, int quality);

} // namespace xab
