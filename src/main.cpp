#include "xerox_airscan_bridge/avahi_publisher.hpp"
#include "xerox_airscan_bridge/config.hpp"
#include "xerox_airscan_bridge/escl_server.hpp"
#include "xerox_airscan_bridge/http_server.hpp"
#include "xerox_airscan_bridge/log.hpp"
#include "xerox_airscan_bridge/sane_device.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <sstream>
#include <thread>

namespace {

std::atomic_bool g_stop_requested{false};

void signal_handler(int) {
  g_stop_requested.store(true);
}

std::string option_type_name(SANE_Value_Type type) {
  switch (type) {
  case SANE_TYPE_BOOL:
    return "bool";
  case SANE_TYPE_INT:
    return "int";
  case SANE_TYPE_FIXED:
    return "fixed";
  case SANE_TYPE_STRING:
    return "string";
  case SANE_TYPE_BUTTON:
    return "button";
  case SANE_TYPE_GROUP:
    return "group";
  }
  return "unknown";
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    xab::Config config = xab::parse_config(argc, argv);
    xab::SaneDevice scanner(config.sane_device);

    for (const auto &device : scanner.list_devices()) {
      xab::log(xab::LogLevel::info, "SANE device=" + device);
    }
    for (const auto &option : scanner.list_options()) {
      std::ostringstream message;
      message << "SANE option index=" << option.index << " name=" << option.name
              << " title=\"" << option.title
              << "\" type=" << option_type_name(option.type);
      xab::log(xab::LogLevel::debug, message.str());
    }

    xab::EsclServer escl(config, scanner);
    xab::HttpServer http(config.listen_address, config.port,
                         [&](const xab::HttpRequest &request) {
                           return escl.handle(request);
                         });
    http.start();

    std::unique_ptr<xab::AvahiPublisher> publisher;
    if (config.publish_mdns) {
      publisher = std::make_unique<xab::AvahiPublisher>(config);
      publisher->start();
    }

    while (!g_stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (publisher) {
      publisher->stop();
    }
    escl.stop();
    http.stop();
    xab::log(xab::LogLevel::info, "stopped");
    return 0;
  } catch (const std::exception &error) {
    xab::log(xab::LogLevel::error, error.what());
    return 1;
  }
}
