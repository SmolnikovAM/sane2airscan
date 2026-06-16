#pragma once

#include "xerox_airscan_bridge/scanner.hpp"

#include <optional>
#include <string>
#include <vector>

#include <sane/sane.h>

namespace xab {

struct SaneOption {
  int index = 0;
  std::string name;
  std::string title;
  SANE_Value_Type type = SANE_TYPE_INT;
  SANE_Unit unit = SANE_UNIT_NONE;
};

class SaneDevice final : public Scanner {
public:
  explicit SaneDevice(std::string configured_device);
  ~SaneDevice() override;

  SaneDevice(const SaneDevice &) = delete;
  SaneDevice &operator=(const SaneDevice &) = delete;

  std::vector<std::string> list_devices() const;
  std::vector<SaneOption> list_options();
  ScanImage scan(const ScanSettings &settings,
                 const std::atomic_bool &cancel_requested) override;

private:
  std::string resolve_device_name() const;
  void open();
  void close();
  std::optional<int> find_option(const std::string &name) const;
  void set_int_option(const std::string &name, int value);
  void set_string_option(const std::string &name, const std::string &value);
  void set_mm_option(const std::string &name, double value);
  void apply_settings(const ScanSettings &settings);

  std::string configured_device_;
  SANE_Handle handle_ = nullptr;
};

} // namespace xab
