#pragma once

#include "errors.h"
#include "errors.pb.h"
#include "events.pb.h"
#include "actions.pb.h"
#include "table.h"

namespace poker {

auto to_proto_error(const Error &err) -> ::poker::v1::Error;
auto to_proto_event(const Event &ev) -> ::poker::v1::Event;
auto from_proto_action(const ::poker::v1::Action &action, PlayerId id)
    -> std::expected<Action, GameError>;

} // namespace poker
