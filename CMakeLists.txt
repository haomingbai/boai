cmake_minimum_required(VERSION 3.25)
project(boai VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  if(APPLE)
    cmake_policy(SET CMP0167 OLD)
  else()
    cmake_policy(SET CMP0167 NEW)
  endif()
endif()

if(NOT DEFINED BUILD_SHARED_LIBS)
  if(WIN32)
    option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
  else()
    option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  endif()
endif()

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BOAI_BUILD_EXAMPLES "Build boai examples. Default: ON" ON)
option(BOAI_BUILD_TESTS "Build boai tests. Default: OFF" OFF)
option(BOAI_FETCH_BSRVCORE
  "Fetch bsrvcore from the network when no local package or source tree is supplied."
  ON
)

set(BOAI_BSRVCORE_CMAKE_DIR "" CACHE PATH
  "Directory containing bsrvcoreConfig.cmake for a local bsrvcore build or install."
)
set(BOAI_BSRVCORE_SOURCE_DIR "" CACHE PATH
  "Path to a local bsrvcore source tree to add as a subdirectory."
)
set(BOAI_BSRVCORE_GIT_REPOSITORY "https://github.com/haomingbai/bsrvcore.git"
  CACHE STRING "Git repository used when boai fetches bsrvcore."
)
set(BOAI_BSRVCORE_GIT_TAG "v0.13.0"
  CACHE STRING "Git ref used when boai fetches bsrvcore."
)

set(BOAI_BSRVCORE_RESOLUTION_MODE "preexisting")

if(NOT TARGET bsrvcore::bsrvcore)
  if(BOAI_BSRVCORE_CMAKE_DIR)
    find_package(bsrvcore CONFIG QUIET
      PATHS "${BOAI_BSRVCORE_CMAKE_DIR}"
      NO_DEFAULT_PATH
    )
    if(TARGET bsrvcore::bsrvcore)
      set(BOAI_BSRVCORE_RESOLUTION_MODE
          "config:${BOAI_BSRVCORE_CMAKE_DIR}")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore)
    find_package(bsrvcore CONFIG QUIET)
    if(TARGET bsrvcore::bsrvcore)
      set(BOAI_BSRVCORE_RESOLUTION_MODE "config:find_package")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore AND BOAI_BSRVCORE_SOURCE_DIR)
    if(NOT EXISTS "${BOAI_BSRVCORE_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "BOAI_BSRVCORE_SOURCE_DIR does not point to a bsrvcore source tree: "
        "${BOAI_BSRVCORE_SOURCE_DIR}"
      )
    endif()

    set(BSRVCORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    add_subdirectory(
      "${BOAI_BSRVCORE_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/bsrvcore-build"
    )
    if(TARGET bsrvcore::bsrvcore)
      set(BOAI_BSRVCORE_RESOLUTION_MODE
          "source:${BOAI_BSRVCORE_SOURCE_DIR}")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore AND BOAI_FETCH_BSRVCORE)
    include(FetchContent)

    set(BSRVCORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
      bsrvcore
      GIT_REPOSITORY "${BOAI_BSRVCORE_GIT_REPOSITORY}"
      GIT_TAG "${BOAI_BSRVCORE_GIT_TAG}"
    )
    FetchContent_MakeAvailable(bsrvcore)
    if(TARGET bsrvcore::bsrvcore)
      set(BOAI_BSRVCORE_RESOLUTION_MODE
          "fetch:${BOAI_BSRVCORE_GIT_REPOSITORY}@${BOAI_BSRVCORE_GIT_TAG}")
    endif()
  endif()
endif()

if(NOT TARGET bsrvcore::bsrvcore)
  message(FATAL_ERROR
    "Unable to resolve bsrvcore. Tried local CMake package discovery, "
    "BOAI_BSRVCORE_SOURCE_DIR, and optional FetchContent."
  )
endif()

message(STATUS "boai resolved bsrvcore via ${BOAI_BSRVCORE_RESOLUTION_MODE}")

add_library(boai
  ${CMAKE_CURRENT_SOURCE_DIR}/src/completion/oai_completion.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/completion/oai_completion_request.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/completion/oai_completion_response.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/completion/oai_completion_factory.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/completion/oai_completion_state.cc
)
add_library(boai::boai ALIAS boai)

target_compile_features(boai PUBLIC cxx_std_20)
target_link_libraries(boai
  PUBLIC
    $<BUILD_INTERFACE:bsrvcore::bsrvcore>
    $<INSTALL_INTERFACE:bsrvcore::bsrvcore>
)
target_include_directories(boai
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(boai PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME boai
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(NOT WIN32)
  set_target_properties(boai PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(BOAI_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
  add_subdirectory(examples)
endif()

if(BOAI_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  enable_testing()
  add_subdirectory(tests)
endif()

set(BOAI_INSTALL_CONFIGDIR "${CMAKE_INSTALL_LIBDIR}/cmake/boai")

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/boaiConfig.cmake.in
  ${PROJECT_BINARY_DIR}/boaiConfig.cmake
  INSTALL_DESTINATION ${BOAI_INSTALL_CONFIGDIR}
)

write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/boaiConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(TARGETS boai
  EXPORT boaiTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h*"
)

install(EXPORT boaiTargets
  FILE boaiTargets.cmake
  NAMESPACE boai::
  DESTINATION ${BOAI_INSTALL_CONFIGDIR}
)

install(FILES
  ${PROJECT_BINARY_DIR}/boaiConfig.cmake
  ${PROJECT_BINARY_DIR}/boaiConfigVersion.cmake
  DESTINATION ${BOAI_INSTALL_CONFIGDIR}
)

if(BOAI_BSRVCORE_RESOLUTION_MODE MATCHES "^config:")
  export(EXPORT boaiTargets
    FILE "${PROJECT_BINARY_DIR}/boaiTargets.cmake"
    NAMESPACE boai::
  )
else()
  message(STATUS
    "boai skipped build-tree export because bsrvcore came from "
    "${BOAI_BSRVCORE_RESOLUTION_MODE}"
  )
endif()
