#include "xerox_airscan_bridge/escl_server.hpp"

#include "xerox_airscan_bridge/log.hpp"

#include <chrono>
#include <regex>
#include <sstream>
#include <stdexcept>
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

} // namespace

EsclServer::EsclServer(Config config, Scanner &scanner)
    : config_(std::move(config)), scanner_(scanner) {}

EsclServer::~EsclServer() {
  std::vector<std::shared_ptr<Job>> jobs;
  {
    std::lock_guard lock(jobs_mutex_);
    for (auto &[_, job] : jobs_) {
      job->cancel_requested.store(true);
      jobs.push_back(job);
    }
  }
  for (auto &job : jobs) {
    if (job->worker.joinable()) {
      job->worker.join();
    }
  }
}

HttpResponse EsclServer::handle(const HttpRequest &request) {
  log(LogLevel::info, "HTTP " + request.method + " " + request.path);
  if (request.method == "GET" && request.path == "/eSCL/ScannerCapabilities") {
    return capabilities();
  }
  if (request.method == "GET" && request.path == "/eSCL/ScannerStatus") {
    return status();
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
      << "<scan:Version>2.63</scan:Version>"
      << "<scan:MakeAndModel>" << xml_escape(config_.device_name)
      << "</scan:MakeAndModel>"
      << "<scan:SerialNumber>" << xml_escape(config_.uuid)
      << "</scan:SerialNumber>"
      << "<scan:UUID>urn:uuid:" << xml_escape(config_.uuid) << "</scan:UUID>"
      << "<scan:AdminURI>/</scan:AdminURI>"
      << "<scan:IconURI>/eSCL/ScannerIcon</scan:IconURI>"
      << "<scan:SettingProfiles>"
      << "<scan:SettingProfile><scan:ColorModes>"
      << "<scan:ColorMode>Grayscale8</scan:ColorMode>"
      << "<scan:ColorMode>RGB24</scan:ColorMode>"
      << "</scan:ColorModes><scan:DocumentFormats>"
      << "<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>"
      << "</scan:DocumentFormats></scan:SettingProfile>"
      << "</scan:SettingProfiles>"
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
      << R"(<scan:ScannerStatus xmlns:scan="http://schemas.hp.com/imaging/escl/2011/05/03">)"
      << "<scan:Version>2.63</scan:Version>"
      << "<scan:State>" << (processing ? "Processing" : "Idle") << "</scan:State>"
      << "</scan:ScannerStatus>";
  return xml_response(xml.str());
}

HttpResponse EsclServer::create_job(const HttpRequest &request) {
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
    if (job->state == JobState::pending || job->state == JobState::scanning) {
      job->state = JobState::cancelled;
    }
  }
  job->cv.notify_all();
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
    settings.document_format = *format;
  }
  return settings;
}

void EsclServer::run_job(const std::shared_ptr<Job> &job) {
  {
    std::lock_guard lock(job->mutex);
    job->state = JobState::scanning;
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
    }
  } catch (const std::exception &error) {
    std::lock_guard lock(job->mutex);
    job->error = error.what();
    job->state = job->cancel_requested.load() ? JobState::cancelled
                                              : JobState::failed;
    log(LogLevel::error, "scan job " + job->id + " failed: " + job->error);
  }
  log(LogLevel::info, "scan job " + job->id + " state=" + state_name(job->state));
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

} // namespace xab
