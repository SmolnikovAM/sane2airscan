#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xab {

struct Config {
  std::string device_name = "Xerox WorkCentre 3119";
  std::string manufacturer = "Xerox";
  std::string model = "WorkCentre 3119";
  std::string sane_device;
  std::string listen_address = "0.0.0.0";
  std::uint16_t port = 8081;
  std::string uuid = "2f07f7a4-3119-44a7-9a11-xerox31190001";
  bool publish_mdns = true;
  int default_resolution = 300;
  std::string default_color_mode = "Grayscale8";
};

Config parse_config(int argc, char **argv);
void print_usage(const char *argv0);

} // namespace xab
