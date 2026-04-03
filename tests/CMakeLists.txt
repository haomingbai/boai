include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

function(boai_add_test target_name source_file label)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name}
    PRIVATE
      boai
      GTest::gtest_main
      GTest::gmock
  )
  target_include_directories(${target_name}
    PRIVATE
      ${PROJECT_SOURCE_DIR}/tests/support
  )

  add_test(NAME ${target_name} COMMAND ${target_name})
  set_tests_properties(${target_name} PROPERTIES LABELS ${label})
endfunction()

add_subdirectory(general)
