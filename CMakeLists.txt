cmake_minimum_required(VERSION 3.20)

project(xerox_airscan_bridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SANE REQUIRED IMPORTED_TARGET sane-backends)
pkg_check_modules(AVAHI_CLIENT REQUIRED IMPORTED_TARGET avahi-client)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)
find_package(Threads REQUIRED)

add_executable(xerox-airscan-bridge
  src/avahi_publisher.cpp
  src/config.cpp
  src/escl_server.cpp
  src/http_server.cpp
  src/jpeg_encoder.cpp
  src/main.cpp
  src/sane_device.cpp
)

target_include_directories(xerox-airscan-bridge PRIVATE
  include
  ${SANE_INCLUDE_DIRS}
  ${AVAHI_CLIENT_INCLUDE_DIRS}
  ${LIBJPEG_INCLUDE_DIRS}
)

target_compile_options(xerox-airscan-bridge PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  ${SANE_CFLAGS_OTHER}
  ${AVAHI_CLIENT_CFLAGS_OTHER}
  ${LIBJPEG_CFLAGS_OTHER}
)

target_link_libraries(xerox-airscan-bridge PRIVATE
  Threads::Threads
  PkgConfig::SANE
  PkgConfig::AVAHI_CLIENT
  PkgConfig::LIBJPEG
)

install(TARGETS xerox-airscan-bridge RUNTIME DESTINATION bin)
