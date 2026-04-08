#pragma once

// Facade header -- includes the config-only surface (data types + I/O).
// AppOptions (which holds renderer/window factory functions) lives in
// draxul-runtime-support; include <draxul/app_options.h> explicitly when
// you need it.
//
// New code should prefer including the specific header it needs:
//   - <draxul/app_config_types.h>  -- struct definitions only (no TOML, no I/O)
//   - <draxul/keybinding_parser.h> -- chord parsing and keybinding matching
//   - <draxul/app_config_io.h>     -- I/O (load/save) and TOML parse/serialize
//   - <draxul/app_options.h>       -- runtime AppOptions (in draxul-runtime-support)

#include <draxul/app_config_io.h>
