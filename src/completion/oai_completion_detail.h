/**
 * @file oai_completion_detail.h
 * @brief Internal helpers for OAI completion implementation.
 */

#pragma once

#ifndef BOAI_SRC_COMPLETION_OAI_COMPLETION_DETAIL_H_
#define BOAI_SRC_COMPLETION_OAI_COMPLETION_DETAIL_H_

#include <atomic>
#include <boost/beast/http.hpp>
#include <cstddef>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast http headers require std::uint32_t on some toolchains.
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "boai/completion/oai_completion.h"

namespace boai::completion::detail {

using bsrvcore::HttpSseClientTask;
using bsrvcore::SseEventParser;

struct ParsedUrl {
  std::string scheme;
  std::string host;
  std::string port;
  std::string target;
};

struct ConnectionPipeline {
  std::shared_ptr<bsrvcore::RequestAssembler> assembler;
  std::shared_ptr<bsrvcore::StreamBuilder> builder;
};

ParsedUrl ParseUrl(const std::string& url);
ConnectionPipeline CreateConnectionPipeline(const OaiCompletionInfo& info,
                                            const std::string& scheme);

struct ToolCallAccumulator {
  std::string id;
  std::string name;
  std::string arguments_text;
};

struct StreamAggregate {
  std::atomic<bool> done{false};
  int http_status_code{0};
  std::size_t delta_count{0};
  std::string accumulated_message;
  std::string accumulated_reasoning;
  std::map<std::size_t, ToolCallAccumulator> tool_calls;
  std::string request_id;
  std::string finish_reason;
  std::string model;
  std::string error_message;
};

std::string BuildCompletionsUrl(std::string base_url);
bool IsHttpSuccessStatus(int status);

std::vector<OaiMessage> CollectMessageChain(
    const std::shared_ptr<OaiCompletionState>& state);

bool BuildRequestPayload(const std::vector<OaiMessage>& messages,
                         const std::vector<OaiToolDefinition>& tools,
                         const OaiModelInfo& model_info, bool stream,
                         std::string* body_out, std::string* error_out);

std::string ExtractErrorMessageFromJsonBody(const std::string& body);

bool ParseCompletionResponseBody(const std::string& body,
                                 OaiMessage* message_out, OaiRequestLog* log,
                                 std::string* error_out);

std::vector<OaiToolCall> BuildToolCallsFromAccumulation(
    const std::map<std::size_t, ToolCallAccumulator>& tool_calls);

OaiRequestLog BuildLogSkeleton(const OaiModelInfo& model_info, bool is_stream);

void PullNextStreamChunk(
    const std::shared_ptr<HttpSseClientTask>& client,
    const std::shared_ptr<SseEventParser>& parser,
    const std::shared_ptr<StreamAggregate>& agg,
    const std::shared_ptr<std::function<void(bool, std::string)>>& finish,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>& on_delta,
    const std::shared_ptr<OaiCompletionFactory::StreamDeltaCallback>&
        on_reasoning_delta);

}  // namespace boai::completion::detail

#endif  // BOAI_SRC_COMPLETION_OAI_COMPLETION_DETAIL_H_
