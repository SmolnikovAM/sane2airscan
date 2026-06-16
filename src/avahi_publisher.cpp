#include "xerox_airscan_bridge/avahi_publisher.hpp"

#include "xerox_airscan_bridge/log.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

#include <avahi-common/error.h>
#include <avahi-common/malloc.h>

namespace xab {

AvahiPublisher::AvahiPublisher(Config config) : config_(std::move(config)) {}

AvahiPublisher::~AvahiPublisher() { stop(); }

void AvahiPublisher::start() {
  if (running_.load()) {
    return;
  }

  int error = 0;
  poll_ = avahi_simple_poll_new();
  if (poll_ == nullptr) {
    throw std::runtime_error("avahi_simple_poll_new failed");
  }

  client_ = avahi_client_new(avahi_simple_poll_get(poll_), AVAHI_CLIENT_NO_FAIL,
                             &AvahiPublisher::client_callback, this, &error);
  if (client_ == nullptr) {
    avahi_simple_poll_free(poll_);
    poll_ = nullptr;
    throw std::runtime_error("avahi_client_new failed: " +
                             std::string(avahi_strerror(error)));
  }

  running_.store(true);
  thread_ = std::thread([this] {
    log(LogLevel::info, "Avahi publisher started service=" + config_.device_name);
    avahi_simple_poll_loop(poll_);
  });
}

void AvahiPublisher::stop() {
  running_.store(false);
  if (poll_ != nullptr) {
    avahi_simple_poll_quit(poll_);
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  if (group_ != nullptr) {
    avahi_entry_group_free(group_);
    group_ = nullptr;
  }
  if (client_ != nullptr) {
    avahi_client_free(client_);
    client_ = nullptr;
  }
  if (poll_ != nullptr) {
    avahi_simple_poll_free(poll_);
    poll_ = nullptr;
  }
}

void AvahiPublisher::client_callback(AvahiClient *client, AvahiClientState state,
                                     void *userdata) {
  auto *publisher = static_cast<AvahiPublisher *>(userdata);
  switch (state) {
  case AVAHI_CLIENT_S_RUNNING:
    publisher->create_services(client);
    break;
  case AVAHI_CLIENT_FAILURE:
    log(LogLevel::error, "Avahi client failure: " +
                             std::string(avahi_strerror(avahi_client_errno(client))));
    avahi_simple_poll_quit(publisher->poll_);
    break;
  case AVAHI_CLIENT_S_COLLISION:
  case AVAHI_CLIENT_S_REGISTERING:
    if (publisher->group_ != nullptr) {
      avahi_entry_group_reset(publisher->group_);
    }
    break;
  case AVAHI_CLIENT_CONNECTING:
    break;
  }
}

void AvahiPublisher::entry_group_callback(AvahiEntryGroup *group,
                                          AvahiEntryGroupState state,
                                          void *userdata) {
  auto *publisher = static_cast<AvahiPublisher *>(userdata);
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    log(LogLevel::info, "published _uscan._tcp name=" + publisher->config_.device_name);
    break;
  case AVAHI_ENTRY_GROUP_COLLISION: {
    char *alternative =
        avahi_alternative_service_name(publisher->config_.device_name.c_str());
    publisher->config_.device_name = alternative;
    avahi_free(alternative);
    log(LogLevel::warn, "Avahi name collision, retrying name=" +
                            publisher->config_.device_name);
    avahi_entry_group_reset(group);
    publisher->create_services(avahi_entry_group_get_client(group));
    break;
  }
  case AVAHI_ENTRY_GROUP_FAILURE:
    log(LogLevel::error,
        "Avahi entry group failure: " +
            std::string(avahi_strerror(
                avahi_client_errno(avahi_entry_group_get_client(group)))));
    break;
  case AVAHI_ENTRY_GROUP_UNCOMMITED:
  case AVAHI_ENTRY_GROUP_REGISTERING:
    break;
  }
}

void AvahiPublisher::create_services(AvahiClient *client) {
  if (group_ == nullptr) {
    group_ = avahi_entry_group_new(client, &AvahiPublisher::entry_group_callback,
                                   this);
    if (group_ == nullptr) {
      log(LogLevel::error, "avahi_entry_group_new failed");
      return;
    }
  }

  if (!avahi_entry_group_is_empty(group_)) {
    return;
  }

  AvahiStringList *txt = nullptr;
  txt = avahi_string_list_add_pair(txt, "rs", "eSCL");
  txt = avahi_string_list_add_pair(txt, "ty", config_.device_name.c_str());
  txt = avahi_string_list_add_pair(txt, "note", config_.device_name.c_str());
  txt = avahi_string_list_add_pair(txt, "adminurl", "/");
  txt = avahi_string_list_add_pair(txt, "pdl", "image/jpeg");
  txt = avahi_string_list_add_pair(txt, "is", "platen");
  txt = avahi_string_list_add_pair(txt, "uuid", config_.uuid.c_str());
  txt = avahi_string_list_add_pair(txt, "representation", "images");
  txt = avahi_string_list_add_pair(txt, "vers", "2.63");

  const int add_status = avahi_entry_group_add_service_strlst(
      group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, static_cast<AvahiPublishFlags>(0),
      config_.device_name.c_str(), "_uscan._tcp", nullptr, nullptr, config_.port,
      txt);
  avahi_string_list_free(txt);

  if (add_status < 0) {
    log(LogLevel::error, "failed to add Avahi service: " +
                             std::string(avahi_strerror(add_status)));
    return;
  }

  const int commit_status = avahi_entry_group_commit(group_);
  if (commit_status < 0) {
    log(LogLevel::error, "failed to commit Avahi service: " +
                             std::string(avahi_strerror(commit_status)));
  }
}

} // namespace xab
