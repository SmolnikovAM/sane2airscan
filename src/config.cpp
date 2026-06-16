#include "xerox_airscan_bridge/config.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace xab {

namespace {

std::string env_or_default(const char *name, const std::string &fallback) {
  if (const char *value = std::getenv(name); value != nullptr && *value != '\0') {
    return value;
  }
  return fallback;
}

bool env_bool_or_default(const char *name, bool fallback) {
  if (const char *value = std::getenv(name); value != nullptr) {
    const std::string text(value);
    return text == "1" || text == "true" || text == "yes";
  }
  return fallback;
}

int parse_int(const std::string &text, const std::string &name) {
  try {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size()) {
      throw std::invalid_argument("trailing input");
    }
    return value;
  } catch (const std::exception &) {
    throw std::runtime_error("invalid integer for " + name + ": " + text);
  }
}

} // namespace

void print_usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n"
      << "\n"
      << "Options:\n"
      << "  --device-name NAME       Bonjour/eSCL scanner name\n"
      << "  --manufacturer NAME      Scanner manufacturer\n"
      << "  --model NAME             Scanner model\n"
      << "  --serial-number VALUE    Scanner serial number advertised over eSCL\n"
      << "  --sane-device NAME       Exact SANE device name; empty means auto-detect\n"
      << "  --listen-address ADDR    HTTP bind address (default: 0.0.0.0)\n"
      << "  --port PORT              HTTP/eSCL port (default: 8081)\n"
      << "  --uuid UUID              Stable UUID published over eSCL/mDNS\n"
      << "  --default-resolution DPI Default scan resolution\n"
      << "  --default-color-mode M   Grayscale8 or RGB24\n"
      << "  --no-mdns                Disable Avahi publication\n"
      << "  --help                   Show this help\n"
      << "\n"
      << "Environment variables use XAB_ prefixes, for example XAB_SANE_DEVICE.\n";
}

Config parse_config(int argc, char **argv) {
  Config config;
  config.device_name = env_or_default("XAB_DEVICE_NAME", config.device_name);
  config.manufacturer = env_or_default("XAB_MANUFACTURER", config.manufacturer);
  config.model = env_or_default("XAB_MODEL", config.model);
  config.serial_number = env_or_default("XAB_SERIAL_NUMBER", config.serial_number);
  config.sane_device = env_or_default("XAB_SANE_DEVICE", config.sane_device);
  config.listen_address =
      env_or_default("XAB_LISTEN_ADDRESS", config.listen_address);
  config.uuid = env_or_default("XAB_UUID", config.uuid);
  config.default_color_mode =
      env_or_default("XAB_DEFAULT_COLOR_MODE", config.default_color_mode);
  config.publish_mdns = env_bool_or_default("XAB_PUBLISH_MDNS", config.publish_mdns);

  if (const char *port = std::getenv("XAB_PORT"); port != nullptr) {
    const auto parsed = parse_int(port, "XAB_PORT");
    if (parsed <= 0 || parsed > 65535) {
      throw std::runtime_error("XAB_PORT out of range");
    }
    config.port = static_cast<std::uint16_t>(parsed);
  }
  if (const char *resolution = std::getenv("XAB_DEFAULT_RESOLUTION");
      resolution != nullptr) {
    config.default_resolution = parse_int(resolution, "XAB_DEFAULT_RESOLUTION");
  }

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    auto need_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + name);
      }
      return argv[++i];
    };

    if (arg == "--help") {
      print_usage(argv[0]);
      std::exit(0);
    } else if (arg == "--device-name") {
      config.device_name = need_value(arg);
    } else if (arg == "--manufacturer") {
      config.manufacturer = need_value(arg);
    } else if (arg == "--model") {
      config.model = need_value(arg);
    } else if (arg == "--serial-number") {
      config.serial_number = need_value(arg);
    } else if (arg == "--sane-device") {
      config.sane_device = need_value(arg);
    } else if (arg == "--listen-address") {
      config.listen_address = need_value(arg);
    } else if (arg == "--port") {
      const int parsed = parse_int(need_value(arg), arg);
      if (parsed <= 0 || parsed > 65535) {
        throw std::runtime_error("--port out of range");
      }
      config.port = static_cast<std::uint16_t>(parsed);
    } else if (arg == "--uuid") {
      config.uuid = need_value(arg);
    } else if (arg == "--default-resolution") {
      config.default_resolution = parse_int(need_value(arg), arg);
    } else if (arg == "--default-color-mode") {
      config.default_color_mode = need_value(arg);
    } else if (arg == "--no-mdns") {
      config.publish_mdns = false;
    } else {
      throw std::runtime_error("unknown option: " + arg);
    }
  }

  return config;
}

} // namespace xab
