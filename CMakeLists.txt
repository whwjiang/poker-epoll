cmake_minimum_required(VERSION 3.26)

project(poker-epoll LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fno-omit-frame-pointer")

include(${PROJECT_SOURCE_DIR}/cmake/dependencies.cmake)

add_library(project_warnings INTERFACE)
target_compile_features(project_warnings INTERFACE cxx_std_23)

set(THREADS_PREFER_PTHREAD_FLAG ON)

set(PROTO_FILES
  ${PROJECT_SOURCE_DIR}/proto/poker/v1/actions.proto
  ${PROJECT_SOURCE_DIR}/proto/poker/v1/cards.proto
  ${PROJECT_SOURCE_DIR}/proto/poker/v1/errors.proto
  ${PROJECT_SOURCE_DIR}/proto/poker/v1/events.proto
  ${PROJECT_SOURCE_DIR}/proto/poker/v1/response.proto
)

set(PROTOBUF_IMPORT_DIRS ${PROJECT_SOURCE_DIR}/proto)
protobuf_generate(
  LANGUAGE cpp
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
  OUT_VAR PROTO_GEN_CPP
  PROTOS ${PROTO_FILES}
  IMPORT_DIRS ${PROTOBUF_IMPORT_DIRS}
  APPEND_PATH
)
protobuf_generate(
  LANGUAGE python
  PROTOC_OUT_DIR ${PROJECT_SOURCE_DIR}/client
  OUT_VAR PROTO_PY
  PROTOS ${PROTO_FILES}
  IMPORT_DIRS ${PROTOBUF_IMPORT_DIRS}
  APPEND_PATH
)
add_custom_target(poker_proto_python DEPENDS ${PROTO_PY})

add_library(poker_proto STATIC ${PROTO_GEN_CPP})
target_link_libraries(poker_proto PUBLIC protobuf::libprotobuf)
target_include_directories(poker_proto
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/proto
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Werror
    -Weverything
    -pthread
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
  )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Werror
    -pthread
  )
elseif(MSVC)
  target_compile_options(project_warnings INTERFACE
    /W4
    /WX
  )
endif()

add_library(poker_epoll STATIC engine/src/player.cc engine/src/player_manager.cc
                              engine/src/hand_evaluator.cc engine/src/table.cc
                              engine/src/proto_translate.cc)
target_include_directories(poker_epoll PUBLIC ${PROJECT_SOURCE_DIR}/engine/src)
target_link_libraries(poker_epoll PUBLIC project_warnings poker_proto)
target_link_libraries(poker_epoll PUBLIC pheval)

include(CTest)
enable_testing()

add_executable(poker_server engine/src/main.cc engine/src/server.cc
                            engine/src/reactor_epoll.cc)
target_link_libraries(poker_server PRIVATE project_warnings poker_proto poker_epoll spdlog::spdlog)

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(cards_tests engine/tests/cards_tests.cc)
target_link_libraries(cards_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(cards_tests)

add_executable(player_manager_tests engine/tests/player_manager_tests.cc)
target_link_libraries(player_manager_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(player_manager_tests)

add_executable(table_tests engine/tests/table_tests.cc)
target_link_libraries(table_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(table_tests)
