#include "demo_options.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace boai::examples::deepseek_demo {

namespace {

constexpr std::string_view kDefaultBaseUrl = "https://api.deepseek.com";
constexpr std::string_view kDefaultModel = "deepseek-chat";
constexpr std::string_view kDefaultSystemPrompt =
    "You are a DeepSeek tool-calling demo. "
    "Use tools whenever they help answer questions about time, arithmetic, "
    "random numbers, text inspection, or the local runtime environment. "
    "When you receive tool results, ground your answer in those results and "
    "keep the final reply concise.";

const char* GetEnv(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == 0) {
    return nullptr;
  }
  return value;
}

bool ReadValueArg(int* index, int argc, char** argv, std::string_view arg,
                  std::string_view name, std::string* out,
                  std::string* error_out) {
  if (arg == name) {
    if ((*index) + 1 >= argc) {
      if (error_out != nullptr) {
        *error_out = "missing value for " + std::string(name);
      }
      return false;
    }
    *out = argv[++(*index)];
    return true;
  }

  const auto prefix = std::string(name) + "=";
  if (arg.size() > prefix.size() && arg.starts_with(prefix)) {
    *out = std::string(arg.substr(prefix.size()));
    return true;
  }

  return false;
}

bool ParseIntArg(int* index, int argc, char** argv, std::string_view arg,
                 std::string_view name, int* out, std::string* error_out) {
  std::string value_text;
  if (!ReadValueArg(index, argc, argv, arg, name, &value_text, error_out)) {
    return false;
  }

  try {
    *out = std::stoi(value_text);
  } catch (const std::exception&) {
    if (error_out != nullptr) {
      *error_out = "invalid integer for " + std::string(name);
    }
    return false;
  }
  return true;
}

bool ParseDoubleArg(int* index, int argc, char** argv, std::string_view arg,
                    std::string_view name, double* out,
                    std::string* error_out) {
  std::string value_text;
  if (!ReadValueArg(index, argc, argv, arg, name, &value_text, error_out)) {
    return false;
  }

  try {
    *out = std::stod(value_text);
  } catch (const std::exception&) {
    if (error_out != nullptr) {
      *error_out = "invalid number for " + std::string(name);
    }
    return false;
  }
  return true;
}

}  // namespace

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n\n"
      << "Common options:\n"
      << "  --user <text>            Run one prompt and exit\n"
      << "  --interactive            Start a chat loop (default when --user is omitted)\n"
      << "  --list_tools             Print available demo tools and exit\n"
      << "  --stream                 Stream assistant output\n"
      << "  --print_reasoning        Print reasoning_content to stderr when provided\n"
      << "  --max_round_trips <n>    Maximum assistant/tool rounds per user turn\n"
      << "  --temperature <value>    Override model temperature\n\n"
      << "Provider options:\n"
      << "  --base_url <url>         Default: DEEPSEEK_BASE_URL or "
      << kDefaultBaseUrl << "\n"
      << "  --api_key <key>          Default: DEEPSEEK_API_KEY or OAI_API_KEY\n"
      << "  --model <name>           Default: DEEPSEEK_MODEL or "
      << kDefaultModel << "\n"
      << "  --system <text>          Override the default system prompt\n"
      << "  --organization <org>     Optional OpenAI-compatible header\n"
      << "  --project <proj>         Optional OpenAI-compatible header\n"
      << "  -h, --help               Show this help\n\n"
      << "Interactive commands:\n"
      << "  :tools                   Print tool list\n"
      << "  :reset                   Reset the conversation\n"
      << "  :quit                    Exit\n";
}

bool ParseArgs(int argc, char** argv, Options* out, std::string* error_out) {
  if (out == nullptr) {
    return false;
  }

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i]);

    if (arg == "-h" || arg == "--help") {
      return false;
    }

    if (arg == "--stream") {
      out->stream = true;
      continue;
    }
    if (arg == "--interactive") {
      out->interactive = true;
      continue;
    }
    if (arg == "--list_tools") {
      out->list_tools = true;
      continue;
    }
    if (arg == "--print_reasoning") {
      out->print_reasoning = true;
      continue;
    }

    if (ReadValueArg(&i, argc, argv, arg, "--base_url", &out->base_url,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--api_key", &out->api_key,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--model", &out->model, error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--organization",
                     &out->organization, error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--project", &out->project,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--system", &out->system,
                     error_out)) {
      continue;
    }
    if (ReadValueArg(&i, argc, argv, arg, "--user", &out->user, error_out)) {
      continue;
    }
    if (ParseIntArg(&i, argc, argv, arg, "--max_round_trips",
                    &out->max_round_trips, error_out)) {
      continue;
    }
    if (ParseDoubleArg(&i, argc, argv, arg, "--temperature",
                       &out->temperature, error_out)) {
      out->has_temperature = true;
      continue;
    }

    if (error_out != nullptr) {
      *error_out = "unknown argument: " + std::string(arg);
    }
    return false;
  }

  if (out->base_url.empty()) {
    if (const char* env = GetEnv("DEEPSEEK_BASE_URL")) {
      out->base_url = env;
    } else {
      out->base_url = std::string(kDefaultBaseUrl);
    }
  }
  if (out->model.empty()) {
    if (const char* env = GetEnv("DEEPSEEK_MODEL")) {
      out->model = env;
    } else {
      out->model = std::string(kDefaultModel);
    }
  }
  if (out->api_key.empty()) {
    if (const char* env = GetEnv("DEEPSEEK_API_KEY")) {
      out->api_key = env;
    } else if (const char* env = GetEnv("OAI_API_KEY")) {
      out->api_key = env;
    }
  }
  if (out->system.empty()) {
    out->system = std::string(kDefaultSystemPrompt);
  }
  if (out->user.empty() && !out->interactive && !out->list_tools) {
    out->interactive = true;
  }
  if (out->max_round_trips < 1) {
    if (error_out != nullptr) {
      *error_out = "--max_round_trips must be >= 1";
    }
    return false;
  }
  if (out->base_url.empty() || out->model.empty()) {
    if (error_out != nullptr) {
      *error_out = "--base_url and --model are required";
    }
    return false;
  }

  return true;
}

}  // namespace boai::examples::deepseek_demo
