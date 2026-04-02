#pragma once
// Convenience header: includes both nvim_rpc.h and nvim_ui.h.
// Prefer the narrower headers in new code:
//   <draxul/nvim_rpc.h>  — NvimProcess, MpackValue, RPC types, IRpcChannel, NvimRpc
//   <draxul/nvim_ui.h>   — ModeInfo, UiEventHandler, NvimInput
#include <draxul/nvim_rpc.h>
#include <draxul/nvim_ui.h>
