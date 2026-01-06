#pragma once

#include "errors.h"
#include "errors.pb.h"
#include "events.pb.h"
#include "table.h"

namespace poker {

auto to_proto_error(const Error &err) -> ::poker::v1::Error;
auto to_proto_event(const Event &ev) -> ::poker::v1::Event;

} // namespace poker
