cmake_minimum_required(VERSION 3.26)

project(poker-epoll LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(project_warnings INTERFACE)
target_compile_features(project_warnings INTERFACE cxx_std_23)

set(THREADS_PREFER_PTHREAD_FLAG ON)


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

add_library(poker_epoll STATIC src/player.cc src/player_manager.cc
                              src/hand_evaluator.cc src/table.cc)
target_include_directories(poker_epoll PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(poker_epoll PUBLIC project_warnings)

include(CTest)
enable_testing()

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(cards_tests tests/cards_tests.cc)
target_link_libraries(cards_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(cards_tests)

add_executable(player_manager_tests tests/player_manager_tests.cc)
target_link_libraries(player_manager_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(player_manager_tests)

add_executable(table_tests tests/table_tests.cc)
target_link_libraries(table_tests PRIVATE poker_epoll GTest::gtest_main Threads::Threads)
gtest_discover_tests(table_tests)
