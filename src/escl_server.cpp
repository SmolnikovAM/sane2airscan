#include "xerox_airscan_bridge/escl_server.hpp"

#include "xerox_airscan_bridge/log.hpp"

#include <chrono>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace xab {

namespace {

std::string xml_escape(const std::string &input) {
  std::string out;
  for (char c : input) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    default:
      out += c;
    }
  }
  return out;
}

HttpResponse xml_response(std::string body) {
  HttpResponse response;
  response.headers["Content-Type"] = "text/xml; charset=utf-8";
  response.body = std::move(body);
  return response;
}

HttpResponse text_response(int status, std::string body) {
  HttpResponse response;
  response.status = status;
  response.headers["Content-Type"] = "text/plain; charset=utf-8";
  response.body = std::move(body);
  return response;
}

std::optional<std::string> xml_text(const std::string &xml,
                                    const std::string &local_name) {
  const std::regex tag("<(?:[A-Za-z0-9_\\-]+:)?" + local_name +
                       R"(\b[^>]*>([^<]+)</(?:[A-Za-z0-9_\-]+:)?)" +
                       local_name + ">");
  std::smatch match;
  if (std::regex_search(xml, match, tag)) {
    return match[1].str();
  }
  return std::nullopt;
}

std::optional<int> xml_int(const std::string &xml, const std::string &name) {
  if (auto text = xml_text(xml, name)) {
    try {
      return std::stoi(*text);
    } catch (const std::exception &) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

double three_hundredths_to_mm(int value) {
  return static_cast<double>(value) * 25.4 / 300.0;
}

std::string first_supported_format(const std::string &requested) {
  if (requested == "image/jpeg" || requested == "image/jpg") {
    return "image/jpeg";
  }
  return "image/jpeg";
}

std::string state_name(EsclServer::JobState state) {
  switch (state) {
  case EsclServer::JobState::pending:
    return "Pending";
  case EsclServer::JobState::scanning:
    return "Processing";
  case EsclServer::JobState::complete:
    return "Completed";
  case EsclServer::JobState::failed:
    return "Aborted";
  case EsclServer::JobState::cancelled:
    return "Canceled";
  }
  return "Unknown";
}

HttpResponse scanner_icon() {
  static constexpr unsigned char png[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89,
      0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63,
      0x60, 0x60, 0x60, 0xf8, 0xff, 0xff, 0x3f, 0x00, 0x05, 0xfe, 0x02,
      0xfe, 0xdc, 0xcc, 0x59, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
      0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  HttpResponse response;
  response.headers["Content-Type"] = "image/png";
  response.body.assign(reinterpret_cast<const char *>(png), sizeof(png));
  return response;
}

} // namespace

EsclServer::EsclServer(Config config, Scanner &scanner)
    : config_(std::move(config)), scanner_(scanner) {}

EsclServer::~EsclServer() {
  stop();
  std::vector<std::shared_ptr<Job>> jobs;
  {
    std::lock_guard lock(jobs_mutex_);
    for (auto &[_, job] : jobs_) {
      jobs.push_back(job);
    }
  }
  for (auto &job : jobs) {
    if (job->worker.joinable()) {
      job->worker.join();
    }
  }
}

void EsclServer::stop() {
  std::vector<std::shared_ptr<Job>> jobs;
  {
    std::lock_guard lock(jobs_mutex_);
    for (auto &[_, job] : jobs_) {
      jobs.push_back(job);
    }
  }

  for (auto &job : jobs) {
    job->cancel_requested.store(true);
    {
      std::lock_guard lock(job->mutex);
      if (job->state == JobState::pending || job->state == JobState::scanning) {
        job->state = JobState::cancelled;
      }
      job->updated_at = std::chrono::steady_clock::now();
    }
    job->cv.notify_all();
  }
}

HttpResponse EsclServer::handle(const HttpRequest &request) {
  reap_jobs();
  log(LogLevel::info, "HTTP " + request.method + " " + request.path);
  if (request.method == "GET" && request.path == "/eSCL/ScannerCapabilities") {
    return capabilities();
  }
  if (request.method == "GET" && request.path == "/eSCL/ScannerStatus") {
    return status();
  }
  if (request.method == "GET" && request.path == "/eSCL/ScannerIcon") {
    return scanner_icon();
  }
  if (request.method == "POST" && request.path == "/eSCL/ScanJobs") {
    return create_job(request);
  }

  const std::regex next_doc(R"(^/eSCL/ScanJobs/([^/]+)/NextDocument$)");
  const std::regex job_path(R"(^/eSCL/ScanJobs/([^/]+)$)");
  std::smatch match;
  if (request.method == "GET" && std::regex_match(request.path, match, next_doc)) {
    return next_document(match[1].str());
  }
  if (request.method == "DELETE" && std::regex_match(request.path, match, job_path)) {
    return delete_job(match[1].str());
  }
  return text_response(404, "not found\n");
}

HttpResponse EsclServer::capabilities() const {
  std::ostringstream xml;
  xml << R"(<?xml version="1.0" encoding="UTF-8"?>)"
      << R"(<scan:ScannerCapabilities xmlns:scan="http://schemas.hp.com/imaging/escl/2011/05/03" xmlns:pwg="http://www.pwg.org/schemas/2010/12/sm">)"
      << "<pwg:Version>2.63</pwg:Version>"
      << "<pwg:MakeAndModel>" << xml_escape(config_.manufacturer + " " + config_.model)
      << "</pwg:MakeAndModel>"
      << "<pwg:SerialNumber>" << xml_escape(config_.serial_number)
      << "</pwg:SerialNumber>"
      << "<scan:UUID>urn:uuid:" << xml_escape(config_.uuid) << "</scan:UUID>"
      << "<scan:AdminURI>/</scan:AdminURI>"
      << "<scan:IconURI>/eSCL/ScannerIcon</scan:IconURI>"
      << "<scan:Platen>"
      << "<scan:PlatenInputCaps>"
      << "<scan:MinWidth>1</scan:MinWidth><scan:MaxWidth>2480</scan:MaxWidth>"
      << "<scan:MinHeight>1</scan:MinHeight><scan:MaxHeight>3508</scan:MaxHeight>"
      << "<scan:MaxScanRegions>1</scan:MaxScanRegions>"
      << "<scan:SettingProfiles>"
      << "<scan:SettingProfile>"
      << "<scan:ColorModes>"
      << "<scan:ColorMode>Grayscale8</scan:ColorMode>"
      << "<scan:ColorMode>RGB24</scan:ColorMode>"
      << "</scan:ColorModes>"
      << "<scan:ContentTypes>"
      << "<scan:ContentType>Document</scan:ContentType>"
      << "<scan:ContentType>Photo</scan:ContentType>"
      << "</scan:ContentTypes>"
      << "<scan:DocumentFormats>"
      << "<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>"
      << "</scan:DocumentFormats>"
      << "<scan:SupportedResolutions>"
      << "<scan:DiscreteResolutions>"
      << "<scan:DiscreteResolution><scan:XResolution>100</scan:XResolution><scan:YResolution>100</scan:YResolution></scan:DiscreteResolution>"
      << "<scan:DiscreteResolution><scan:XResolution>150</scan:XResolution><scan:YResolution>150</scan:YResolution></scan:DiscreteResolution>"
      << "<scan:DiscreteResolution><scan:XResolution>200</scan:XResolution><scan:YResolution>200</scan:YResolution></scan:DiscreteResolution>"
      << "<scan:DiscreteResolution><scan:XResolution>300</scan:XResolution><scan:YResolution>300</scan:YResolution></scan:DiscreteResolution>"
      << "</scan:DiscreteResolutions>"
      << "</scan:SupportedResolutions>"
      << "</scan:SettingProfile>"
      << "</scan:SettingProfiles>"
      << "</scan:PlatenInputCaps>"
      << "</scan:Platen>"
      << "</scan:ScannerCapabilities>";
  return xml_response(xml.str());
}

HttpResponse EsclServer::status() const {
  bool processing = false;
  {
    std::lock_guard lock(jobs_mutex_);
    for (const auto &[_, job] : jobs_) {
      std::lock_guard job_lock(job->mutex);
      if (job->state == JobState::pending || job->state == JobState::scanning) {
        processing = true;
        break;
      }
    }
  }

  std::ostringstream xml;
  xml << R"(<?xml version="1.0" encoding="UTF-8"?>)"
      << R"(<scan:ScannerStatus xmlns:scan="http://schemas.hp.com/imaging/escl/2011/05/03" xmlns:pwg="http://www.pwg.org/schemas/2010/12/sm">)"
      << "<pwg:Version>2.63</pwg:Version>"
      << "<pwg:State>" << (processing ? "Processing" : "Idle") << "</pwg:State>"
      << "</scan:ScannerStatus>";
  return xml_response(xml.str());
}

HttpResponse EsclServer::create_job(const HttpRequest &request) {
  reap_jobs();
  auto job = std::make_shared<Job>();
  job->settings = parse_scan_settings(request.body);

  {
    std::lock_guard lock(jobs_mutex_);
    job->id = std::to_string(next_job_id_++);
    jobs_[job->id] = job;
  }

  job->worker = std::thread([this, job] { run_job(job); });

  HttpResponse response;
  response.status = 201;
  response.reason = "Created";
  response.headers["Location"] = "/eSCL/ScanJobs/" + job->id;
  response.headers["Content-Type"] = "text/plain; charset=utf-8";
  response.body = "created\n";
  return response;
}

HttpResponse EsclServer::next_document(const std::string &job_id) {
  auto job = find_job(job_id);
  if (!job) {
    return text_response(404, "job not found\n");
  }

  std::unique_lock lock(job->mutex);
  job->cv.wait_for(lock, std::chrono::minutes(3), [&] {
    return job->state == JobState::complete || job->state == JobState::failed ||
           job->state == JobState::cancelled;
  });

  if (job->state == JobState::pending || job->state == JobState::scanning) {
    HttpResponse response = text_response(503, "scan still in progress\n");
    response.headers["Retry-After"] = "2";
    return response;
  }
  if (job->state == JobState::cancelled) {
    return text_response(409, "job cancelled\n");
  }
  if (job->state == JobState::failed) {
    return text_response(500, "scan failed: " + job->error + "\n");
  }
  if (job->delivered || !job->image) {
    return text_response(404, "no more documents\n");
  }

  job->delivered = true;
  job->updated_at = std::chrono::steady_clock::now();
  HttpResponse response;
  response.status = 200;
  response.headers["Content-Type"] = job->image->content_type;
  response.body.assign(reinterpret_cast<const char *>(job->image->bytes.data()),
                       job->image->bytes.size());
  return response;
}

HttpResponse EsclServer::delete_job(const std::string &job_id) {
  auto job = find_job(job_id);
  if (!job) {
    return text_response(404, "job not found\n");
  }
  job->cancel_requested.store(true);
  {
    std::lock_guard lock(job->mutex);
    job->delivered = true;
    if (job->state == JobState::pending || job->state == JobState::scanning) {
      job->state = JobState::cancelled;
    }
    job->updated_at = std::chrono::steady_clock::now();
  }
  job->cv.notify_all();
  reap_jobs();
  return text_response(204, "");
}

ScanSettings EsclServer::parse_scan_settings(const std::string &xml) const {
  ScanSettings settings;
  settings.resolution = config_.default_resolution;
  settings.color_mode =
      config_.default_color_mode == "RGB24" ? ColorMode::color24
                                            : ColorMode::grayscale8;

  if (auto resolution = xml_int(xml, "XResolution")) {
    if (*resolution == 100 || *resolution == 150 || *resolution == 200 ||
        *resolution == 300) {
      settings.resolution = *resolution;
    }
  }
  if (auto color_mode = xml_text(xml, "ColorMode")) {
    if (*color_mode == "RGB24" || *color_mode == "Color") {
      settings.color_mode = ColorMode::color24;
    } else {
      settings.color_mode = ColorMode::grayscale8;
    }
  }
  if (auto format = xml_text(xml, "DocumentFormat")) {
    settings.document_format = first_supported_format(*format);
  }
  if (auto input_source = xml_text(xml, "InputSource")) {
    settings.input_source = *input_source == "Platen" ? "Platen" : *input_source;
  }
  const auto x_offset = xml_int(xml, "XOffset").value_or(0);
  const auto y_offset = xml_int(xml, "YOffset").value_or(0);
  const auto width = xml_int(xml, "Width");
  const auto height = xml_int(xml, "Height");
  if (width && height && *width > 0 && *height > 0) {
    settings.x_offset_mm = three_hundredths_to_mm(x_offset);
    settings.y_offset_mm = three_hundredths_to_mm(y_offset);
    settings.width_mm = three_hundredths_to_mm(*width);
    settings.height_mm = three_hundredths_to_mm(*height);
  }
  return settings;
}

void EsclServer::run_job(const std::shared_ptr<Job> &job) {
  JobState final_state = JobState::failed;
  {
    std::lock_guard lock(job->mutex);
    job->state = JobState::scanning;
    job->updated_at = std::chrono::steady_clock::now();
  }
  job->cv.notify_all();

  try {
    std::lock_guard scan_lock(scanner_mutex_);
    if (job->cancel_requested.load()) {
      throw std::runtime_error("scan cancelled");
    }
    ScanImage image = scanner_.scan(job->settings, job->cancel_requested);
    {
      std::lock_guard lock(job->mutex);
      job->image = std::move(image);
      job->state = job->cancel_requested.load() ? JobState::cancelled
                                                : JobState::complete;
      job->worker_done = true;
      job->updated_at = std::chrono::steady_clock::now();
      final_state = job->state;
    }
  } catch (const std::exception &error) {
    std::lock_guard lock(job->mutex);
    job->error = error.what();
    job->state = job->cancel_requested.load() ? JobState::cancelled
                                              : JobState::failed;
    job->worker_done = true;
    job->updated_at = std::chrono::steady_clock::now();
    final_state = job->state;
    log(LogLevel::error, "scan job " + job->id + " failed: " + job->error);
  }
  log(LogLevel::info, "scan job " + job->id + " state=" + state_name(final_state));
  job->cv.notify_all();
}

std::shared_ptr<EsclServer::Job> EsclServer::find_job(const std::string &job_id) {
  std::lock_guard lock(jobs_mutex_);
  const auto it = jobs_.find(job_id);
  if (it == jobs_.end()) {
    return nullptr;
  }
  return it->second;
}

void EsclServer::reap_jobs() {
  const auto now = std::chrono::steady_clock::now();
  std::vector<std::shared_ptr<Job>> stale_jobs;
  {
    std::lock_guard lock(jobs_mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
      auto &job = it->second;
      bool stale = false;
      {
        std::lock_guard job_lock(job->mutex);
        const bool terminal =
            job->state == JobState::complete || job->state == JobState::failed ||
            job->state == JobState::cancelled;
        const bool old_enough = now - job->updated_at > std::chrono::minutes(10);
        stale = terminal && job->worker_done && (job->delivered || old_enough);
      }
      if (stale) {
        stale_jobs.push_back(job);
        it = jobs_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (auto &job : stale_jobs) {
    if (job->worker.joinable() &&
        job->worker.get_id() != std::this_thread::get_id()) {
      job->worker.join();
    }
  }
}

} // namespace xab
