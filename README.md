# poker-epoll

Learning async reactors and modern C++.

## Prerequisites

- CMake `>= 3.26`
- A C++23 compiler (`clang++` or `g++`)
- `git` (CMake `FetchContent` pulls dependencies from GitHub)
- `pthread` support (standard on Linux; not actually used but required for build)

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j 4
```

Notes:

- If `CMAKE_BUILD_TYPE` is not set, this project defaults to `Debug`.
- First configure/build may take longer because dependencies are downloaded.

## Run Tests

Run all discovered tests:

```bash
ctest --test-dir build --output-on-failure
```

Run tests matching a pattern:

```bash
ctest --test-dir build -R table --output-on-failure
```

Run a single test binary directly:

```bash
./build/cards_tests
```

## Next Steps

- Add action timeouts on the server side (support exists via table)
- Fuzz test inputs
- Factor out `epoll` code into some sort of small library
- Support `io_uring` as a drop-in replacement for `epoll`
- Performance test `epoll` against `io_uring`
- Persist player purses across server restart and client sessions, and have the server remember client identities somehow (e.g. cookie persisted on client)
