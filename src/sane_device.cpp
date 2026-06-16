#include "xerox_airscan_bridge/sane_device.hpp"

#include "xerox_airscan_bridge/jpeg_encoder.hpp"
#include "xerox_airscan_bridge/log.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace xab {

namespace {

std::string sane_status_text(SANE_Status status) {
  return sane_strstatus(status) == nullptr ? "unknown SANE error"
                                           : sane_strstatus(status);
}

std::string option_name(const SANE_Option_Descriptor *descriptor) {
  return descriptor != nullptr && descriptor->name != nullptr ? descriptor->name
                                                              : "";
}

std::string option_title(const SANE_Option_Descriptor *descriptor) {
  return descriptor != nullptr && descriptor->title != nullptr ? descriptor->title
                                                               : "";
}

SANE_Word mm_to_sane_fixed(double mm) {
  return SANE_FIX(mm);
}

std::vector<std::uint8_t>
compact_rows(const std::vector<std::uint8_t> &raw, int width, int height,
             int components, int bytes_per_line) {
  const int wanted_stride = width * components;
  if (wanted_stride <= 0 || bytes_per_line < wanted_stride) {
    throw std::runtime_error("invalid SANE line stride");
  }
  if (wanted_stride == bytes_per_line) {
    return raw;
  }

  std::vector<std::uint8_t> compact;
  compact.resize(static_cast<std::size_t>(wanted_stride) *
                 static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const auto source_offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(bytes_per_line);
    const auto target_offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(wanted_stride);
    if (source_offset + static_cast<std::size_t>(wanted_stride) > raw.size()) {
      throw std::runtime_error("incomplete SANE image data");
    }
    std::copy_n(raw.data() + source_offset, wanted_stride,
                compact.data() + target_offset);
  }
  return compact;
}

} // namespace

SaneDevice::SaneDevice(std::string configured_device)
    : configured_device_(std::move(configured_device)) {
  SANE_Int version = 0;
  const SANE_Status status = sane_init(&version, nullptr);
  if (status != SANE_STATUS_GOOD) {
    throw std::runtime_error("sane_init failed: " + sane_status_text(status));
  }
  std::ostringstream message;
  message << "sane initialized version=" << version;
  log(LogLevel::info, message.str());
}

SaneDevice::~SaneDevice() {
  close();
  sane_exit();
}

std::vector<std::string> SaneDevice::list_devices() const {
  const SANE_Device **devices = nullptr;
  const SANE_Status status = sane_get_devices(&devices, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    throw std::runtime_error("sane_get_devices failed: " +
                             sane_status_text(status));
  }

  std::vector<std::string> result;
  for (int i = 0; devices != nullptr && devices[i] != nullptr; ++i) {
    result.emplace_back(devices[i]->name);
  }
  return result;
}

std::string SaneDevice::resolve_device_name() const {
  if (!configured_device_.empty()) {
    return configured_device_;
  }

  const SANE_Device **devices = nullptr;
  const SANE_Status status = sane_get_devices(&devices, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    throw std::runtime_error("sane_get_devices failed: " +
                             sane_status_text(status));
  }

  for (int i = 0; devices != nullptr && devices[i] != nullptr; ++i) {
    const std::string name(devices[i]->name);
    const bool is_local = name.rfind("net:", 0) != 0;
    const bool is_xerox = name.find("xerox_mfp") != std::string::npos;
    if (is_local && is_xerox) {
      return name;
    }
  }
  for (int i = 0; devices != nullptr && devices[i] != nullptr; ++i) {
    const std::string name(devices[i]->name);
    if (name.find("xerox_mfp") != std::string::npos) {
      return name;
    }
  }
  for (int i = 0; devices != nullptr && devices[i] != nullptr; ++i) {
    const std::string name(devices[i]->name);
    if (name.rfind("net:", 0) != 0) {
      return name;
    }
  }
  if (devices != nullptr && devices[0] != nullptr) {
    return devices[0]->name;
  }
  throw std::runtime_error("no SANE scanner devices found");
}

void SaneDevice::open() {
  if (handle_ != nullptr) {
    return;
  }

  const std::string device_name = resolve_device_name();
  SANE_Status status = sane_open(device_name.c_str(), &handle_);
  if (status != SANE_STATUS_GOOD) {
    throw std::runtime_error("sane_open failed for " + device_name + ": " +
                             sane_status_text(status));
  }
  log(LogLevel::info, "opened SANE device name=" + device_name);
}

void SaneDevice::close() {
  if (handle_ != nullptr) {
    sane_close(handle_);
    handle_ = nullptr;
  }
}

std::vector<SaneOption> SaneDevice::list_options() {
  open();
  std::vector<SaneOption> result;
  for (int index = 1;; ++index) {
    const SANE_Option_Descriptor *descriptor =
        sane_get_option_descriptor(handle_, index);
    if (descriptor == nullptr) {
      break;
    }
    result.push_back(SaneOption{index, option_name(descriptor),
                                option_title(descriptor), descriptor->type,
                                descriptor->unit});
  }
  return result;
}

std::optional<int> SaneDevice::find_option(const std::string &name) const {
  if (handle_ == nullptr) {
    return std::nullopt;
  }
  for (int index = 1;; ++index) {
    const SANE_Option_Descriptor *descriptor =
        sane_get_option_descriptor(handle_, index);
    if (descriptor == nullptr) {
      break;
    }
    if (option_name(descriptor) == name) {
      return index;
    }
  }
  return std::nullopt;
}

void SaneDevice::set_int_option(const std::string &name, int value) {
  const auto index = find_option(name);
  if (!index) {
    return;
  }
  const SANE_Option_Descriptor *descriptor =
      sane_get_option_descriptor(handle_, *index);
  if (descriptor == nullptr) {
    return;
  }
  SANE_Int info = 0;
  SANE_Word word = descriptor->type == SANE_TYPE_FIXED ? SANE_FIX(value) : value;
  const SANE_Status status =
      sane_control_option(handle_, *index, SANE_ACTION_SET_VALUE, &word, &info);
  if (status != SANE_STATUS_GOOD) {
    log(LogLevel::warn, "failed to set SANE option " + name + ": " +
                            sane_status_text(status));
  }
}

void SaneDevice::set_string_option(const std::string &name,
                                   const std::string &value) {
  const auto index = find_option(name);
  if (!index) {
    return;
  }
  SANE_Int info = 0;
  std::vector<char> buffer(value.begin(), value.end());
  buffer.push_back('\0');
  const SANE_Status status = sane_control_option(
      handle_, *index, SANE_ACTION_SET_VALUE, buffer.data(), &info);
  if (status != SANE_STATUS_GOOD) {
    log(LogLevel::warn, "failed to set SANE option " + name + "=" + value +
                            ": " + sane_status_text(status));
  }
}

void SaneDevice::set_mm_option(const std::string &name, double value) {
  const auto index = find_option(name);
  if (!index) {
    return;
  }
  const SANE_Option_Descriptor *descriptor =
      sane_get_option_descriptor(handle_, *index);
  if (descriptor == nullptr) {
    return;
  }
  SANE_Int info = 0;
  SANE_Word word = descriptor->type == SANE_TYPE_FIXED ? mm_to_sane_fixed(value)
                                                       : static_cast<int>(value);
  const SANE_Status status =
      sane_control_option(handle_, *index, SANE_ACTION_SET_VALUE, &word, &info);
  if (status != SANE_STATUS_GOOD) {
    log(LogLevel::warn, "failed to set SANE option " + name + ": " +
                            sane_status_text(status));
  }
}

void SaneDevice::apply_settings(const ScanSettings &settings) {
  set_int_option("resolution", settings.resolution);
  set_string_option("source", settings.input_source == "Platen" ? "Flatbed"
                                                                 : settings.input_source);
  if (settings.color_mode == ColorMode::color24) {
    set_string_option("mode", "Color");
  } else {
    set_string_option("mode", "Gray");
  }
  set_mm_option("tl-x", settings.x_offset_mm);
  set_mm_option("tl-y", settings.y_offset_mm);
  set_mm_option("br-x", settings.x_offset_mm + settings.width_mm);
  set_mm_option("br-y", settings.y_offset_mm + settings.height_mm);
}

ScanImage SaneDevice::scan(const ScanSettings &settings,
                           const std::atomic_bool &cancel_requested) {
  open();
  apply_settings(settings);

  SANE_Status status = sane_start(handle_);
  if (status != SANE_STATUS_GOOD) {
    throw std::runtime_error("sane_start failed: " + sane_status_text(status));
  }

  SANE_Parameters parameters{};
  status = sane_get_parameters(handle_, &parameters);
  if (status != SANE_STATUS_GOOD) {
    sane_cancel(handle_);
    throw std::runtime_error("sane_get_parameters failed: " +
                             sane_status_text(status));
  }
  if (parameters.depth != 8) {
    sane_cancel(handle_);
    throw std::runtime_error("unsupported SANE sample depth: " +
                             std::to_string(parameters.depth));
  }
  if (parameters.format != SANE_FRAME_GRAY &&
      parameters.format != SANE_FRAME_RGB) {
    sane_cancel(handle_);
    throw std::runtime_error("unsupported SANE frame format");
  }

  const int components = parameters.format == SANE_FRAME_GRAY ? 1 : 3;
  std::vector<std::uint8_t> raw;
  if (parameters.lines > 0 && parameters.bytes_per_line > 0) {
    raw.reserve(static_cast<std::size_t>(parameters.lines) *
                static_cast<std::size_t>(parameters.bytes_per_line));
  }

  std::vector<std::uint8_t> buffer(32 * 1024);
  while (true) {
    if (cancel_requested.load()) {
      sane_cancel(handle_);
      throw std::runtime_error("scan cancelled");
    }

    SANE_Int read_len = 0;
    status = sane_read(handle_, buffer.data(),
                       static_cast<SANE_Int>(buffer.size()), &read_len);
    if (status == SANE_STATUS_EOF) {
      break;
    }
    if (status != SANE_STATUS_GOOD) {
      sane_cancel(handle_);
      throw std::runtime_error("sane_read failed: " + sane_status_text(status));
    }
    raw.insert(raw.end(), buffer.begin(), buffer.begin() + read_len);
  }

  int height = parameters.lines;
  if (height <= 0) {
    if (parameters.bytes_per_line <= 0 ||
        raw.size() % static_cast<std::size_t>(parameters.bytes_per_line) != 0) {
      throw std::runtime_error("cannot infer scanned image height");
    }
    height = static_cast<int>(raw.size() /
                              static_cast<std::size_t>(parameters.bytes_per_line));
  }

  const auto compact =
      compact_rows(raw, parameters.pixels_per_line, height, components,
                   parameters.bytes_per_line);
  ScanImage image;
  image.content_type = "image/jpeg";
  image.bytes =
      encode_jpeg(compact, parameters.pixels_per_line, height,
                  components == 1 ? PixelFormat::gray8 : PixelFormat::rgb8, 85);
  return image;
}

} // namespace xab
