#pragma once

#include "session_state.h"

#include <string>
#include <vector>

namespace draxul
{

std::vector<SessionSummary> list_known_sessions(std::string* error = nullptr);

} // namespace draxul
