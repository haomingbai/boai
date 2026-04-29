/**
 * @file oai_completion_detail.cc
 * @brief Internal helpers for OAI completion: URL parsing and connection pipeline.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-30
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#include "oai_completion_detail.h"

#include <memory>
#include <string>

#include "bsrvcore/connection/client/request_assembler.h"
#include "bsrvcore/connection/client/stream_builder.h"

namespace boai::completion::detail {

using bsrvcore::DefaultRequestAssembler;
using bsrvcore::DirectStreamBuilder;
using bsrvcore::ProxyRequestAssembler;
using bsrvcore::ProxyStreamBuilder;
using bsrvcore::RequestAssembler;
using bsrvcore::StreamBuilder;

ParsedUrl ParseUrl(const std::string& url) {
  ParsedUrl result;

  const auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    result.scheme = "https";
    result.host = url;
    result.port = "443";
    result.target = "/";
    return result;
  }

  result.scheme = url.substr(0, scheme_end);
  result.port = (result.scheme == "https") ? "443" : "80";

  const auto host_start = scheme_end + 3;
  const auto path_start = url.find('/', host_start);
  const auto port_sep = url.find(':', host_start);

  const auto host_end =
      (port_sep != std::string::npos) ? port_sep : path_start;

  result.host = url.substr(host_start, host_end - host_start);

  if (port_sep != std::string::npos &&
      (path_start == std::string::npos || port_sep < path_start)) {
    const auto port_end =
        (path_start != std::string::npos) ? path_start : url.size();
    result.port = url.substr(port_sep + 1, port_end - port_sep - 1);
  }

  if (path_start != std::string::npos) {
    result.target = url.substr(path_start);
  } else {
    result.target = "/";
  }

  return result;
}

ConnectionPipeline CreateConnectionPipeline(const OaiCompletionInfo& info,
                                            const std::string& scheme) {
  auto inner_assembler = std::make_shared<DefaultRequestAssembler>();
  std::shared_ptr<StreamBuilder> builder = DirectStreamBuilder::Create();

  std::shared_ptr<RequestAssembler> assembler = inner_assembler;

  if (info.proxy.enabled()) {
    assembler = std::make_shared<ProxyRequestAssembler>(
        std::move(inner_assembler), info.proxy);
    if (scheme == "https") {
      builder = ProxyStreamBuilder::Create(std::move(builder));
    }
  }

  assembler->SetStreamBuilder(builder);

  return {std::move(assembler), std::move(builder)};
}

}  // namespace boai::completion::detail
