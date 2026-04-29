#include <bsrvcore/allocator/allocator.h>

#include <boost/asio/io_context.hpp>

#include <iostream>
#include <string>

#include "boai/completion/oai_completion.h"
#include "demo_options.h"
#include "demo_session.h"
#include "demo_tools.h"

int main(int argc, char** argv) {
  namespace demo = boai::examples::deepseek_demo;
  namespace completion = boai::completion;

  demo::Options options;
  std::string error;
  if (!demo::ParseArgs(argc, argv, &options, &error)) {
    if (!error.empty()) {
      std::cerr << "Error: " << error << "\n\n";
    }
    demo::PrintUsage(argv[0]);
    return error.empty() ? 0 : 2;
  }

  demo::ToolRegistry tool_registry;
  if (options.list_tools) {
    tool_registry.PrintList(std::cout);
    return 0;
  }

  boost::asio::io_context ioc;

  auto info = bsrvcore::AllocateShared<completion::OaiCompletionInfo>();
  info->base_url = options.base_url;
  info->api_key = options.api_key;
  if (!options.organization.empty()) {
    info->organization = options.organization;
  }
  if (!options.project.empty()) {
    info->project = options.project;
  }
  if (!options.proxy_host.empty()) {
    info->proxy.host = options.proxy_host;
    info->proxy.port = options.proxy_port;
    info->proxy.auth = options.proxy_auth;
  }

  auto model_info = bsrvcore::AllocateShared<completion::OaiModelInfo>();
  model_info->model = options.model;
  model_info->params["tool_choice"] = "auto";
  if (options.has_temperature) {
    model_info->params["temperature"] = options.temperature;
  }

  completion::OaiCompletionFactory const factory(ioc.get_executor(), info);

  return options.interactive
             ? demo::RunInteractive(ioc, factory, tool_registry, model_info,
                                    options)
             : demo::RunOneShot(ioc, factory, tool_registry, model_info,
                                options);
}
