# Poker Epoll Architecture Walkthrough

## Executive Summary

This codebase is a **single-threaded, event-driven poker engine** built around Linux `epoll` and protobuf-framed TCP messages. The architecture is intentionally deterministic: all game-state mutations happen on one thread in strict event-loop order, which minimizes coordination overhead and makes correctness easier to reason about.

At a high level:

- `main.cc` owns the reactor loop and socket I/O.
- `Server` manages connection/session lifecycle and table assignment.
- `Table` encapsulates the poker state machine and emits domain events.
- `PlayerManager` handles seat/holding/index mechanics.
- `proto_translate` bridges domain variants to wire messages.

This mirrors a low-latency engine design pattern: one serialized execution context per state partition.

---

## 1. Runtime and Eventing Model

The runtime is centered on a nonblocking listener and edge-triggered epoll (`EPOLLET`).

- Listener setup and nonblocking mode happen once at startup.
- `epoll_wait` dispatches readiness batches.
- For each ready FD, reads/writes are drained in loops until `EAGAIN`.

This model is efficient for low-latency workloads because it avoids thread synchronization in the hot path.

### Framing Strategy

Messages are length-prefixed (`uint32_t`, network byte order) followed by protobuf payload bytes. Parsing is incremental and tolerant of partial reads.

- Server framing parse: `try_parse_frame` in `main.cc`.
- Client framing mirrors this exactly in `client.py`.

---

## 2. Protocol Contract (Protobuf)

The protocol is strongly typed and versioned under `poker.v1`.

- `Action`: oneof `{ fold, bet }`
- `Event`: oneof of all game transitions (`player_added`, `bet_placed`, `phase_advanced`, etc.)
- `Error`: oneof across `ServerError`, `GameError`, `PlayerMgmtError`
- `Response`: repeated `ServerMessage` (event or error)

Using `oneof` keeps payload interpretation explicit and avoids ambiguous message shapes.

---

## 3. Server Layer: Connection and Table Orchestration

`Server` owns:

- `connections_`: `unordered_map<PlayerId, unique_ptr<Conn>>`
- `tables_`: `unordered_map<TableId, Table>`
- Monotonic IDs for players/tables

### Connect Path

1. Allocate `PlayerId`, construct `Conn`, register with epoll.
2. Enforce max connected clients.
3. Find first table with an open seat; create one if none available.
4. Call `Table::add_player` and return result as `std::expected`.

### Close Path

On disconnect:

1. Remove FD from epoll, close fd, erase connection.
2. Remove player from table.
3. **If that removal makes table empty, erase table from `tables_`.**

This avoids stale empty tables remaining resident and being reused with stale state.

---

## 4. Table: Core State Machine and Hand Engine

`Table` is where most non-trivial logic lives.

### State Shapes

- `hand_state_` is `std::optional<HandState>`.
- `HandState` tracks phase, button, bets, committed amounts, board cards, hole cards, participants, turn queue, and per-player state.

### Start Hand

`handle_new_hand`:

- Validates enough players and no active hand.
- Seats held players via `PlayerManager`.
- Advances button and computes active ring.
- Deals cards, posts blinds, builds turn queue.
- Emits a rich event stream (`HandStarted`, `PhaseAdvanced`, `PlayerChips`, `DealtHole`, blind bets, initial `TurnAdvanced`).

### Action Application

`on_action(Action)` uses `std::visit` over variant actions:

- Validates state existence, actor presence, and turn ownership.
- Dispatches to specialized handlers (`handle(Bet)`, `handle(Fold)`, `handle(Timeout)`).
- Handles terminal conditions:
  - one player remains → immediate award and hand reset,
  - everyone all-in/no active actors → reveal remaining board + side-pot distribution,
  - normal progression → street advance or turn advance.

### Betting/Raise Semantics

`handle(Bet)` enforces:

- check/call/raise constraints,
- all-in normalization,
- min-raise tracking,
- queue rebuild after raises (requeue active players in ring order).

This is one of the densest and most correctness-sensitive sections.

### Side Pot Logic

`build_side_pots` and `distribute_side_pots` implement layered contribution slicing:

- sort contributions by amount,
- build pot layers by deltas,
- compute eligible winners per layer (`active` + `all_in`),
- rank hands (`phevaluator`) and split payouts (with remainder distribution order).

---

## 5. PlayerManager: Seating and Holding Queue

`PlayerManager` separates admission vs active seat participation:

- `holding_`: newly joined players waiting for next hand,
- `seats_`: currently seated players,
- `index_`: player→seat mapping,
- `open_seats_`: free seat indices.

At hand start, `seat_held_players` moves players from holding into seats and assigns `kBuyIn`.

Ring traversal (`next_player`, `active_cycle_from`) supports betting order and button movement.

---

## 6. Domain/Wire Translation Layer

`proto_translate` isolates transport concerns:

- `from_proto_action` maps protobuf to internal `Action` variant.
- `to_proto_event` and `to_proto_error` map domain types to protobuf with `std::visit` and compile-time branching.

This separation prevents protobuf classes from leaking into core domain logic.

---

## 7. C++ Features Worth Calling Out

The implementation uses modern C++ in useful, pragmatic ways:

- `std::expected<T, E>` for explicit error channels in game/server APIs.
- `std::variant` + `std::visit` for algebraic action/event/error modeling.
- `if constexpr` + `std::is_same_v` for compile-time serializer dispatch.
- RAII ownership with `std::unique_ptr` for connection lifecycle.
- `std::optional` to represent hand active/inactive state cleanly.

This yields strong type safety and clear control flow without exceptions in the hot path.

---

## 8. Performance and Reliability Considerations

### Strengths

- Deterministic mutation order (single-threaded state machine).
- No lock contention in core path.
- Efficient readiness-driven I/O with bounded per-event processing loops.

### Current Risks / Improvement Opportunities

- Throughput scaling is single-core bounded unless sharded by table/process.
- Some TODOs indicate future hardening needed around error-path resource handling.
- Table RNG seed currently fixed in `Server::handle_connect` (`std::mt19937_64 rng(0)`), useful for reproducibility but not production randomness.

---

## 9. Build and Test Layout

CMake defines:

- `poker_epoll` static library for engine logic,
- `poker_server` executable for network runtime,
- gtest binaries by subsystem (`cards_tests`, `player_manager_tests`, `table_tests`, `server_tests`),
- protobuf generation for both C++ and Python client stubs.

This separation supports local unit validation without requiring end-to-end network tests for all logic.

---

## 10. Recent Fix: Empty Table Cleanup

A key lifecycle fix is now in place:

- If the last player disconnects from a table, `Server::handle_close` removes the table from `tables_`.
- `Table::is_empty()` provides explicit emptiness check.
- Regression test (`server_tests`) verifies that a new connection after full disconnect receives a new table ID.

This closes a class of stale-table lifecycle bugs and makes table ownership semantics explicit.
