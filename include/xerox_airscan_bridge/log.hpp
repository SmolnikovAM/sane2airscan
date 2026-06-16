#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace xab {

enum class LogLevel { debug, info, warn, error };

inline const char *level_name(LogLevel level) {
  switch (level) {
  case LogLevel::debug:
    return "debug";
  case LogLevel::info:
    return "info";
  case LogLevel::warn:
    return "warn";
  case LogLevel::error:
    return "error";
  }
  return "unknown";
}

inline void log(LogLevel level, const std::string &message) {
  static std::mutex mutex;
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_r(&t, &tm);

  std::lock_guard lock(mutex);
  std::cerr << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z") << " level="
            << level_name(level) << " " << message << '\n';
}

} // namespace xab
