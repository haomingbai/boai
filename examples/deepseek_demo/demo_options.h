#pragma once

#include <string>

namespace boai::examples::deepseek_demo {

struct Options {
  std::string base_url;
  std::string api_key;
  std::string model;
  std::string organization;
  std::string project;
  std::string system;
  std::string user;
  std::string proxy_host;
  std::string proxy_port;
  std::string proxy_auth;
  bool stream{false};
  bool interactive{false};
  bool list_tools{false};
  bool print_reasoning{false};
  bool has_temperature{false};
  double temperature{0.2};
  int max_round_trips{8};
};

bool ParseArgs(int argc, char** argv, Options* out, std::string* error_out);

void PrintUsage(const char* argv0);

}  // namespace boai::examples::deepseek_demo
