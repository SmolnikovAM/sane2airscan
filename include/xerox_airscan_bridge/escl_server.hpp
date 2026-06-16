#pragma once

#include "xerox_airscan_bridge/config.hpp"
#include "xerox_airscan_bridge/http_server.hpp"
#include "xerox_airscan_bridge/scanner.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace xab {

class EsclServer {
public:
  EsclServer(Config config, Scanner &scanner);
  ~EsclServer();

  HttpResponse handle(const HttpRequest &request);

  enum class JobState {
    pending,
    scanning,
    complete,
    failed,
    cancelled,
  };

private:
  struct Job {
    std::string id;
    ScanSettings settings;
    JobState state = JobState::pending;
    std::atomic_bool cancel_requested{false};
    std::optional<ScanImage> image;
    std::string error;
    bool delivered = false;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
  };

  HttpResponse capabilities() const;
  HttpResponse status() const;
  HttpResponse create_job(const HttpRequest &request);
  HttpResponse next_document(const std::string &job_id);
  HttpResponse delete_job(const std::string &job_id);
  ScanSettings parse_scan_settings(const std::string &xml) const;
  void run_job(const std::shared_ptr<Job> &job);
  std::shared_ptr<Job> find_job(const std::string &job_id);

  Config config_;
  Scanner &scanner_;
  mutable std::mutex jobs_mutex_;
  std::unordered_map<std::string, std::shared_ptr<Job>> jobs_;
  std::mutex scanner_mutex_;
  std::uint64_t next_job_id_ = 1;
};

} // namespace xab
