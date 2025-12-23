cmake_minimum_required(VERSION 3.26)

project(poker-epoll LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(project_warnings INTERFACE)
target_compile_features(project_warnings INTERFACE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Werror
    -Weverything
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
  )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(project_warnings INTERFACE
    -Wall
    -Wextra
    -Werror
  )
elseif(MSVC)
  target_compile_options(project_warnings INTERFACE
    /W4
    /WX
  )
endif()

add_library(poker_epoll INTERFACE)
target_include_directories(poker_epoll INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(poker_epoll INTERFACE project_warnings)
