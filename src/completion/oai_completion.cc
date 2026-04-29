/**
 * @file oai_completion.cc
 * @brief Factory request execution for chat-style OAI completion facade using
 *        bsrvcore's three-stage Assembler/Builder/Task architecture.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-01
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "boai/completion/oai_completion.h"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <chrono>
#include <cstdint>  // NOLINT(misc-include-cleaner): Boost.Beast field.hpp requires std::uint32_t on some toolchains.
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bsrvcore/allocator/allocator.h"
#include "bsrvcore/connection/client/http_client_task.h"
#include "bsrvcore/connection/client/http_sse_client_task.h"
#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/sse_event_parser.h"
#include "bsrvcore/connection/client/stream_builder.h"
#include "bsrvcore/connection/client/stream_slot.h"
#include "oai_completion_detail.h"

namespace boai::completion {

namespace http = boost::beast::http;
using bsrvcore::AllocateShared;
using bsrvcore::DefaultRequestAssembler;
using bsrvcore::DirectStreamBuilder;
using bsrvcore::HttpClientOptions;
using bsrvcore::HttpClientRequest;
using bsrvcore::HttpClientResult;
using bsrvcore::HttpClientTask;
using bsrvcore::HttpSseClientOptions;
using bsrvcore::HttpSseClientResult;
using bsrvcore::HttpSseClientTask;
using bsrvcore::ProxyRequestAssembler;
using bsrvcore::ProxyStreamBuilder;
using bsrvcore::RequestAssembler;
using bsrvcore::SslContextPtr;
using bsrvcore::SseEventParser;
using bsrvcore::StreamBuilder;
using bsrvcore::StreamSlot;

bool OaiCompletionFactory::FetchCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    CompletionCallback cb) const {
  return FetchCompletion(std::move(state), {}, std::move(model_info),
                         std::move(cb));
}

bool OaiCompletionFactory::FetchCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    const std::shared_ptr<OaiModelInfo>& model_info,
    CompletionCallback cb) const {
  if (!state || !cb || !model_info || !info_ || info_->base_url.empty() ||
      model_info->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!detail::BuildRequestPayload(detail::CollectMessageChain(state), tools,
                                   *model_info, false, &request_body,
                                   &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";
    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, false);
    log.error_message = std::move(build_error);
    auto failure_state = AllocateShared<OaiCompletionState>(
        info_, model_info, std::move(assistant), std::move(log),
        std::move(state));
    cb(std::move(failure_state));
    return true;
  }

  auto parsed =
      detail::ParseUrl(detail::BuildCompletionsUrl(info_->base_url));

  auto inner_assembler = std::make_shared<DefaultRequestAssembler>();
  std::shared_ptr<StreamBuilder> builder = DirectStreamBuilder::Create();
  std::shared_ptr<RequestAssembler> assembler = inner_assembler;
  if (info_->proxy.enabled()) {
    assembler = std::make_shared<ProxyRequestAssembler>(
        std::move(inner_assembler), info_->proxy);
    if (parsed.scheme == "https") {
      builder = ProxyStreamBuilder::Create(std::move(builder));
    }
  }
  assembler->SetStreamBuilder(builder);

  HttpClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  const bool is_https = (parsed.scheme == "https");
  auto init_request = HttpClientRequest{http::verb::post, parsed.target, 11};
  auto assembled = assembler->Assemble(
      std::move(init_request), options,
      parsed.scheme, parsed.host, parsed.port,
      is_https ? ssl_ctx_ : SslContextPtr{});

  const std::string conn_host = assembled.connection_key.host;
  const std::string conn_target = assembled.request.target();

  builder->Acquire(
      assembled.connection_key, executor_,
      [this, request = std::move(assembled.request),
       request_body = std::move(request_body), state = std::move(state),
       cb = std::move(cb), info = info_, model_info, options,
       conn_host, conn_target](boost::system::error_code ec,
                               StreamSlot slot) mutable {
        if (ec) {
          OaiMessage assistant;
          assistant.role = "assistant";
          OaiRequestLog log = detail::BuildLogSkeleton(*model_info, false);
          log.error_message = ec.message();
          auto failure_state = AllocateShared<OaiCompletionState>(
              info, model_info, std::move(assistant), std::move(log),
              std::move(state));
          cb(std::move(failure_state));
          return;
        }

        std::shared_ptr<HttpClientTask> task;
        if (slot.ssl_stream) {
          task = HttpClientTask::CreateHttpsRaw(
              executor_, std::move(*slot.ssl_stream), conn_host, conn_target,
              http::verb::post, options);
        } else {
          task = HttpClientTask::CreateHttpRaw(
              executor_, std::move(*slot.tcp_stream), conn_host, conn_target,
              http::verb::post, options);
        }

        task->Request().method(http::verb::post);
        task->Request().target(conn_target);
        task->Request().version(11);
        task->Request().set(http::field::host, conn_host);
        task->Request().set(http::field::content_type, "application/json");
        task->Request().set(http::field::accept, "application/json");
        if (!info->api_key.empty()) {
          task->Request().set(http::field::authorization,
                              "Bearer " + info->api_key);
        }
        if (info->organization.has_value()) {
          task->Request().set("OpenAI-Organization", *info->organization);
        }
        if (info->project.has_value()) {
          task->Request().set("OpenAI-Project", *info->project);
        }
        task->Request().body() = std::move(request_body);
        task->Request().prepare_payload();

        task->OnDone([state = std::move(state), cb = std::move(cb),
                      info = std::move(info),
                      model_info](const HttpClientResult& result) mutable {
          OaiMessage assistant;
          assistant.role = "assistant";

          OaiRequestLog log = detail::BuildLogSkeleton(*model_info, false);
          log.http_status_code = result.response.result_int();

          const auto request_id_header =
              result.response.base().find("x-request-id");
          if (request_id_header != result.response.base().end()) {
            log.request_id = std::string(request_id_header->value().data(),
                                         request_id_header->value().size());
          }

          if (result.ec) {
            log.error_message = result.ec.message();
            auto failure_state = AllocateShared<OaiCompletionState>(
                info, model_info, std::move(assistant), std::move(log),
                std::move(state));
            cb(std::move(failure_state));
            return;
          }

          if (result.cancelled) {
            log.error_message = "request cancelled";
            auto failure_state = AllocateShared<OaiCompletionState>(
                info, model_info, std::move(assistant), std::move(log),
                std::move(state));
            cb(std::move(failure_state));
            return;
          }

          std::string parse_error;
          if (!detail::IsHttpSuccessStatus(log.http_status_code)) {
            parse_error = detail::ExtractErrorMessageFromJsonBody(
                result.response.body());
            log.error_message =
                parse_error.empty()
                    ? ("HTTP status " + std::to_string(log.http_status_code))
                    : std::move(parse_error);
            auto failure_state = AllocateShared<OaiCompletionState>(
                info, model_info, std::move(assistant), std::move(log),
                std::move(state));
            cb(std::move(failure_state));
            return;
          }

          if (!detail::ParseCompletionResponseBody(
                  result.response.body(), &assistant, &log, &parse_error)) {
            log.error_message = std::move(parse_error);
            auto failure_state = AllocateShared<OaiCompletionState>(
                info, model_info, std::move(assistant), std::move(log),
                std::move(state));
            cb(std::move(failure_state));
            return;
          }

          log.status = OaiCompletionStatus::kSuccess;
          auto next_state = AllocateShared<OaiCompletionState>(
              info, model_info, std::move(assistant), std::move(log),
              std::move(state));
          cb(std::move(next_state));
        });

        task->Start();
      });

  return true;
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta) const {
  return FetchStreamCompletion(
      std::move(state), std::vector<OaiToolDefinition>{}, std::move(model_info),
      std::move(on_done), std::move(on_delta), StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, std::shared_ptr<OaiModelInfo> model_info,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  return FetchStreamCompletion(
      std::move(state), std::vector<OaiToolDefinition>{}, std::move(model_info),
      std::move(on_done), std::move(on_delta), std::move(on_reasoning_delta));
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    std::shared_ptr<OaiModelInfo> model_info, StreamDoneCallback on_done,
    StreamDeltaCallback on_delta) const {
  return FetchStreamCompletion(std::move(state), tools, std::move(model_info),
                               std::move(on_done), std::move(on_delta),
                               StreamDeltaCallback{});
}

bool OaiCompletionFactory::FetchStreamCompletion(
    StatePtr state, const std::vector<OaiToolDefinition>& tools,
    const std::shared_ptr<OaiModelInfo>& model_info,
    StreamDoneCallback on_done, StreamDeltaCallback on_delta,
    StreamDeltaCallback on_reasoning_delta) const {
  if (!state || !on_done || (!on_delta && !on_reasoning_delta) || !model_info ||
      !info_ || info_->base_url.empty() || model_info->model.empty()) {
    return false;
  }

  std::string request_body;
  std::string build_error;
  if (!detail::BuildRequestPayload(detail::CollectMessageChain(state), tools,
                                   *model_info, true, &request_body,
                                   &build_error)) {
    OaiMessage assistant;
    assistant.role = "assistant";
    OaiRequestLog log = detail::BuildLogSkeleton(*model_info, true);
    log.error_message = std::move(build_error);
    auto failure_state = AllocateShared<OaiCompletionState>(
        info_, model_info, std::move(assistant), std::move(log),
        std::move(state));
    on_done(std::move(failure_state));
    return true;
  }

  auto parsed =
      detail::ParseUrl(detail::BuildCompletionsUrl(info_->base_url));

  auto inner_assembler = std::make_shared<DefaultRequestAssembler>();
  std::shared_ptr<StreamBuilder> builder = DirectStreamBuilder::Create();
  std::shared_ptr<RequestAssembler> assembler = inner_assembler;
  if (info_->proxy.enabled()) {
    assembler = std::make_shared<ProxyRequestAssembler>(
        std::move(inner_assembler), info_->proxy);
    if (parsed.scheme == "https") {
      builder = ProxyStreamBuilder::Create(std::move(builder));
    }
  }
  assembler->SetStreamBuilder(builder);

  HttpSseClientOptions options;
  options.connect_timeout = std::chrono::seconds(10);
  options.read_header_timeout = std::chrono::seconds(10);
  options.read_body_timeout = std::chrono::seconds(60);

  const bool is_https = (parsed.scheme == "https");
  auto init_request = HttpClientRequest{http::verb::post, parsed.target, 11};
  auto assembled = assembler->Assemble(
      std::move(init_request), options,
      parsed.scheme, parsed.host, parsed.port,
      is_https ? ssl_ctx_ : SslContextPtr{});

  const std::string conn_host = assembled.connection_key.host;
  const std::string conn_target = assembled.request.target();

  auto parser = AllocateShared<SseEventParser>();
  auto agg = AllocateShared<detail::StreamAggregate>();

  std::shared_ptr<StreamDeltaCallback> delta_cb;
  if (on_delta) {
    delta_cb = AllocateShared<StreamDeltaCallback>(std::move(on_delta));
  }

  std::shared_ptr<StreamDeltaCallback> reasoning_delta_cb;
  if (on_reasoning_delta) {
    reasoning_delta_cb =
        AllocateShared<StreamDeltaCallback>(std::move(on_reasoning_delta));
  }

  auto finish = AllocateShared<std::function<void(bool, std::string)>>();

  builder->Acquire(
      assembled.connection_key, executor_,
      [this, request = std::move(assembled.request),
       request_body = std::move(request_body), state = std::move(state),
       on_done = std::move(on_done), info = info_, model_info, options,
       conn_host, conn_target, parser, agg, delta_cb, reasoning_delta_cb,
       finish](boost::system::error_code ec, StreamSlot slot) mutable {
        if (ec) {
          OaiMessage assistant;
          assistant.role = "assistant";
          OaiRequestLog log = detail::BuildLogSkeleton(*model_info, true);
          log.error_message = ec.message();
          auto failure_state = AllocateShared<OaiCompletionState>(
              info, model_info, std::move(assistant), std::move(log),
              std::move(state));
          on_done(std::move(failure_state));
          return;
        }

        std::shared_ptr<HttpSseClientTask> client;
        if (slot.ssl_stream) {
          client = HttpSseClientTask::CreateHttpsRaw(
              executor_, std::move(*slot.ssl_stream), conn_host, conn_target,
              options);
        } else {
          client = HttpSseClientTask::CreateHttpRaw(
              executor_, std::move(*slot.tcp_stream), conn_host, conn_target,
              options);
        }

        client->Request() = std::move(request);
        client->Request().method(http::verb::post);
        client->Request().set(http::field::content_type, "application/json");
        client->Request().set(http::field::accept, "text/event-stream");
        if (!info->api_key.empty()) {
          client->Request().set(http::field::authorization,
                                "Bearer " + info->api_key);
        }
        if (info->organization.has_value()) {
          client->Request().set("OpenAI-Organization", *info->organization);
        }
        if (info->project.has_value()) {
          client->Request().set("OpenAI-Project", *info->project);
        }
        client->Request().body() = std::move(request_body);
        client->Request().prepare_payload();

        *finish = [agg, on_done = std::move(on_done), info = std::move(info),
                   model_info, state = std::move(state)](
                      bool success, std::string error_message) mutable {
          if (agg->done.exchange(true)) {
            return;
          }

          if (!error_message.empty()) {
            agg->error_message = std::move(error_message);
          }

          OaiMessage assistant;
          assistant.role = "assistant";
          assistant.message = agg->accumulated_message;
          assistant.tool_calls =
              detail::BuildToolCallsFromAccumulation(agg->tool_calls);
          assistant.reasoning = agg->accumulated_reasoning;

          OaiRequestLog log = detail::BuildLogSkeleton(*model_info, true);
          log.status = success ? OaiCompletionStatus::kSuccess
                               : OaiCompletionStatus::kFail;
          log.http_status_code = agg->http_status_code;
          log.error_message = agg->error_message;
          log.request_id = agg->request_id;
          log.finish_reason = agg->finish_reason;
          if (!agg->model.empty()) {
            log.model = agg->model;
          }
          log.delta_count = agg->delta_count;

          auto done_state = AllocateShared<OaiCompletionState>(
              info, model_info, std::move(assistant), std::move(log),
              std::move(state));
          on_done(std::move(done_state));
        };

        client->Start([agg, finish, client, parser, delta_cb,
                       reasoning_delta_cb](const HttpSseClientResult& result) {
          agg->http_status_code = result.header.result_int();

          if (result.cancelled) {
            (*finish)(false, "stream cancelled");
            return;
          }

          if (result.ec) {
            (*finish)(false, result.ec.message());
            return;
          }

          if (!detail::IsHttpSuccessStatus(agg->http_status_code)) {
            (*finish)(false,
                      "HTTP status " + std::to_string(agg->http_status_code));
            return;
          }

          detail::PullNextStreamChunk(client, parser, agg, finish, delta_cb,
                                      reasoning_delta_cb);
        });
      });

  return true;
}

}  // namespace boai::completion
