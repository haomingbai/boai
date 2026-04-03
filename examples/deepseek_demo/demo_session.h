#pragma once

#include <boost/asio/io_context.hpp>

#include <memory>

#include "boai/completion/oai_completion.h"
#include "demo_options.h"
#include "demo_tools.h"

namespace boai::examples::deepseek_demo {

int RunInteractive(
    boost::asio::io_context& ioc,
    const boai::completion::OaiCompletionFactory& factory,
    const ToolRegistry& tool_registry,
    const std::shared_ptr<boai::completion::OaiModelInfo>& model_info,
    const Options& options);

int RunOneShot(boost::asio::io_context& ioc,
               const boai::completion::OaiCompletionFactory& factory,
               const ToolRegistry& tool_registry,
               const std::shared_ptr<boai::completion::OaiModelInfo>& model_info,
               const Options& options);

}  // namespace boai::examples::deepseek_demo
