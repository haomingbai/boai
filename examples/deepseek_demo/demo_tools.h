#pragma once

#include <boost/json.hpp>

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "boai/completion/oai_completion.h"

namespace boai::examples::deepseek_demo {

namespace json = boost::json;

struct ToolEntry {
  using Handler = std::function<json::value(const json::value&, std::string*)>;

  boai::completion::OaiToolDefinition definition;
  Handler handler;
};

class ToolRegistry {
 public:
  ToolRegistry();

  [[nodiscard]] std::vector<boai::completion::OaiToolDefinition> Definitions()
      const;

  [[nodiscard]] json::object Execute(
      const boai::completion::OaiToolCall& call) const;

  void PrintList(std::ostream& out) const;

 private:
  [[nodiscard]] const ToolEntry* Find(std::string_view name) const;

  std::vector<ToolEntry> entries_;
};

}  // namespace boai::examples::deepseek_demo
