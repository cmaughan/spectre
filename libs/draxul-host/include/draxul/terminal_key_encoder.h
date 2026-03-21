#pragma once

#include <draxul/events.h>
#include <draxul/vt_state.h>

#include <string>

namespace draxul
{

// Translates a key event into the VT escape sequence that should be sent to the
// terminal process. Returns an empty string if the event produces no sequence
// (e.g. modifier-only key, unhandled keycode).
std::string encode_terminal_key(const KeyEvent& event, const VtState& vt);

} // namespace draxul
