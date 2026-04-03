add_executable(example_boai_deepseek_demo
  deepseek_demo.cc
  demo_options.cc
  demo_session.cc
  demo_tools.cc
)
target_link_libraries(example_boai_deepseek_demo PRIVATE boai)

set_target_properties(example_boai_deepseek_demo PROPERTIES
  OUTPUT_NAME deepseek-demo
)
