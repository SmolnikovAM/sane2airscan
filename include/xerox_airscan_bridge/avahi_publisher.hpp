#pragma once

#include "xerox_airscan_bridge/config.hpp"

#include <atomic>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/thread-watch.h>

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
  AvahiThreadedPoll *poll_ = nullptr;
  AvahiClient *client_ = nullptr;
  AvahiEntryGroup *group_ = nullptr;
  std::atomic_bool running_{false};
};

} // namespace xab
