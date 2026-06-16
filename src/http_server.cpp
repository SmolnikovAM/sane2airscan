#include "xerox_airscan_bridge/http_server.hpp"

#include "xerox_airscan_bridge/log.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

#include <arpa/inet.h>

namespace xab {

namespace {

std::string trim(std::string text) {
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' ||
                          text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  std::size_t start = 0;
  while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
    ++start;
  }
  return text.substr(start);
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool send_all(int fd, const char *data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const ssize_t n = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

HttpRequest parse_request(const std::string &data) {
  const auto header_end = data.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    throw std::runtime_error("malformed HTTP request");
  }
  const std::string headers = data.substr(0, header_end);
  HttpRequest request;

  std::size_t line_start = 0;
  std::size_t line_end = headers.find("\r\n");
  const std::string request_line = headers.substr(line_start, line_end);
  const auto method_end = request_line.find(' ');
  const auto path_end = request_line.find(' ', method_end + 1);
  if (method_end == std::string::npos || path_end == std::string::npos) {
    throw std::runtime_error("malformed HTTP request line");
  }
  request.method = request_line.substr(0, method_end);
  request.path = request_line.substr(method_end + 1, path_end - method_end - 1);

  line_start = line_end == std::string::npos ? headers.size() : line_end + 2;
  while (line_start < headers.size()) {
    line_end = headers.find("\r\n", line_start);
    const std::string line =
        headers.substr(line_start, line_end == std::string::npos
                                       ? std::string::npos
                                       : line_end - line_start);
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
      request.headers[lower(trim(line.substr(0, colon)))] =
          trim(line.substr(colon + 1));
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 2;
  }

  request.body = data.substr(header_end + 4);
  return request;
}

std::string reason_for_status(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 409:
    return "Conflict";
  case 500:
    return "Internal Server Error";
  case 503:
    return "Service Unavailable";
  default:
    return "OK";
  }
}

} // namespace

HttpServer::HttpServer(std::string listen_address, std::uint16_t port,
                       Handler handler)
    : listen_address_(std::move(listen_address)), port_(port),
      handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
  }

  int reuse = 1;
  ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port_);
  if (::inet_pton(AF_INET, listen_address_.c_str(), &address.sin_addr) != 1) {
    ::close(server_fd_);
    server_fd_ = -1;
    throw std::runtime_error("invalid listen address: " + listen_address_);
  }

  if (::bind(server_fd_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0) {
    ::close(server_fd_);
    server_fd_ = -1;
    throw std::runtime_error("bind failed: " + std::string(std::strerror(errno)));
  }
  if (::listen(server_fd_, 16) != 0) {
    ::close(server_fd_);
    server_fd_ = -1;
    throw std::runtime_error("listen failed: " + std::string(std::strerror(errno)));
  }

  running_.store(true);
  accept_thread_ = std::thread([this] { accept_loop(); });
}

void HttpServer::stop() {
  running_.store(false);
  if (server_fd_ >= 0) {
    ::shutdown(server_fd_, SHUT_RDWR);
    ::close(server_fd_);
    server_fd_ = -1;
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

void HttpServer::accept_loop() {
  log(LogLevel::info, "HTTP server listening address=" + listen_address_ +
                          " port=" + std::to_string(port_));
  while (running_.load()) {
    sockaddr_in client_address{};
    socklen_t client_len = sizeof(client_address);
    const int client_fd =
        ::accept(server_fd_, reinterpret_cast<sockaddr *>(&client_address),
                 &client_len);
    if (client_fd < 0) {
      if (running_.load()) {
        log(LogLevel::warn,
            "accept failed: " + std::string(std::strerror(errno)));
      }
      continue;
    }
    std::thread([this, client_fd] { handle_client(client_fd); }).detach();
  }
}

void HttpServer::handle_client(int client_fd) {
  std::string data;
  std::array<char, 8192> buffer{};

  try {
    while (data.find("\r\n\r\n") == std::string::npos) {
      const ssize_t n = ::recv(client_fd, buffer.data(), buffer.size(), 0);
      if (n <= 0) {
        ::close(client_fd);
        return;
      }
      data.append(buffer.data(), static_cast<std::size_t>(n));
      if (data.size() > 1024 * 1024) {
        throw std::runtime_error("HTTP headers too large");
      }
    }

    HttpRequest request = parse_request(data);
    std::size_t content_length = 0;
    if (const auto it = request.headers.find("content-length");
        it != request.headers.end()) {
      content_length = static_cast<std::size_t>(std::stoul(it->second));
    }
    const auto header_end = data.find("\r\n\r\n");
    while (data.size() < header_end + 4 + content_length) {
      const ssize_t n = ::recv(client_fd, buffer.data(), buffer.size(), 0);
      if (n <= 0) {
        break;
      }
      data.append(buffer.data(), static_cast<std::size_t>(n));
    }
    request.body = data.substr(header_end + 4, content_length);

    HttpResponse response = handler_(request);
    if (response.reason.empty()) {
      response.reason = reason_for_status(response.status);
    }
    response.headers.try_emplace("Server", "xerox-airscan-bridge/0.1");
    response.headers.try_emplace("Connection", "close");
    response.headers["Content-Length"] = std::to_string(response.body.size());

    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
                       response.reason + "\r\n";
    for (const auto &[name, value] : response.headers) {
      wire += name + ": " + value + "\r\n";
    }
    wire += "\r\n";
    send_all(client_fd, wire.data(), wire.size());
    if (!response.body.empty()) {
      send_all(client_fd, response.body.data(), response.body.size());
    }
  } catch (const std::exception &error) {
    const std::string body = "internal error\n";
    const std::string wire =
        "HTTP/1.1 500 Internal Server Error\r\nConnection: close\r\n"
        "Content-Type: text/plain\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    log(LogLevel::error, "HTTP client failed: " + std::string(error.what()));
    send_all(client_fd, wire.data(), wire.size());
  }

  ::close(client_fd);
}

} // namespace xab
