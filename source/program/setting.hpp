#pragma once

#include "common.hpp"  // IWYU pragma: keep

#define EXL_MODULE_NAME "D3Hack"

#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

/*
#define EXL_SUPPORTS_REBOOTPAYLOAD
*/

namespace exl::setting {
    /* How large the fake .bss heap will be. */
    constexpr size_t HeapSize = 0x3000;

    /* How large the JIT area will be for hooks. */
    //
    // d3hack-custom: raised 0x4000 -> 0x10000 on 2026-08-25. HookMax is
    // JitSize / (TrampolineSize * 4) = JitSize / 200, so 0x4000 allowed only 81 trampolines
    // and this mod was sitting right on that ceiling: adding two probes aborted the BOOT with
    //
    //     Failed: AllocForTrampoline(&rxtrampoline, &rwtrampoline)
    //     hook_impl.cpp:602  exl::hook::nx64::Hook
    //
    // which names an allocator, not a hook count, so it does not read as "too many hooks".
    // 0x10000 gives 327. If this aborts again, raise it again -- it is 64 KB of JIT area, not
    // a design constraint.
    constexpr size_t JitSize = 0x10000;  // default 0x1000 usually works

    /* How large the area will be inline hook pool. */
    // d3hack-custom: raised alongside JitSize. Every HOOK_DEFINE_INLINE consumes an entry here
    // AND a trampoline above, so the two limits are hit together.
    constexpr size_t InlinePoolSize = 0x10000;  // default 0x1000 usually works

    /* How large the formatting buffer should be for logging. The buffer will be on the stack. */
    constexpr size_t LogBufferSize = 256;

    /* Sanity checks. */
    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
    static_assert(ALIGN_UP(InlinePoolSize, PAGE_SIZE) == InlinePoolSize, "");
}  // namespace exl::setting
