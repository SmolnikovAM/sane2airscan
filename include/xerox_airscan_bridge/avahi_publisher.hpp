#pragma once

#include "xerox_airscan_bridge/config.hpp"

#include <atomic>
#include <thread>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>

namespace xab {

class AvahiPublisher {
public:
  explicit AvahiPublisher(Config config);
  ~AvahiPublisher();

  void start();
  void stop();

private:
  static void client_callback(AvahiClient *client, AvahiClientState state,
                              void *userdata);
  static void entry_group_callback(AvahiEntryGroup *group,
                                   AvahiEntryGroupState state, void *userdata);
  void create_services(AvahiClient *client);

  Config config_;
  AvahiSimplePoll *poll_ = nullptr;
  AvahiClient *client_ = nullptr;
  AvahiEntryGroup *group_ = nullptr;
  std::thread thread_;
  std::atomic_bool running_{false};
};

} // namespace xab
