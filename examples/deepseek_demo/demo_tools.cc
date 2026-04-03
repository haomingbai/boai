#include "demo_tools.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <ios>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace boai::examples::deepseek_demo {

namespace completion = boai::completion;

namespace {

std::optional<std::string> JsonStringField(const json::object& obj,
                                           std::string_view key) {
  const json::value* value = obj.if_contains(key);
  if (value == nullptr || !value->is_string()) {
    return std::nullopt;
  }
  return std::string(value->as_string().c_str());
}

std::optional<double> JsonNumberField(const json::object& obj,
                                      std::string_view key) {
  const json::value* value = obj.if_contains(key);
  if (value == nullptr) {
    return std::nullopt;
  }

  if (value->is_int64()) {
    return static_cast<double>(value->as_int64());
  }
  if (value->is_uint64()) {
    return static_cast<double>(value->as_uint64());
  }
  if (value->is_double()) {
    return value->as_double();
  }
  return std::nullopt;
}

std::optional<std::int64_t> JsonIntField(const json::object& obj,
                                         std::string_view key) {
  const json::value* value = obj.if_contains(key);
  if (value == nullptr) {
    return std::nullopt;
  }

  if (value->is_int64()) {
    return value->as_int64();
  }
  if (value->is_uint64()) {
    const auto raw = value->as_uint64();
    if (raw <=
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return static_cast<std::int64_t>(raw);
    }
  }
  if (value->is_double()) {
    const double raw = value->as_double();
    const double rounded = std::round(raw);
    if (std::fabs(raw - rounded) < 1e-9 &&
        rounded >=
            static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
        rounded <=
            static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
      return static_cast<std::int64_t>(rounded);
    }
  }
  return std::nullopt;
}

json::object MakeToolEnvelope(std::string_view name, const json::value& input,
                              json::value output) {
  json::object result;
  result["ok"] = true;
  result["tool"] = std::string(name);
  result["input"] = input;
  result["output"] = std::move(output);
  return result;
}

json::object MakeToolError(std::string_view name, const json::value& input,
                           std::string message) {
  json::object result;
  result["ok"] = false;
  result["tool"] = std::string(name);
  result["input"] = input;
  result["error"] = std::move(message);
  return result;
}

const json::object* RequireObjectArgs(const json::value& args,
                                      std::string* error_out) {
  if (!args.is_object()) {
    if (error_out != nullptr) {
      *error_out = "tool arguments must be a JSON object";
    }
    return nullptr;
  }
  return &args.as_object();
}

std::string DetectPlatform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__) && defined(__MACH__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#elif defined(__FreeBSD__)
  return "freebsd";
#else
  return "unknown";
#endif
}

std::string DetectArch() {
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#elif defined(__i386__) || defined(_M_IX86)
  return "x86";
#else
  return "unknown";
#endif
}

std::string DetectCompiler() {
#if defined(__clang__)
  return "clang-" + std::to_string(__clang_major__) + "." +
         std::to_string(__clang_minor__);
#elif defined(__GNUC__)
  return "gcc-" + std::to_string(__GNUC__) + "." +
         std::to_string(__GNUC_MINOR__);
#elif defined(_MSC_VER)
  return "msvc-" + std::to_string(_MSC_VER);
#else
  return "unknown";
#endif
}

bool BuildCalendarTime(std::time_t now, bool utc, std::tm* out) {
  if (out == nullptr) {
    return false;
  }

#if defined(_WIN32)
  return utc ? gmtime_s(out, &now) == 0 : localtime_s(out, &now) == 0;
#else
  return utc ? gmtime_r(&now, out) != nullptr
             : localtime_r(&now, out) != nullptr;
#endif
}

json::value RunCurrentTimeTool(const json::value& args, std::string* error_out) {
  const json::object* obj = RequireObjectArgs(args, error_out);
  if (obj == nullptr) {
    return nullptr;
  }

  const auto mode = JsonStringField(*obj, "clock").value_or("local");
  const bool utc = mode == "utc";
  if (!utc && mode != "local") {
    if (error_out != nullptr) {
      *error_out = "clock must be either \"local\" or \"utc\"";
    }
    return nullptr;
  }

  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm time_info{};
  if (!BuildCalendarTime(seconds, utc, &time_info)) {
    if (error_out != nullptr) {
      *error_out = "failed to format current time";
    }
    return nullptr;
  }

  std::ostringstream iso;
  iso << std::put_time(&time_info, "%Y-%m-%d %H:%M:%S");
  if (utc) {
    iso << " UTC";
  }

  json::object output;
  output["clock"] = utc ? "utc" : "local";
  output["iso8601_like"] = iso.str();
  output["unix_seconds"] = static_cast<std::int64_t>(seconds);
  output["platform"] = DetectPlatform();
  return output;
}

json::value RunCalculatorTool(const json::value& args, std::string* error_out) {
  const json::object* obj = RequireObjectArgs(args, error_out);
  if (obj == nullptr) {
    return nullptr;
  }

  const auto lhs = JsonNumberField(*obj, "lhs");
  const auto rhs = JsonNumberField(*obj, "rhs");
  const auto op = JsonStringField(*obj, "op");
  if (!lhs || !rhs || !op) {
    if (error_out != nullptr) {
      *error_out = "calculate requires numeric lhs, numeric rhs, and string op";
    }
    return nullptr;
  }

  double value = 0.0;
  if (*op == "add") {
    value = *lhs + *rhs;
  } else if (*op == "subtract") {
    value = *lhs - *rhs;
  } else if (*op == "multiply") {
    value = *lhs * *rhs;
  } else if (*op == "divide") {
    if (std::fabs(*rhs) < 1e-12) {
      if (error_out != nullptr) {
        *error_out = "division by zero";
      }
      return nullptr;
    }
    value = *lhs / *rhs;
  } else if (*op == "power") {
    value = std::pow(*lhs, *rhs);
  } else if (*op == "mod") {
    const auto lhs_int = JsonIntField(*obj, "lhs");
    const auto rhs_int = JsonIntField(*obj, "rhs");
    if (!lhs_int || !rhs_int) {
      if (error_out != nullptr) {
        *error_out = "mod requires integer lhs and rhs";
      }
      return nullptr;
    }
    if (*rhs_int == 0) {
      if (error_out != nullptr) {
        *error_out = "mod by zero";
      }
      return nullptr;
    }

    json::object output;
    output["expression"] =
        std::to_string(*lhs_int) + " % " + std::to_string(*rhs_int);
    output["value"] = *lhs_int % *rhs_int;
    return output;
  } else {
    if (error_out != nullptr) {
      *error_out =
          "op must be one of add, subtract, multiply, divide, power, mod";
    }
    return nullptr;
  }

  json::object output;
  output["lhs"] = *lhs;
  output["rhs"] = *rhs;
  output["op"] = *op;
  output["value"] = value;
  return output;
}

std::mt19937_64& GlobalRng() {
  static std::mt19937_64 engine(std::random_device{}());
  return engine;
}

json::value RunRandomNumberTool(const json::value& args,
                                std::string* error_out) {
  const json::object* obj = RequireObjectArgs(args, error_out);
  if (obj == nullptr) {
    return nullptr;
  }

  const auto min_value = JsonIntField(*obj, "min").value_or(0);
  const auto max_value = JsonIntField(*obj, "max").value_or(100);
  if (min_value > max_value) {
    if (error_out != nullptr) {
      *error_out = "min must be <= max";
    }
    return nullptr;
  }

  std::uniform_int_distribution<std::int64_t> dist(min_value, max_value);
  json::object output;
  output["min"] = min_value;
  output["max"] = max_value;
  output["value"] = dist(GlobalRng());
  return output;
}

std::size_t CountUtf8CodePoints(std::string_view text) {
  std::size_t count = 0;
  for (unsigned char ch : text) {
    if ((ch & 0xC0U) != 0x80U) {
      ++count;
    }
  }
  return count;
}

std::size_t CountWords(std::string_view text) {
  std::size_t count = 0;
  bool in_word = false;
  for (unsigned char ch : text) {
    if (std::isspace(ch) != 0) {
      in_word = false;
      continue;
    }
    if (!in_word) {
      ++count;
      in_word = true;
    }
  }
  return count;
}

json::value RunTextStatsTool(const json::value& args, std::string* error_out) {
  const json::object* obj = RequireObjectArgs(args, error_out);
  if (obj == nullptr) {
    return nullptr;
  }

  const auto text = JsonStringField(*obj, "text");
  if (!text) {
    if (error_out != nullptr) {
      *error_out = "text_stats requires string text";
    }
    return nullptr;
  }

  std::size_t lines = 1;
  bool has_non_ascii = false;
  for (unsigned char ch : *text) {
    if (ch == '\n') {
      ++lines;
    }
    if (ch > 0x7F) {
      has_non_ascii = true;
    }
  }
  if (text->empty()) {
    lines = 0;
  }

  json::object output;
  output["bytes"] = static_cast<std::int64_t>(text->size());
  output["characters"] = static_cast<std::int64_t>(CountUtf8CodePoints(*text));
  output["words"] = static_cast<std::int64_t>(CountWords(*text));
  output["lines"] = static_cast<std::int64_t>(lines);
  output["has_non_ascii"] = has_non_ascii;
  return output;
}

json::value RunSystemInfoTool(const json::value& args, std::string* error_out) {
  const json::object* obj = RequireObjectArgs(args, error_out);
  if (obj == nullptr) {
    return nullptr;
  }

  json::object output;
  output["platform"] = DetectPlatform();
  output["architecture"] = DetectArch();
  output["compiler"] = DetectCompiler();
  output["cpp_standard"] = static_cast<std::int64_t>(__cplusplus);
  output["pointer_width"] = static_cast<std::int64_t>(sizeof(void*) * 8U);
  return output;
}

}  // namespace

ToolRegistry::ToolRegistry() {
  {
    completion::OaiToolDefinition tool;
    tool.name = "get_current_time";
    tool.description = "Return the current local or UTC time from this machine.";
    tool.parameters = {
        {"type", "object"},
        {"properties",
         json::object{{"clock",
                       json::object{{"type", "string"},
                                    {"enum", json::array{"local", "utc"}}}}}},
    };
    entries_.push_back({std::move(tool), RunCurrentTimeTool});
  }

  {
    completion::OaiToolDefinition tool;
    tool.name = "calculate";
    tool.description =
        "Calculate two numbers using add, subtract, multiply, divide, power, or mod.";
    tool.parameters = {
        {"type", "object"},
        {"properties",
         json::object{
             {"lhs", json::object{{"type", "number"}}},
             {"rhs", json::object{{"type", "number"}}},
             {"op",
              json::object{{"type", "string"},
                           {"enum",
                            json::array{"add", "subtract", "multiply",
                                        "divide", "power", "mod"}}}},
         }},
        {"required", json::array{"lhs", "rhs", "op"}},
    };
    entries_.push_back({std::move(tool), RunCalculatorTool});
  }

  {
    completion::OaiToolDefinition tool;
    tool.name = "random_number";
    tool.description =
        "Pick a random integer in a closed interval [min, max].";
    tool.parameters = {
        {"type", "object"},
        {"properties",
         json::object{{"min", json::object{{"type", "integer"}}},
                      {"max", json::object{{"type", "integer"}}}}},
    };
    entries_.push_back({std::move(tool), RunRandomNumberTool});
  }

  {
    completion::OaiToolDefinition tool;
    tool.name = "text_stats";
    tool.description =
        "Count UTF-8 characters, words, lines, and bytes for a piece of text.";
    tool.parameters = {
        {"type", "object"},
        {"properties",
         json::object{{"text", json::object{{"type", "string"}}}}},
        {"required", json::array{"text"}},
    };
    entries_.push_back({std::move(tool), RunTextStatsTool});
  }

  {
    completion::OaiToolDefinition tool;
    tool.name = "system_info";
    tool.description =
        "Describe the current runtime platform, CPU architecture, and compiler.";
    tool.parameters = {
        {"type", "object"},
        {"properties", json::object{}},
    };
    entries_.push_back({std::move(tool), RunSystemInfoTool});
  }
}

std::vector<completion::OaiToolDefinition> ToolRegistry::Definitions() const {
  std::vector<completion::OaiToolDefinition> out;
  out.reserve(entries_.size());
  for (const auto& entry : entries_) {
    out.push_back(entry.definition);
  }
  return out;
}

const ToolEntry* ToolRegistry::Find(std::string_view name) const {
  for (const auto& entry : entries_) {
    if (entry.definition.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

json::object ToolRegistry::Execute(const completion::OaiToolCall& call) const {
  const ToolEntry* entry = Find(call.name);
  if (entry == nullptr) {
    return MakeToolError(
        call.name, call.arguments,
        "unknown tool; ask the assistant to choose one of the declared demo tools");
  }

  const json::value input = call.arguments.is_null() ? json::object{} : call.arguments;
  std::string error;
  json::value output = entry->handler(input, &error);
  if (!error.empty()) {
    return MakeToolError(call.name, input, std::move(error));
  }
  return MakeToolEnvelope(call.name, input, std::move(output));
}

void ToolRegistry::PrintList(std::ostream& out) const {
  out << "Available tools:\n";
  for (const auto& entry : entries_) {
    out << "  - " << entry.definition.name << ": "
        << entry.definition.description << "\n";
  }
}

}  // namespace boai::examples::deepseek_demo
