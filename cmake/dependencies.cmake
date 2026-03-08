include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# Protobuf via git (provides protoc + libprotobuf targets).
FetchContent_Declare(
  protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v31.1
  GIT_PROGRESS TRUE
  GIT_SHALLOW TRUE
  USES_TERMINAL_DOWNLOAD TRUE
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)
include("${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake")

# spdlog via git, pinned to current submodule commit.
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
  GIT_PROGRESS TRUE
  GIT_SHALLOW TRUE
  USES_TERMINAL_DOWNLOAD TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# PokerHandEvaluator via git, pinned to current submodule commit.
FetchContent_Declare(
  pheval
  GIT_REPOSITORY https://github.com/HenryRLee/PokerHandEvaluator.git
  GIT_TAG v0.5.3.1
  SOURCE_SUBDIR cpp
  GIT_PROGRESS TRUE
  GIT_SHALLOW TRUE
  USES_TERMINAL_DOWNLOAD TRUE
)
set(BUILD_CARD5 OFF CACHE BOOL "" FORCE)
set(BUILD_CARD6 OFF CACHE BOOL "" FORCE)
set(BUILD_CARD7 OFF CACHE BOOL "" FORCE)
set(BUILD_PLO4 OFF CACHE BOOL "" FORCE)
set(BUILD_PLO5 OFF CACHE BOOL "" FORCE)
set(BUILD_PLO6 OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(pheval)
