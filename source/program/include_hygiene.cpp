// Compile-only TU to catch missing includes and brittle include ordering.
//
// d3hack-custom: OFF BY DEFAULT. Build with -DEXL_INCLUDE_HYGIENE=ON to run the check.
//
// This TU emits nothing. Verified, not assumed: `nm` on its object shows ZERO strong (T/D/B)
// symbols, and the linker map shows 163 of its 164 input sections under "Discarded input
// sections" -- not one byte reaches the binary. The inline variables in util.hpp are emitted by
// main.cpp, which actually odr-uses them; this TU references none of them.
//
// What it DID cost was ~8 s of wall time on every build, because it re-parses ~550 headers --
// config.hpp pulls toml++ and imgui_overlay.hpp pulls imgui. Preprocessing alone measured
// user 0.27 s against real 10.1 s, so it is bind-mount I/O, not compilation.
//
// Note also that trimming a single header out of the list saves nothing: the transitive
// closures overlap almost completely, so the whole TU is the only unit worth toggling.
//
// The check itself is weaker than it looks -- all 18 headers are included in one TU in a fixed
// order, so only the first is genuinely tested standalone, and every one of them is already
// compiled by at least one real .cpp. The project's actual hygiene tool is IWYU
// (cmake/Iwyu.cmake, EXL_ENABLE_IWYU), which covers every source.

#ifdef D3HACK_INCLUDE_HYGIENE

#include "d3/_util.hpp"
#include "d3/hooks/debug.hpp"
#include "d3/hooks/resolution.hpp"
#include "d3/hooks/util.hpp"
#include "d3/patches.hpp"
#include "d3/resolution_util.hpp"
#include "program/config.hpp"
#include "program/config_schema.hpp"
#include "program/exception_handler.hpp"
#include "program/fs_util.hpp"
#include "program/gui2/imgui_overlay.hpp"
#include "program/gui2/input_util.hpp"
#include "program/gui2/ui/overlay.hpp"
#include "program/gui2/ui/virtual_keyboard.hpp"
#include "program/log_once.hpp"
#include "program/logging.hpp"
#include "program/runtime_apply.hpp"
#include "program/tagnx.hpp"

#endif  // D3HACK_INCLUDE_HYGIENE

namespace {
    [[maybe_unused]] void IncludeHygieneDummy() {}
}  // namespace
