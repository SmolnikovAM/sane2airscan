#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace xab {

enum class ColorMode {
  grayscale8,
  color24,
};

struct ScanSettings {
  int resolution = 300;
  ColorMode color_mode = ColorMode::grayscale8;
  double x_offset_mm = 0.0;
  double y_offset_mm = 0.0;
  double width_mm = 210.0;
  double height_mm = 297.0;
  std::string input_source = "Platen";
  std::string document_format = "image/jpeg";
};

struct ScanImage {
  std::string content_type = "image/jpeg";
  std::vector<std::uint8_t> bytes;
};

class Scanner {
public:
  virtual ~Scanner() = default;
  virtual ScanImage scan(const ScanSettings &settings,
                         const std::atomic_bool &cancel_requested) = 0;
};

} // namespace xab
