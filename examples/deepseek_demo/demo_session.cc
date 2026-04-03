#include "demo_session.h"

#include <bsrvcore/allocator/allocator.h>

#include <future>
#include <iostream>
#include <string>
#include <utility>

namespace boai::examples::deepseek_demo {

namespace completion = boai::completion;

namespace {

namespace json = boost::json;
using StatePtr = completion::OaiCompletionFactory::StatePtr;

void PrintReasoning(const std::string& reasoning) {
  if (reasoning.empty()) {
    return;
  }
  std::cerr << "reasoning:\n" << reasoning << "\n";
}

void PrintLog(int round, const completion::OaiRequestLog& log) {
  std::cerr << "round=" << round << " status="
            << (log.status == completion::OaiCompletionStatus::kSuccess
                    ? "success"
                    : "fail")
            << " http=" << log.http_status_code;
  if (!log.request_id.empty()) {
    std::cerr << " request_id=" << log.request_id;
  }
  if (!log.finish_reason.empty()) {
    std::cerr << " finish_reason=" << log.finish_reason;
  }
  if (!log.error_message.empty()) {
    std::cerr << " error=\"" << log.error_message << "\"";
  }
  if (log.is_stream) {
    std::cerr << " deltas=" << log.delta_count;
  }
  std::cerr << "\n";
}

StatePtr BuildInitialState(const completion::OaiCompletionFactory& factory,
                           const Options& options) {
  StatePtr state = nullptr;
  if (!options.system.empty()) {
    completion::OaiMessage system;
    system.role = "system";
    system.message = options.system;
    state = factory.AppendMessage(system, state);
  }
  return state;
}

StatePtr WaitForCompletion(
    boost::asio::io_context& ioc,
    const completion::OaiCompletionFactory& factory, StatePtr state,
    const std::vector<completion::OaiToolDefinition>& tools,
    const std::shared_ptr<completion::OaiModelInfo>& model_info) {
  std::promise<StatePtr> promise;
  auto future = promise.get_future();
  bool fulfilled = false;

  const bool started = factory.FetchCompletion(
      std::move(state), tools, model_info,
      [&promise, &fulfilled](StatePtr done_state) mutable {
        if (fulfilled) {
          return;
        }
        fulfilled = true;
        promise.set_value(std::move(done_state));
      });
  if (!started) {
    return nullptr;
  }

  ioc.restart();
  ioc.run();
  return future.get();
}

StatePtr WaitForStreamCompletion(
    boost::asio::io_context& ioc,
    const completion::OaiCompletionFactory& factory, StatePtr state,
    const std::vector<completion::OaiToolDefinition>& tools,
    const std::shared_ptr<completion::OaiModelInfo>& model_info,
    bool print_reasoning) {
  std::promise<StatePtr> promise;
  auto future = promise.get_future();
  bool fulfilled = false;
  auto saw_content = bsrvcore::AllocateShared<bool>(false);
  auto saw_reasoning = bsrvcore::AllocateShared<bool>(false);

  auto on_done = [&promise, &fulfilled](StatePtr done_state) mutable {
    if (fulfilled) {
      return;
    }
    fulfilled = true;
    promise.set_value(std::move(done_state));
  };

  completion::OaiCompletionFactory::StreamDeltaCallback on_delta =
      [saw_content](const std::string& delta) {
        if (!delta.empty()) {
          *saw_content = true;
          std::cout << delta << std::flush;
        }
      };

  completion::OaiCompletionFactory::StreamDeltaCallback on_reasoning_delta;
  if (print_reasoning) {
    on_reasoning_delta = [saw_reasoning](const std::string& delta) {
      if (delta.empty()) {
        return;
      }
      if (!*saw_reasoning) {
        *saw_reasoning = true;
        std::cerr << "reasoning:\n";
      }
      std::cerr << delta << std::flush;
    };
  }

  const bool started =
      on_reasoning_delta
          ? factory.FetchStreamCompletion(std::move(state), tools, model_info,
                                          std::move(on_done),
                                          std::move(on_delta),
                                          std::move(on_reasoning_delta))
          : factory.FetchStreamCompletion(std::move(state), tools, model_info,
                                          std::move(on_done),
                                          std::move(on_delta));
  if (!started) {
    return nullptr;
  }

  ioc.restart();
  ioc.run();
  auto done = future.get();
  if (*saw_content) {
    std::cout << "\n";
  }
  if (*saw_reasoning) {
    std::cerr << "\n";
  } else if (print_reasoning && done != nullptr &&
             !done->GetMessage().reasoning.empty()) {
    PrintReasoning(done->GetMessage().reasoning);
  }
  return done;
}

bool AppendToolResults(const completion::OaiCompletionFactory& factory,
                       const ToolRegistry& tool_registry, StatePtr* state_out) {
  if (state_out == nullptr || *state_out == nullptr) {
    return false;
  }

  const auto& tool_calls = (*state_out)->GetMessage().tool_calls;
  for (std::size_t i = 0; i < tool_calls.size(); ++i) {
    const auto& call = tool_calls[i];
    std::cerr << "tool_call[" << (i + 1) << "] name=" << call.name;
    if (!call.id.empty()) {
      std::cerr << " id=" << call.id;
    }
    if (!call.arguments.is_null()) {
      std::cerr << " args=" << json::serialize(call.arguments);
    }
    std::cerr << "\n";

    json::object result = tool_registry.Execute(call);
    std::cerr << "tool_result[" << (i + 1)
              << "]=" << json::serialize(result) << "\n";

    completion::OaiMessage tool_message;
    tool_message.role = "tool";
    tool_message.message = json::serialize(result);
    tool_message.tool_call_id = call.id;
    *state_out = factory.AppendMessage(tool_message, *state_out);
  }

  return true;
}

StatePtr RunAssistantTurn(
    boost::asio::io_context& ioc,
    const completion::OaiCompletionFactory& factory,
    const ToolRegistry& tool_registry,
    const std::shared_ptr<completion::OaiModelInfo>& model_info,
    const Options& options, StatePtr state) {
  const auto tools = tool_registry.Definitions();

  for (int round = 1; round <= options.max_round_trips; ++round) {
    StatePtr done = options.stream
                        ? WaitForStreamCompletion(ioc, factory, state, tools,
                                                  model_info,
                                                  options.print_reasoning)
                        : WaitForCompletion(ioc, factory, state, tools,
                                            model_info);
    if (done == nullptr) {
      std::cerr << "failed to start completion request\n";
      return nullptr;
    }

    PrintLog(round, done->GetLog());
    if (!options.stream && options.print_reasoning) {
      PrintReasoning(done->GetMessage().reasoning);
    }

    if (done->GetLog().status != completion::OaiCompletionStatus::kSuccess) {
      return done;
    }

    if (done->GetMessage().tool_calls.empty()) {
      if (!options.stream) {
        std::cout << done->GetMessage().message << "\n";
      }
      return done;
    }

    state = done;
    if (!AppendToolResults(factory, tool_registry, &state)) {
      std::cerr << "failed to append tool results\n";
      return nullptr;
    }
  }

  std::cerr << "exceeded max_round_trips without a final assistant answer\n";
  return nullptr;
}

}  // namespace

int RunInteractive(boost::asio::io_context& ioc,
                   const completion::OaiCompletionFactory& factory,
                   const ToolRegistry& tool_registry,
                   const std::shared_ptr<completion::OaiModelInfo>& model_info,
                   const Options& options) {
  StatePtr initial_state = BuildInitialState(factory, options);
  StatePtr state = initial_state;

  std::cerr << "deepseek-demo interactive mode. type :tools, :reset or :quit\n";
  std::string line;
  while (true) {
    std::cout << "you> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      return 0;
    }

    if (line == ":quit" || line == ":exit") {
      return 0;
    }
    if (line == ":tools") {
      tool_registry.PrintList(std::cout);
      continue;
    }
    if (line == ":reset") {
      state = initial_state;
      std::cerr << "conversation reset\n";
      continue;
    }
    if (line.empty()) {
      continue;
    }

    completion::OaiMessage user;
    user.role = "user";
    user.message = line;
    state = factory.AppendMessage(user, state);

    std::cout << "assistant> " << std::flush;
    StatePtr done =
        RunAssistantTurn(ioc, factory, tool_registry, model_info, options, state);
    if (done == nullptr) {
      return 1;
    }
    if (done->GetLog().status != completion::OaiCompletionStatus::kSuccess) {
      return 1;
    }
    state = done;
  }
}

int RunOneShot(boost::asio::io_context& ioc,
               const completion::OaiCompletionFactory& factory,
               const ToolRegistry& tool_registry,
               const std::shared_ptr<completion::OaiModelInfo>& model_info,
               const Options& options) {
  StatePtr state = BuildInitialState(factory, options);

  completion::OaiMessage user;
  user.role = "user";
  user.message = options.user;
  state = factory.AppendMessage(user, state);

  StatePtr done =
      RunAssistantTurn(ioc, factory, tool_registry, model_info, options, state);
  if (done == nullptr) {
    return 1;
  }
  return done->GetLog().status == completion::OaiCompletionStatus::kSuccess ? 0
                                                                             : 1;
}

}  // namespace boai::examples::deepseek_demo
