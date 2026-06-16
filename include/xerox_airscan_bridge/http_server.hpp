#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace xab {

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
};

struct HttpResponse {
  int status = 200;
  std::string reason;
  std::map<std::string, std::string> headers;
  std::string body;
};

class HttpServer {
public:
  using Handler = std::function<HttpResponse(const HttpRequest &)>;

  HttpServer(std::string listen_address, std::uint16_t port, Handler handler);
  ~HttpServer();

  void start();
  void stop();

private:
  void accept_loop();
  void handle_client(int client_fd);

  std::string listen_address_;
  std::uint16_t port_;
  Handler handler_;
  int server_fd_ = -1;
  std::atomic_bool running_{false};
  std::thread accept_thread_;
};

} // namespace xab
