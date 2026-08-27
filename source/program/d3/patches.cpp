#include "d3/patches.hpp"

#include "d3/_util.hpp"
#include "d3/resolution_util.hpp"
#include "lib/armv8/instructions.hpp"
#include "lib/armv8/register.hpp"
#include "lib/patch/random_access_patcher.hpp"
#include "program/build_stamp.hpp"
#include "program/config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace d3 {

    const char *const    g_szHackVerWatermark = d3::build_stamp::kVersionLine;
    const char *const    g_szHackVerAutosave  = d3::build_stamp::kAutosaveString;
    constinit const char g_szHackVerStart[]   = CRLF D3HACK_FULLVER CRLF CRLF CRLF;

    constinit const char      g_szTraceStat[]         = "sd:/config/d3hack-nx/debug.txt";
    constinit const char      g_szVariableResString[] = "%4dp Output (Variable: %4dp)";
    constinit const Signature g_tSignature {.szName = "   " D3HACK_TAGLINE_SHORT CRLF " ", .szComment = "Diablo III v", .nMonth = 2, .nDay = 7, .nYear = 6};
    const RGBAColor           g_rgbaText       = crgbaUIGold;
    const RGBAColor           g_rgbaDropShadow = crgbaWatermarkShadow;

    namespace patch = exl::patch;
    namespace reg   = exl::armv8::reg;
    namespace ins   = exl::armv8::inst;

    using dword                                       = std::array<std::byte, 0x4>;
    static const dword k_infinite_mp_restore_call     = make_bytes(0x41, 0xFF, 0xFF, 0x97);  // BL ActorCommonData::ResourceAttributeSetFloat(int,float,int)
    static const dword k_dynamic_seasonal_restore_tbz = make_bytes(0xA8, 0x00, 0x90, 0x36);  // TBZ W8,#0x12,loc_4CACCC (2.7.6 @ 0x4CACB8)

    static bool     g_dynamic_seasonal_patch_applied = false;
    static uint64_t g_dynamic_seasonal_generation    = 0;
    static auto     PatchTable(const char *name) -> uintptr_t;

    static void SetDynamicSeasonalPatchArmed(bool armed, const char *reason) {
        if (!global_config.seasons.active) {
            armed = false;
        }
        if (g_dynamic_seasonal_patch_applied == armed) {
            return;
        }

        auto jest = patch::RandomAccessPatcher();
        if (armed) {
            jest.Patch<ins::Nop>(PatchTable("patch_dynamic_seasonal_10_nop"));
        } else {
            jest.Patch<dword>(PatchTable("patch_dynamic_seasonal_10_nop"), k_dynamic_seasonal_restore_tbz);
        }

        g_dynamic_seasonal_patch_applied = armed;
        ++g_dynamic_seasonal_generation;
        PRINT(
            "[seasonal_gate] apply=%d gen=%llu reason=%s",
            armed ? 1 : 0,
            static_cast<unsigned long long>(g_dynamic_seasonal_generation),
            reason ? reason : "<none>"
        )
    }

    static auto PatchTable(const char *name) -> uintptr_t {
        return GameOffsetFromTable(name) - exl::util::modules::GetTargetStart();
    }

    static void MakeAdrlPatch(const uintptr_t mainAddr, uintptr_t adrpTarget, const exl::armv8::reg::Register registerTarget) {
        auto            jest       = patch::RandomAccessPatcher();
        uintptr_t const adrpOffset = mainAddr;
        auto            diff       = ins::Adrp::GetDifference(GameOffset(adrpOffset), adrpTarget);
        jest.Patch<ins::Adrp>(adrpOffset, registerTarget, diff.m_Page);
        jest.Patch<ins::AddImmediate>(adrpOffset + 4, registerTarget, registerTarget, diff.m_Offset);
    }

    /* String swap and formatting for BuildLockerDrawWatermark() */
    void PatchBuildlocker() {
        if (!(global_config.overlays.active && global_config.overlays.buildlocker_watermark))
            return;
        auto jest = patch::RandomAccessPatcher();
        /* Spoof the existence of a build signature so we can use the debug watermark (tSignature) */
        jest.Patch<ins::Nop>(PatchTable("patch_buildlocker_01_nop"));
        jest.Patch<ins::Movz>(PatchTable("patch_buildlocker_02_movz"), reg::W20, (8 + GLOBALSNO_FONT_SCRIPT));
        MakeAdrlPatch(PatchTable("patch_buildlocker_03_adrl"), reinterpret_cast<uintptr_t>(&g_tSignature), reg::X2);
        MakeAdrlPatch(PatchTable("patch_buildlocker_04_adrl"), reinterpret_cast<uintptr_t>(&crgbaTan), reg::X19);
        MakeAdrlPatch(PatchTable("patch_buildlocker_05_adrl"), reinterpret_cast<uintptr_t>(g_szHackVerWatermark), reg::X1);
        auto *szBuildLockerFormat = reinterpret_cast<std::array<char, 21> *>(jest.RwFromAddr(PatchTable("data_build_locker_format_rw")));
        *szBuildLockerFormat      = std::to_array("%s%s%d.%d.%d\0\0\0\0\0\0\0\0");
        jest.Patch<ins::Movz>(PatchTable("patch_buildlocker_06_movz"), reg::W21, 0x13);  // RENDERLAYER_UI_OVERLAY

        /* 0x10CC - lower right */

        /* 0x1114 - upper left */
        //      0x106C 09 D0 35 1E                   FMOV            S9, #-15.0
        //      0x10D4 0A D0 25 1E                   FMOV            S10, #15.0
        //      0x10D8 21 28 2A 1E                   FADD            S1, S1, S10 <- Y-axis from top
        //      0x10FC 00 28 29 1E                   FADD            S0, S0, S9  <- X-axis from left
        jest.Patch<dword>(PatchTable("patch_buildlocker_07_bytes"), make_bytes(0x00, 0x28, 0x2A, 0x1E));  // S0, +15.0 (S10) instead of -15.0

        /* 0x1160 - Lower left */
        //      0x1120 21 38 22 1E                   FSUB            S1, S1, S2
        //      0x1128 00 28 29 1E                   FADD            S0, S0, S9
        //      0x1150 21 28 29 1E                   FADD            S1, S1, S9
        jest.Patch<dword>(PatchTable("patch_buildlocker_08_bytes"), make_bytes(0x00, 0x28, 0x2A, 0x1E));  // S0, +15.0 (S10) instead of -15.0

        /* 0x11AC - upper right */

        /* 0x1224 - center screen */
        jest.Patch<ins::Nop>(PatchTable("patch_buildlocker_09_nop"));

        /* 0x12F8 - primary, lower middle */
        //      0x11C0 0A 10 2C 1E                   FMOV            S10, #0.5 <- center X axis on screen
        // jest.Patch<dword>(PatchTable("patch_buildlocker_10_bytes"), make_bytes(0x0A, 0x10, 0x2E, 0x1E));  // #1.0, aka shift right
    }

    /* String swap and formatting for APP_DRAW_VARIABLE_RES_DEBUG_BIT */
    void PatchVarResLabel() {
        auto jest = patch::RandomAccessPatcher();
        jest.Patch<ins::Movz>(PatchTable("patch_var_res_label_01_movz"), reg::W0, (8 + GLOBALSNO_FONT_EXOCETLIGHT));
        // 3CC78    movz x8, #0x42c8, lsl #16   --> 100.0f
        jest.Patch<ins::Movz>(PatchTable("patch_var_res_label_02_movz"), reg::X8, 0x4288, ins::ShiftValue_16);  // X-axis: 68.0f
        // 3CC7C    movk x8, #0x4316, lsl #48   --> 150.0f
        jest.Patch<ins::Movk>(PatchTable("patch_var_res_label_03_movk"), reg::X8, 0x4080, ins::ShiftValue_48);  // Y-axis: 4.0f

        // MakeAdrlPatch(PatchTable("patch_var_res_label_04_adrl"), reinterpret_cast<uintptr_t>(&c_szVariableResString), reg::X1);
        // AppDrawFlagSet(APP_DRAW_VARIABLE_RES_DEBUG_BIT, 1);     // Must be run each frame in another hook
    }

    void PatchGraphicsPersistentHeapEarly() {
        auto jest = patch::RandomAccessPatcher();
        // SigmaMemoryInit (sSigmaMemoryInit, 0xA36A50): gptGraphicsPersistentMemoryHeap size.
        // 0xA36B34: MOV reg::W19, #0x17400000
        u32 gfxPersistentHeapSize = 0x17400000u;

        [[maybe_unused]] constexpr u32 MB32 = 0x02000000u;
        [[maybe_unused]] constexpr u32 MB64 = 0x04000000u;

        // Bump graphics persistent heap (XRemoteHeap) to reduce alloc failures.
        if (g_ptDevMemMode != nullptr && *g_ptDevMemMode != 0u) {
            gfxPersistentHeapSize += 0x40000000u;  // extra 1GB if extra RAM detected
        } else {
            gfxPersistentHeapSize += MB64;         // 64MB allows 1440p, stable HOS >=20.x operation on hardware
        }

        jest.Patch<ins::Movz>(0xA36B34, reg::W19, (gfxPersistentHeapSize >> 16), ins::ShiftValue_16);
        PRINT_EXPR("Patched to: %08X", gfxPersistentHeapSize)
    }

    /* String swap and formatting for APP_DRAW_FPS_BIT */
    void PatchReleaseFPSLabel() {
        auto jest = patch::RandomAccessPatcher();
        jest.Patch<ins::Movz>(PatchTable("patch_release_fps_label_01_movz"), reg::W0, (8 + GLOBALSNO_FONT_EXOCETLIGHT));
        auto *szReleaseFPSFormat = reinterpret_cast<std::array<char, 10> *>(jest.RwFromAddr(PatchTable("data_release_fps_format_rw")));
        auto *nReleaseFPSPosX    = reinterpret_cast<float *>(jest.RoFromAddr(PatchTable("data_release_fps_pos_x_ro")));
        auto *nReleaseFPSPosY    = reinterpret_cast<float *>(jest.RoFromAddr(PatchTable("data_release_fps_pos_y_ro")));
        *szReleaseFPSFormat      = std::to_array("FPS: %.0f");  // :.ascii "%3.1f FPS"
        *nReleaseFPSPosX         = -77.7;                       // nReleaseFPSPosX:.float -180.0
        *nReleaseFPSPosY         = 500.0;                       // nReleaseFPSPosY:.float 290.0

        /* Skip the frame timer check/color setter (avoid flashing text colors) */
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_02_nop"));
        MakeAdrlPatch(PatchTable("patch_release_fps_label_03_adrl"), reinterpret_cast<uintptr_t>(&crgbaWhite), reg::X8);

        /* Skip rendering all extra FPS data labels */
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_04_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_05_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_06_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_07_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_08_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_09_nop"));
        jest.Patch<ins::Nop>(PatchTable("patch_release_fps_label_10_nop"));

        // AppDrawFlagSet(APP_DRAW_FPS_BIT, 1);  // Must be run each frame in another hook
    }

    void PatchDDMLabels() {
        auto jest = patch::RandomAccessPatcher();
        /* String swap and formatting for DDM_FPS_QA */
        jest.Patch<ins::Movz>(PatchTable("patch_ddm_labels_01_movz"), reg::W0, (8 + GLOBALSNO_FONT_SCRIPT));
        MakeAdrlPatch(PatchTable("patch_ddm_labels_02_adrl"), reinterpret_cast<uintptr_t>(g_szHackVerWatermark), reg::X1);
        MakeAdrlPatch(PatchTable("patch_ddm_labels_03_adrl"), reinterpret_cast<uintptr_t>(&g_rgbaText), reg::X3);
        MakeAdrlPatch(PatchTable("patch_ddm_labels_04_adrl"), reinterpret_cast<uintptr_t>(&g_rgbaDropShadow), reg::X4);
        jest.Patch<ins::Movz>(PatchTable("patch_ddm_labels_05_movz"), reg::W7, 0x13);  // RENDERLAYER_UI_OVERLAY

        /* String swap and formatting for DDM_FPS_SIMPLE */
        jest.Patch<ins::Movz>(PatchTable("patch_ddm_labels_06_movz"), reg::W0, (8 + GLOBALSNO_FONT_SCRIPT));
        MakeAdrlPatch(PatchTable("patch_ddm_labels_07_adrl"), reinterpret_cast<uintptr_t>(g_szHackVerWatermark), reg::X23);
        MakeAdrlPatch(PatchTable("patch_ddm_labels_08_adrl"), reinterpret_cast<uintptr_t>(&g_rgbaText), reg::X3);
        MakeAdrlPatch(PatchTable("patch_ddm_labels_09_adrl"), reinterpret_cast<uintptr_t>(&g_rgbaDropShadow), reg::X4);
        jest.Patch<ins::Movz>(PatchTable("patch_ddm_labels_10_movz"), reg::W7, 0x13);  // RENDERLAYER_UI_OVERLAY
    }

    void PatchResolutionTargets() {
        auto jest = patch::RandomAccessPatcher();
        /* FontDefinition::GetPointSizeData (0x276A60/0x276A6C). */
        /* 0x276A60: MOV reg::W9, #8   | 0x276A6C: MOV reg::W8, #0x10 */
        jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_08_movz"), reg::W9, 32);
        jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_09_movz"), reg::W8, 32);

        // ShellInitialize (0x6678B8): BL nn::oe::GetOperationMode
        // ShellEventLoop (0x667AE8): BL nn::oe::GetOperationMode
        // jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_10_movz"), reg::W0, 0);  // 0 = "Docked"
        // jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_11_movz"), reg::W0, 0);  // 0 = "Docked"
        // ShellInitialize (0x6678C8): BL nn::oe::GetPerformanceMode
        // ShellEventLoop (0x667B04): BL nn::oe::GetPerformanceMode
        // jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_12_movz"), reg::W0, 1);  // 1 = "Boost"
        // jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_13_movz"), reg::W0, 1);  // 1 = "Boost"
        if (!(global_config.resolution_hack.active))
            return;

        /* ►Always run perf-mode change handler on perf notify (skip v8 == gePerformanceMode check). */
        // 0x667B10: B.EQ def_667A20
        // jest.Patch<ins::Nop>(0x667B10);  // always fall through to re-init UI

        auto      resolution = global_config.resolution_hack;
        const u32 max_target = MaxResolutionHackOutputTarget();
        if (resolution.target_resolution > max_target) {
            resolution.SetTargetRes(max_target);
        }
        const u32 outW      = resolution.OutputWidthPx();
        const u32 outH      = resolution.OutputHeightPx();
        const u32 handheldH = resolution.OutputHandheldHeightPx();
        const u32 fallbackW = handheldH != 0 ? resolution.WidthForHeight(handheldH) : 1280u;
        const u32 fallbackH = handheldH != 0 ? handheldH : 720u;
        // const u32 clampW = global_config.resolution_hack.ClampTextureWidthPx();
        // const u32 clampH = global_config.resolution_hack.ClampTextureHeightPx();

        // NX64NVNHeap::EarlyInit (0x0E7770): uMaxSize/Preallocate size.
        // 0x0E7770: MOV reg::W19, #0x49C00000
        // NX64NVNHeap::EarlyInit (0x0E77A0): largest pool max_alloc.
        // 0x0E77A0: MOV reg::W3, #0x8000000
        // Increase heap headroom + largest pool max_alloc for >1280p output.
        if (outH > 1280) {
            u32 heapSize = 0x59C00000;  // +0x10000000 (256 MB)
            u32 maxAlloc = 0x10000000;  // +0x08000000 (128 MB)
            if (outH >= 1800) {
                heapSize = 0x69C00000;  // +0x20000000 (512 MB)
                maxAlloc = 0x20000000;  // +0x18000000 (384 MB)
            }
            jest.Patch<ins::Movz>(0x0E7770, reg::W19, (heapSize >> 16), ins::ShiftValue_16);
            jest.Patch<ins::Movz>(0x0E77A0, reg::W3, (maxAlloc >> 16), ins::ShiftValue_16);
        }

        // Display mode pair (full). GFXNX64NVN::Init (0x0E7850).
        // 0x0E7858: MOV reg::X8, #1600
        // 0x0E7860: MOVK reg::X8, #900, LSL#32
        jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_01_movz"), reg::X8, outW);
        jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_02_movk"), reg::X8, outH, ins::ShiftValue_32);

        // Fallback/base scale mode (same block).
        // 0x0E785C: MOV reg::X9, #1280
        // 0x0E7864: MOVK reg::X9, #720, LSL#32
        jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_03_movz"), reg::X9, fallbackW);
        jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_04_movk"), reg::X9, fallbackH, ins::ShiftValue_32);

        // d3hack-custom: HUD/UI aspect constant, same block (GFXNX64NVN::Init).
        //   0x0E7868: MOVZ W10, #0x8E39
        //   0x0E786C: MOVK W10, #0x3FE3, LSL#16      -> 0x3FE38E39 = 1.7777f (16:9)
        // Patching the display-mode pair above WITHOUT this gives an ultrawide backbuffer
        // with a 16:9 HUD stretched across it. Both are required.
        //
        // Deliberately skipped when the ratio is stock 16:9, so a default build writes
        // exactly the bytes it always has. The instruction identity here came from a
        // third-party 32:9 pchtxt, NOT from disassembling our own binary -- so this
        // unverified write stays confined to users who opted into a wide ratio.
        if (!resolution.AspectRatioIsStock()) {
            const u32 aspectBits = resolution.AspectRatioBits();
            jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_14_movz"), reg::W10, (aspectBits & 0xFFFFu));
            jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_15_movk"), reg::W10, (aspectBits >> 16), ins::ShiftValue_16);
            PRINT("[d3hack-aspect] output %ux%u  aspect %d/1000  bits %08X", outW, outH,
                  static_cast<int>(resolution.AspectRatio() * 1000.0f), aspectBits)
        }

        // if (global_config.resolution_hack.min_res_scale >= 100.0f) {
        // Too late to patch cdecl defaults at this point, and prefer the hook
        //     /* VariableResRWindowData->flMinPercent = 0.70f - 0x03CBBC: MOVK reg::X9, #0x3F33, LSL#48 (CGameVariableResInitializeForRWindow) */
        //     jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_05_movk"), reg::X9, 0x3F80, ins::ShiftValue_48);  // 100% (1.0f) minimum resolution scale
        // }

        /* VariableResRWindowData->flMaxPercent = 1.00f - 0x03CBCC: MOV reg::W8, #0x3F800000 */
        // jest.Patch<ins::Movz>(PatchTable("patch_resolution_targets_06_movz"), reg::W8, 0x3FA0, ins::ShiftValue_16);  // 125% (1.25f) maximum resolution scale (>1.0f breaks rendering)

        /* VariableResRWindowData->flPercentIncr = 0.05f - 0x03CBD4: MOVK reg::X9, #0x3D4C, LSL#48 */
        // jest.Patch<ins::Movk>(PatchTable("patch_resolution_targets_07_movk"), reg::X9, 0x3CF5, ins::ShiftValue_48);  // 3% (0.03f) resolution adjust percentage
    }

    void PatchDynamicSeasonal() {
        if (!global_config.seasons.active) {
            SetDynamicSeasonalPatchArmed(false, "config_inactive");
            return;
        }

        PRINT("Setting active Season to %d", global_config.seasons.current_season)
        XVarUint32_Set(&g_varSeasonNum, global_config.seasons.current_season, 3u);
        XVarUint32_Set(&g_varSeasonState, 1, 3u);

        auto jest = patch::RandomAccessPatcher();
        jest.Patch<ins::Movz>(PatchTable("patch_dynamic_seasonal_03_movz"), reg::W1, 1);  // always true for UIHeroCreate::Console::bSeasonConfirmedAvailable() (skip B.ne and always confirm)
        jest.Patch<ins::Movz>(PatchTable("patch_dynamic_seasonal_04_movz"), reg::W0, 1);  // always true for Console::Online::IsSeasonsInitialized()

        // Update season_created uses at runtime (UIOnlineActions::SetGameParamsForHero).
        jest.Patch<ins::Movz>(PatchTable("patch_dynamic_seasonal_05_movz"), reg::W21, global_config.seasons.current_season);  // season_created = ...
        // Ignore mismatched hero season in UIHeroSelect (skip Rebirth/season prompt for old seasons).
        // jest.Patch<ins::Nop>(PatchTable("patch_dynamic_seasonal_06_nop"));  // B.ne loc_358C74
        // Hide "Create Seasonal Hero" main menu option (item 9) when forcing season current_season.
        // ItemShouldBeVisible(case 9) calls IsSeasonsInitialized; forcing reg::W0=0 makes it return false.
        // jest.Patch<ins::Movz>(PatchTable("patch_dynamic_seasonal_07_movz"), reg::W0, 0);
        /* Always return error_none for Console::UIOnlineActions::ValidateHeroForPartyMember (skip function body). */
        // 0x1BF5F0: SUB SP, SP, #0x100
        jest.Patch<ins::Movz>(PatchTable("patch_dynamic_seasonal_08_movz"), reg::W0, 0);  // error_none
        // 0x1BF5F4: STR X23, [SP,#0xF0+var_30]
        jest.Patch<ins::Ret>(PatchTable("patch_dynamic_seasonal_09_ret"));

        // Runtime-spawn gate defaults to OFF until the next GameCommonData::Initialize call.
        SetDynamicSeasonalPatchArmed(false, "season_bootstrap");
    }

    void UpdateDynamicSeasonalForSpawn(const GameParams *tParams) {
        if (!global_config.seasons.active) {
            SetDynamicSeasonalPatchArmed(false, "seasons_inactive");
            return;
        }

        constexpr uint32 kSeasonalCreationFlag = (1u << 18);

        const bool params_valid    = (tParams != nullptr);
        const bool params_seasonal = params_valid && ((tParams->dwCreationFlags & kSeasonalCreationFlag) != 0u);
        const bool spawn_seasonal  = params_valid && params_seasonal;

        PRINT(
            "[seasonal_gate] eval conn=%x flags=0x%x params_valid=%d params=%d",
            params_valid ? tParams->idGameConnection : static_cast<uint32>(0xFFFFFFFFu),
            params_valid ? tParams->dwCreationFlags : 0u,
            params_valid ? 1 : 0,
            params_seasonal ? 1 : 0
        )

        SetDynamicSeasonalPatchArmed(spawn_seasonal, spawn_seasonal ? "spawn_seasonal" : "spawn_nonseasonal");
    }

    // d3hack-custom: let seasonal-theme items exist outside a seasonal game.
    //
    // Ethereals, Soul Shards and Sanctified items are refused in two places, and simply
    // enabling their community buffs is not enough -- both gates fail closed on a
    // non-seasonal hero, which is why the CommunityBuff* flags on their own change nothing.
    //
    // 0x492F2C, on the drop list. A seasonal game returns early at 0x492F04 and never
    // filters; a non-seasonal one falls through to
    //     bl IsSeasonalOnly ; cbnz w0, 0x492F3C   -- 0x492F3C returns -1, i.e. no drop
    // so NOP that one branch. Do NOT touch the `cbz w0, 0x492F0C` at 0x492F00: 0x492F04
    // leads to the function EPILOGUE, so skipping it returns before anything is rolled
    // and kills all loot, seasonal or not. A normal item returns 0 from IsSeasonalOnly
    // and never takes the branch, so NOPing it cannot affect ordinary drops.
    // 0x897870, ItemAllowedForHero(gbidItem, ptACD):
    //     if (IsSeasonalOnly(gbid) && !GameIsSeasonal())           return 0;   // 0x897894
    //     if (IsEthereal(gbid)   && !ETHEREALITEMSUNLOCKED)        return 0;   // 0x8978B4
    //     if (IsSoulShard(gbid)  && !SOULSHARDSUNLOCKED)           return 0;   // 0x8978D0
    //     if (IsSanctified(gbid) && !SANCTIFIEDITEMSUNLOCKED)      return 0;   // 0x8978EC
    //     return 1;
    // The three UNLOCKED attributes are granted by the season theme, so a non-seasonal
    // hero has none of them regardless of the seasonal check.
    //
    // NOP the five deny-branches rather than forcing the function to return 1 outright:
    // every other test (item validity, the mode == 6 case) still runs, and each skipped
    // gate stays visible at its own address.
    void PatchSeasonalItemGates() {
        if (!global_config.events.active || !global_config.events.AllowSeasonalItemsOffSeason)
            return;
        auto               jest       = patch::RandomAccessPatcher();
        static const char *s_arGates[] = {
            "patch_seasonitem_droplist_nop", "patch_seasonitem_hero_season_nop",
            "patch_seasonitem_ethereal_nop", "patch_seasonitem_soulshard_nop",
            "patch_seasonitem_sanctified_nop"};
        for (const char *k : s_arGates)
            jest.Patch<ins::Nop>(PatchTable(k));
        PRINT("[d3hack-custom] seasonal item gates opened (%d sites): ethereal/soulshard/sanctified off-season",
              static_cast<int>(sizeof(s_arGates) / sizeof(s_arGates[0])))
    }

    void PatchDynamicEvents() {
        if (!global_config.events.active)
            return;

        constexpr s64 kBuffStartFallback = 1;
        constexpr s64 kBuffEndFallback   = 0x7FFFFFFFFFFFFFFFLL;

        auto *buff_start = *reinterpret_cast<s64 **>(GameOffsetFromTable("event_buff_start"));
        auto *buff_end   = *reinterpret_cast<s64 **>(GameOffsetFromTable("event_buff_end"));
        if ((buff_start != nullptr) && (buff_end != nullptr) && ((*buff_start == 0) || (*buff_end == 0) || *buff_end <= *buff_start)) {
            *buff_start = kBuffStartFallback;
            *buff_end   = kBuffEndFallback;
        }

        if (auto *egg_flag = *reinterpret_cast<u32 **>(GameOffsetFromTable("event_egg_flag")))
            *egg_flag = global_config.events.EasterEggWorldEnabled ? 1u : 0u;

        if (auto *igr = *reinterpret_cast<uintptr_t **>(GameOffsetFromTable("event_igr")))
            XVarBool_Set(igr, global_config.events.IgrEnabled, 3u);
        if (auto *ann = *reinterpret_cast<uintptr_t **>(GameOffsetFromTable("event_ann")))
            XVarBool_Set(ann, global_config.events.AnniversaryEnabled, 3u);
        if (auto *egg_xvar = *reinterpret_cast<uintptr_t **>(GameOffsetFromTable("event_egg_xvar")))
            XVarBool_Set(egg_xvar, global_config.events.EasterEggWorldEnabled, 3u);
        if (auto *event_enabled = *reinterpret_cast<uintptr_t **>(GameOffsetFromTable("event_enabled")))
            XVarBool_Set(event_enabled, true, 3u);
        if (auto *event_season_only = *reinterpret_cast<uintptr_t **>(GameOffsetFromTable("event_season_only")))
            XVarBool_Set(event_season_only, false, 3u);

        auto set_bool = [](uintptr_t *var, bool value) -> void {
            if (var)
                XVarBool_Set(var, value, 3u);
        };

        set_bool(&g_varDoubleRiftKeystones, global_config.events.DoubleRiftKeystones);
        set_bool(&g_varDoubleBloodShards, global_config.events.DoubleBloodShards);
        set_bool(&g_varDoubleTreasureGoblins, global_config.events.DoubleTreasureGoblins);
        set_bool(&g_varDoubleBountyBags, global_config.events.DoubleBountyBags);
        set_bool(&g_varRoyalGrandeur, global_config.events.RoyalGrandeur);
        set_bool(&g_varLegacyOfNightmares, global_config.events.LegacyOfNightmares);
        set_bool(&g_varTriunesWill, global_config.events.TriunesWill);
        set_bool(&g_varPandemonium, global_config.events.Pandemonium);
        set_bool(&g_varKanaiPowers, global_config.events.KanaiPowers);
        set_bool(&g_varTrialsOfTempests, global_config.events.TrialsOfTempests);
        set_bool(&g_varShadowClones, global_config.events.ShadowClones);
        set_bool(&g_varFourthKanaisCubeSlot, global_config.events.FourthKanaisCubeSlot);
        set_bool(&g_varEtherealItems, global_config.events.EtherealItems);
        set_bool(&g_varSoulShards, global_config.events.SoulShards);
        set_bool(&g_varSwarmRifts, global_config.events.SwarmRifts);
        set_bool(&g_varSanctifiedItems, global_config.events.SanctifiedItems);
        set_bool(&g_varDarkAlchemy, global_config.events.DarkAlchemy);
        set_bool(&g_varNestingPortals, global_config.events.NestingPortals);
        set_bool(&g_varParagonCap, false);  // ParagonCap off by default
    }

    static inline void PortCheatCodes() {
        if (!global_config.rare_cheats.active)
            return;
        auto jest = patch::RandomAccessPatcher();

        const auto &cheats = global_config.rare_cheats;
        /* Restore debug display of allocation errors */
        jest.Patch<ins::Movz>(PatchTable("patch_cheat_alloc_errors_01_movz"), reg::W8, 0);
        // jest.Patch<ins::MovRegister>(PatchTable("patch_cheat_alloc_errors_02_bytes"), reg::X0, SP);
        jest.Patch<dword>(PatchTable("patch_cheat_alloc_errors_02_bytes"), make_bytes(0xE0, 0x03, 0x00, 0x91));

        /* d3hack-custom: paragon level cap.
           The cap is a hardcoded MOVZ #20000 in four places, NOT a table row count.
           The XP-table index is separately clamped to 19999 (0x2130CC), so levels past
           20000 simply reuse the last row -- raising these is safe and needs no table
           extension; XP per level just goes flat. MOVZ imm16 caps the value at 65535. */
        if (global_config.rare_cheats.max_paragon_level > 20000) {
            // MOVZ carries a 16-bit immediate. Values above 65535 are encoded shifted left
            // by 16, so the effective cap rounds down to a multiple of 65536.
            const u32 want  = static_cast<u32>(global_config.rare_cheats.max_paragon_level);
            const bool hi   = (want > 65535u);
            const u32 imm   = hi ? (want >> 16) : want;
            const u32 cap   = hi ? (imm << 16) : imm;
            const u32 movzB = hi ? 0x52A00000u : 0x52800000u;
            const char *keys[6] = {"patch_paragon_cap_01", "patch_paragon_cap_02",
                                   "patch_paragon_cap_03", "patch_paragon_cap_04",
                                   "patch_paragon_cap_05", "patch_paragon_cap_06"};
            const u32   rd[6]   = {8u, 8u, 8u, 9u, 27u, 0u};  // destination register per site
            for (int k = 0; k < 6; ++k) {
                const u32 word = movzB | (imm << 5) | rd[k];
                jest.Patch<dword>(PatchTable(keys[k]),
                                  make_bytes(static_cast<u8>(word & 0xFFu), static_cast<u8>((word >> 8) & 0xFFu),
                                             static_cast<u8>((word >> 16) & 0xFFu), static_cast<u8>((word >> 24) & 0xFFu)));
            }
            // 0xA40AA0 is the bitfield serializer: it range-checks each value against the
            // field descriptor min/max ([x1+0x20]/[x1+0x28]) and asserts on overflow.
            // Paragon level is a range-bounded field capped at 20000, so 20001 trips it.
            // The STOCK field is 15 bits wide (0..20000 needs 15), which would put a hard
            // ceiling at 32767. Ctor_WidenParagonField in d3/hooks/debug.hpp lifts that by
            // inflating the declared range to 0x7FFFFFFF at descriptor construction, so the
            // derived width comes out at 31 bits and a cap past 32767 is safe. It is installed
            // by the ParagonFieldWidening registry entry, gated on this same cap being raised.
            // The two have to move together: raising the cap without the widening truncates
            // the level on the wire, with no symptom until a character is already past it.
            jest.Patch<ins::Nop>(PatchTable("patch_bitfield_range_assert_nop"));
            // 0x7F71F0 enumerates GB_PARAGON_BONUSES (eType 0x28) and writes 0 into
            // every PARAGON_BONUS attribute -- that is what wipes allocations on load.
            // Turn both cbz guards into unconditional branches to skip the loop.
            PRINT("[d3hack-custom] paragon level cap: 20000 -> %u (6 sites + serializer assert)", cap)
        }

        // d3hack-custom: paragon XP table index clamp.
        // 0x4C4118 indexes the XP table by raw paragon level with no bounds check, so the
        // table must otherwise cover the whole cap (2 billion rows = 238 GB). Replace that
        // single `sxtw x9, w20` with a call into the .text/.rodata padding gap holding:
        //     mov w9, #19999 ; cmp w20, w9 ; csel w9, w20, w9, lt ; sxtw x9, w9 ; ret
        // Levels past 19999 then reuse the last row (flat XP per level) instead of reading
        // out of bounds. BL clobbers x30, which is safe here: the function already makes
        // calls, so its return address is on the stack.
        if (global_config.rare_cheats.max_paragon_level > 20000) {
            static const u32 s_cave[5] = {0x5289C3E9u, 0x6B09029Fu, 0x1A89B289u, 0x93407D29u, 0xD65F03C0u};
            static const char *s_keys[5] = {"patch_xpidx_cave_0", "patch_xpidx_cave_1", "patch_xpidx_cave_2",
                                            "patch_xpidx_cave_3", "patch_xpidx_cave_4"};
            for (int i = 0; i < 5; ++i) {
                const u32 w = s_cave[i];
                jest.Patch<dword>(PatchTable(s_keys[i]),
                                  make_bytes(static_cast<u8>(w & 0xFFu), static_cast<u8>((w >> 8) & 0xFFu),
                                             static_cast<u8>((w >> 16) & 0xFFu), static_cast<u8>((w >> 24) & 0xFFu)));
            }
            jest.Patch<dword>(PatchTable("patch_xpidx_call"), make_bytes(0x62, 0xBA, 0x1E, 0x94));
            PRINT("[d3hack-custom] xp table index clamped at %d via cave 0x%X", 19999, 0xC72AA0)
        }

        // d3hack-custom: per-attribute paragon spend limit.
        // 0x5271F0 is the limit getter and has FOUR callers: spend (0x7F6750), load-time
        // validation (0x7F6878) and two UI sites (0x2141F0, 0x214834). The UI gates the
        // button before the spend path runs, and the record is a GBRecordGet temporary
        // that gets released, so neither consumer-patching nor data-patching works.
        // Scale the return instead: mov w0,w19 -> add w0,w19,w19,lsl #k = limit*(1+2^k).
        // k=2 gives x5, turning 50 into exactly 250; main stat/Vitality 100000 -> 500000.
        if (global_config.rare_cheats.paragon_stat_cap > 50) {
            const int want = global_config.rare_cheats.paragon_stat_cap;
            u32       k    = 2u;
            for (u32 t = 1u; t <= 5u; ++t) {
                if (50 * static_cast<int>(1u + (1u << t)) == want) {
                    k = t;
                    break;
                }
            }
            const u32 word = 0x0B000000u | (19u << 16) | (k << 10) | (19u << 5);
            jest.Patch<dword>(PatchTable("patch_paragon_limit_scale"),
                              make_bytes(static_cast<u8>(word & 0xFFu), static_cast<u8>((word >> 8) & 0xFFu),
                                         static_cast<u8>((word >> 16) & 0xFFu), static_cast<u8>((word >> 24) & 0xFFu)));
            PRINT("[d3hack-custom] paragon per-stat limit scaled x%u (50 -> %u), requested %d",
                  1u + (1u << k), 50u * (1u + (1u << k)), want)
        }
        // d3hack-custom: stop the game WIPING a paragon category, without also killing respec.
        //
        // What this used to do -- and why it was wrong. 0x7F71E0 is
        // `ResetParagonCategory(actor, category)`: it walks every GB_PARAGON_BONUSES record,
        // keeps the ones whose record +0xE0 equals the category argument, and zeroes attribute
        // 0x522 with the bonus id as its param. That one function is BOTH the load-time
        // over-limit fixup and the legitimate in-game respec, so gutting its loop (the old
        // patch branched over it at 0x7F7230, and over the pool free at 0x7F72B0) disabled
        // respeccing outright -- points went back where they were, because nothing ever
        // cleared them. It also leaked: 0x6CB780 allocates the record list before the loop and
        // the skipped block at 0x7F72B4 is what returns it to the pool.
        //
        // Cut it at the CALLERS instead. Four call it; only two are the fixup:
        //
        //     0x7F6894  per-bonus:    spend > GetParagonBonusLimit(0x5271F0) -> wipe category
        //     0x7F6954  per-category: spent > pool capacity                  -> wipe category
        //     0x7F71C4  internal helper, reached from the respec path
        //     0x7F74C4  the respec message handler -- resolves the ACD from a 16-bit handle
        //               (0x360 stride), resets, then 0x7F6F30 + 0x7F67C0 and returns 1
        //
        // NOP the first two and an over-budget save survives loading, which is the whole point
        // of the setting, while the last two keep working and respec is live. Both call sites
        // discard the return value and reload every register they use afterwards, so a NOP is
        // safe at each.
        if (global_config.rare_cheats.paragon_no_reset) {
            jest.Patch<dword>(PatchTable("patch_paragon_noreset_validate_01"), make_bytes(0x1F, 0x20, 0x03, 0xD5));
            jest.Patch<dword>(PatchTable("patch_paragon_noreset_validate_02"), make_bytes(0x1F, 0x20, 0x03, 0xD5));
            PRINT("[d3hack-custom] paragon over-limit wipe disabled at %d validation sites "
                  "(in-game respec left working)", 2)
        }

        /* d3hack-custom: experience multiplier.
           0x79FE70 stock is `mov x20, x1` (X1 = the xp amount). Replacing it with
           `add x20, xzr, x1, lsl #n` multiplies by 2^n in one instruction.
           Encoding: 0x8B010000 | (n << 10) | 0x3F4  (n=5 gives 0x8B0117F4 = x32). */
        if (global_config.rare_cheats.xp_multiplier > 1) {
            u32 shift = 0;
            while ((shift + 1u) <= 30u &&
                   (1u << (shift + 1u)) <= static_cast<u32>(global_config.rare_cheats.xp_multiplier))
                ++shift;
            const u32 word = 0x8B010000u | (shift << 10) | 0x3F4u;
            jest.Patch<dword>(PatchTable("patch_xp_multiplier"),
                              make_bytes(static_cast<u8>(word & 0xFFu), static_cast<u8>((word >> 8) & 0xFFu),
                                         static_cast<u8>((word >> 16) & 0xFFu), static_cast<u8>((word >> 24) & 0xFFu)));
            PRINT("[d3hack-custom] xp multiplier: requested %d -> applied x%u (lsl #%u)",
                  global_config.rare_cheats.xp_multiplier, 1u << shift, shift)
        }

        /* Spawn extra progress orbs */
        if (cheats.extra_gr_orbs_elites)
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_extra_gr_orbs_elites_01_movz"), reg::W3,
                                  static_cast<u16>(global_config.rare_cheats.extra_gr_orbs_count));  // d3hack-custom (was 999)

        /* Drop any item (Staff of Herding, etc) */
        if (cheats.drop_anything)
            jest.Patch<ins::Movn>(PatchTable("patch_cheat_drop_anything_01_movn"), reg::W0, 0);
        // 04000000 00504C78 12800000

        /* 100% Legendary probability (Kadala/Kanai) */
        if (cheats.guaranteed_legendaries) {
            jest.Patch<ins::Nop>(PatchTable("patch_cheat_guaranteed_legendaries_01_nop"));  // unlikely? shot in the dark
            jest.Patch<ins::Nop>(PatchTable("patch_cheat_guaranteed_legendaries_02_nop"));
            jest.Patch<ins::Branch>(PatchTable("patch_cheat_guaranteed_legendaries_03_branch"), 0x18C);
            jest.Patch<ins::Nop>(PatchTable("patch_cheat_guaranteed_legendaries_04_nop"));
            jest.Patch<ins::Branch>(PatchTable("patch_cheat_guaranteed_legendaries_05_branch"), 0x350);
        }
        // jest.Patch<dword>(PatchTable("patch_cheat_guaranteed_legendaries_04_nop"), make_bytes(0xD4, 0x00, 0x00, 0x14));
        // 04000000 0088F9E0 140000D4

        /* Instant town portal & Book of Cain */
        if (cheats.instant_portal)
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_instant_portal_01_movz"), reg::W24, 0);
        // 04000000 009E3250 52800018

        /* Show testing art cosmetics */
        // jest.Patch<ins::CmnImmediate>(PatchTable("patch_cheat_testing_cosmetics_01_cmn_imm"), reg::W2, 0);
        // 04000000 004FFFE4 3100081F

        /* ►Primal ancient probability 100% */
        // jest.Patch<ins::Branch>(PatchTable("patch_cheat_primal_01_branch"), 0x360);
        // jest.Patch<ins::Movz>(PatchTable("patch_cheat_primal_02_movz"), reg::W8, 2);
        // jest.Patch<dword>(PatchTable("patch_cheat_primal_03_bytes"), make_bytes(0x40, 0x21, 0x2A, 0x1E));
        // jest.Patch<dword>(PatchTable("patch_cheat_primal_04_bytes"), make_bytes(0x00, 0x20, 0x20, 0x1E));  // covered by reg::W8, 2
        // jest.Patch<dword>(PatchTable("patch_cheat_primal_05_bytes"), make_bytes(0x1F, 0x20, 0x03, 0xD5));
        /*
        04000000 0088DF10 1E2A2140
        04000000 0088DFFC 1E202000
        04000000 0088DFE0 D503201F
        */

        /* ►No cooldown */
        if (cheats.no_cooldowns) {
            jest.Patch<dword>(PatchTable("patch_cheat_no_cooldowns_01_bytes"), make_bytes(0xE0, 0x03, 0x27, 0x1E));
            jest.Patch<dword>(PatchTable("patch_cheat_no_cooldowns_02_bytes"), make_bytes(0xE0, 0x03, 0x00, 0x2A));
            jest.Patch<dword>(PatchTable("patch_cheat_no_cooldowns_03_bytes"), make_bytes(0xE8, 0x03, 0x27, 0x1E));
            jest.Patch<dword>(PatchTable("patch_cheat_no_cooldowns_04_bytes"), make_bytes(0xE8, 0x03, 0x27, 0x1E));
        }
        /*
        04000000 007396D0 1E2703E0
        04000000 007F8960 2A0003E0
        04000000 009BC53C 1E2703E8
        04000000 009BC828 1E2703E8
        */

        /* ►Instantly identify items */
        if (cheats.instant_craft_actions)
            jest.Patch<ins::Nop>(PatchTable("patch_cheat_instant_identify_01_nop"));
        // 04000000 0020636C D503201F

        /* ►Instantly craft items */
        if (cheats.instant_craft_actions)
            jest.Patch<ins::AddImmediate>(PatchTable("patch_cheat_instant_craft_01_add_imm"), reg::W8, reg::W8, 5);
        // 04000000 0040F9B0 11001508

        /* ►Instantly enchant items */
        if (cheats.instant_craft_actions)
            jest.Patch<ins::AddImmediate>(PatchTable("patch_cheat_instant_enchant_01_add_imm"), reg::W8, reg::W8, 1);
        // 04000000 001DCFA4 11000508

        /* ►Instantly craft Kanai's Cube items */
        if (cheats.instant_craft_actions) {
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_cube_instant_craft_01_movz"), reg::W8, 0);
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_cube_instant_craft_02_movz"), reg::W9, 0);
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_cube_instant_craft_03_movz"), reg::W10, 0);
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_cube_instant_craft_04_movz"), reg::W8, 0);
        }
        /*
        04000000 00236D20 52800008
        04000000 00236D24 52800009
        04000000 00236D28 5280000A
        04000000 00236D30 52800008
        */

        /* ►Kanai's Cube does not consume materials */
        if (cheats.cube_no_consume)
            jest.Patch<ins::Ret>(PatchTable("patch_cheat_cube_no_consume_01_ret"));
        // 04000000 008874D0 D65F03C0

        /* ►Greater Rift Lv. 150 after clearing once */
        // jest.Patch<dword>(PatchTable("patch_cheat_gr150_01_bytes"), make_bytes(0xF5, 0x03, 0x00, 0x2A));
        // 04000000 004873E4 2A0003F5

        /* ►Legendary Gem Upgrade 100% */
        if (cheats.gem_upgrade_always)
            jest.Patch<dword>(PatchTable("patch_cheat_gem_upgrade_01_bytes"), make_bytes(0x08, 0x10, 0x2E, 0x1E));
        // 04000000 006A255C 1E2E1008*/

        /* ►Legendary Gem Upgrade Speed */
        if (cheats.gem_upgrade_speed)
            jest.Patch<dword>(PatchTable("patch_cheat_gem_speed_01_bytes"), make_bytes(0x02, 0x10, 0x2E, 0x1E));
        // 04000000 00349B98 1E2E1002

        /* ►Legendary Gem Lv. 150 after upgrading once */
        /*
        mov w22, w0
        movz w0, #0x96
        sub w23, w0, w22
        */
        // jest.Patch<ins::Movk>(PatchTable("patch_cheat_gem_lvl150_01_bytes"), reg::W22, reg::W0);  // MOV LIKE THIS?
        // d3hack-custom: LegendaryGemUncapped -> patch the gem's max-rank load to MOV W20, #-1.
        // 0x7DF95C does `tbnz w20, #0x1f` and skips the cap when the max is negative, so this
        // takes the game's own built-in "no limit" path instead of forcing a fixed rank.
        if (cheats.gem_uncapped) {
            // Three sites load the gem's max rank from [x0+0x2F4] and interpret it differently:
            //   0x7DF944 apply      : `tbnz w20,#31` -> negative means "no cap"
            //   0x1F16C8 canUpgrade : `cmp w0,w20; cset lt` -> needs max > rank
            //   0x243B20 isMaxed    : `tbz w20,#31` -> negative takes the FAIL path
            // A large positive (0xFFFF) is correct for all three; -1 only worked for the first.
            // MOVZ W20, #imm16 -- max rank now comes from LegendaryGemMaxLevel.
            const u32 gemMax  = (global_config.rare_cheats.gem_max_level > 0 &&
                                 global_config.rare_cheats.gem_max_level <= 65535)
                                    ? static_cast<u32>(global_config.rare_cheats.gem_max_level)
                                    : 65535u;
            const u32 gemWord = 0x52800000u | (gemMax << 5) | 20u;
            const auto gemB   = make_bytes(static_cast<u8>(gemWord & 0xFFu), static_cast<u8>((gemWord >> 8) & 0xFFu),
                                           static_cast<u8>((gemWord >> 16) & 0xFFu), static_cast<u8>((gemWord >> 24) & 0xFFu));
            jest.Patch<dword>(PatchTable("patch_gem_uncap_maxrank_movn"), gemB);
            jest.Patch<dword>(PatchTable("patch_gem_canupgrade_maxrank"), gemB);
            jest.Patch<dword>(PatchTable("patch_gem_ismaxed_maxrank"), gemB);
            // 0x6A2518 `cmp w0, #0x95` / 0x6A251C `b.gt` returns 0.0 chance for any rank > 149.
            // NOP the branch so the normal (rank - riftLevel - 1) chance tables apply at any rank.
            jest.Patch<ins::Nop>(PatchTable("patch_gem_chance_rank_gate_nop"));
            // d3hack-custom: gem upgrade chance floor above a chosen greater-rift tier.
            //
            // The chance fn picks its table at 0x6A252C with `add x9, x9, #0x9b8`, w19
            // holding the rift level and w0 the jewel rank; it then indexes
            // table_A[rank - level - 1] once the rank runs ahead of the rift, whose tail
            // is 1% and whose last slot is a flat 0.0 -- a hard stop on gem progress.
            //
            // Replace that one instruction with a call to a stub in the .text/.rodata
            // padding that hands back a FLOORED copy of table_A at or above the
            // configured tier, and the stock table below it. Stock table_A is never
            // touched, so ordinary play keeps the original 60/30/15/8/4/2/1/0 curve.
            //
            // NOTE the rift level in w19 is ZERO-BASED: a GR 200 arrives as 199. That is
            // also why the game's index is `rank - level - 1` rather than `rank - level`.
            // The compare immediate is therefore (displayed tier - 1), and the branch is
            // b.lt so that the configured tier itself is included.
            {
                const int  nFloorGR   = global_config.rare_cheats.gem_floor_min_gr;
                const u32  nCmpImm    = static_cast<u32>(nFloorGR > 0 ? nFloorGR - 1 : 0) & 0xFFFu;
                const float flFloor   = static_cast<float>(global_config.rare_cheats.gem_floor_percent) / 100.0f;

                // cmp w19, #imm / b.lt +0xC / adr x9, #16 / ret / add x9,x9,#0x9b8 / ret
                const u32 arStub[6] = {0x71000000u | (nCmpImm << 10) | (19u << 5) | 31u,
                                       0x5400006Bu, 0x10000089u, 0xD65F03C0u, 0x9126E129u, 0xD65F03C0u};
                static const char *s_arStubKeys[6] = {"patch_gemsel_stub_0", "patch_gemsel_stub_1",
                                                      "patch_gemsel_stub_2", "patch_gemsel_stub_3",
                                                      "patch_gemsel_stub_4", "patch_gemsel_stub_5"};
                for (int i = 0; i < 6; ++i)
                    jest.Patch<dword>(PatchTable(s_arStubKeys[i]), make_dword(arStub[i]));

                // Stock table_A @0xEC99B8, every entry lifted to at least the floor.
                static const float s_arStock[17] = {0.60f, 0.30f, 0.15f, 0.08f, 0.04f, 0.02f,
                                                    0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f,
                                                    0.01f, 0.01f, 0.01f, 0.01f, 0.00f};
                static const char *s_arTblKeys[17] = {
                    "patch_gemsel_tbl_00", "patch_gemsel_tbl_01", "patch_gemsel_tbl_02", "patch_gemsel_tbl_03",
                    "patch_gemsel_tbl_04", "patch_gemsel_tbl_05", "patch_gemsel_tbl_06", "patch_gemsel_tbl_07",
                    "patch_gemsel_tbl_08", "patch_gemsel_tbl_09", "patch_gemsel_tbl_10", "patch_gemsel_tbl_11",
                    "patch_gemsel_tbl_12", "patch_gemsel_tbl_13", "patch_gemsel_tbl_14", "patch_gemsel_tbl_15",
                    "patch_gemsel_tbl_16"};
                for (int i = 0; i < 17; ++i) {
                    const float flValue = s_arStock[i] > flFloor ? s_arStock[i] : flFloor;
                    u32         uBits   = 0;
                    __builtin_memcpy(&uBits, &flValue, sizeof(uBits));
                    jest.Patch<dword>(PatchTable(s_arTblKeys[i]), make_dword(uBits));
                }

                jest.Patch<dword>(PatchTable("patch_gemsel_call"), make_bytes(0x65, 0x41, 0x17, 0x94));
                PRINT("[d3hack-custom] gem upgrade chance floored at %d%% from GR %d (w19 >= %d)",
                      global_config.rare_cheats.gem_floor_percent, nFloorGR, static_cast<int>(nCmpImm))
            }

            // 0x887188 `cmp w23,w0` / 0x88718C `b.ge` bails out when
            // JEWEL_UPGRADES_USED (attr 0x580) >= JEWEL_UPGRADES_MAX (attr 0x581).
            // This fires BEFORE the chance function is called, so it gates everything.
            jest.Patch<ins::Nop>(PatchTable("patch_gem_upgrades_used_gate_nop"));
            // 0x500D98 picks the gem's max rank for the item load/sync path, then
            // 0x500EB0..0x500EBC does `value = (max >= 0) ? min(rank, max) : rank`.
            // That is what reverted an upgraded gem back to 150 on reload. Forcing the
            // max to 0xFFFF makes the clamp below a no-op.
            jest.Patch<dword>(PatchTable("patch_gem_rank_persist_clamp"), make_bytes(0xF7, 0xFF, 0x9F, 0x52));
            PRINT("[d3hack-custom] legendary gem uncapped (max-rank=%d; chance, upgrades-used and persist clamps removed)", 0xFFFF)
        } else if (cheats.gem_upgrade_lvl150) {
            jest.Patch<dword>(PatchTable("patch_cheat_gem_lvl150_01_bytes"), make_bytes(0xF6, 0x03, 0x00, 0x2A));
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_gem_lvl150_02_movz"), reg::W0,
                                  static_cast<u16>(global_config.rare_cheats.gem_max_level));
            // jest.Patch<Sub>(PatchTable("patch_cheat_gem_lvl150_03_bytes"), reg::W23, reg::W0, reg::W22);
            jest.Patch<dword>(PatchTable("patch_cheat_gem_lvl150_03_bytes"), make_bytes(0x17, 0x00, 0x16, 0x4B));
            jest.Patch<ins::Ret>(PatchTable("patch_cheat_gem_lvl150_04_ret"));
            jest.Patch<dword>(PatchTable("patch_cheat_gem_lvl150_05_bytes"), make_bytes(0x89, 0x4D, 0x12, 0x94));
        }

        // d3hack-custom: let Ramaladni's Gift target anything equippable, boots included.
        //
        // 0x4F6C90 is CanAddSocketsToItem(target, consumable). It walks the consumable's
        // AddSocketsType_* flags and validates the target against each category:
        //
        //     w20 = 0xFFFFF187 (SOCKETS)     <- the attribute key it builds neighbours from
        //     +0x1A -> 0x1A1 AddSocketsType_Weapon
        //     +0x1B -> 0x1A2 AddSocketsType_Offhand
        //     +0x1D -> 0x1A4 AddSocketsType_Chest
        //     +0x17 -> 0x19E Unidentified (checked on the target)
        //
        // That neighbour-offset addressing is why a scan for literal AddSocketsType_* keys
        // finds ZERO sites -- only SOCKETS itself is ever materialised.
        //
        // It returns -1 for "valid target" and a GAMEERROR code otherwise; the use handler
        // 0x7DF7F0 does `cmn w0,#1 / b.eq` and only proceeds on -1. So MOVN W0,#0 + RET at
        // the entry accepts everything. Patching the entry is safe: it lands before the
        // prologue pushes anything, so returning early leaves the stack untouched.
        //
        // The other caller, 0x4F678C, is the can-I-use-this predicate the UI asks, so the
        // same patch also makes the console's Add Sockets button offer up on boots.
        // The byte patch that used to live here (MOVN W0,#0 / RET at 0x4F6C90) is gone: the
        // CanAddSockets trampoline in hooks/util.hpp does the same job and can also log, and
        // a hook and a byte patch cannot share an address.
        //
        // d3hack-custom: the Gift's UI target list is filtered by `ItemIsSocketable` at
        // 0x4F6BE0, NOT by the gate at 0x4F6C90 -- a trampoline on the gate logged ZERO calls
        // while the list was open, which ruled that whole path out.
        //
        // 0x4F6BE0 takes an item ACD, checks it is an item (+0x38 == 2), reads its GBID from
        // +0x3C, then tests six item-category flags through 0x4F44F0:
        //
        //     0x1B (27)  0x30 (48)  0x1C (28)  0x1D (29)  0x32 (50)  0x1E (30)
        //
        // Any one set -> 0x4F6C60, which returns "identified?" as the result. None set ->
        // 0x4F6C80, return 0. Those six are the Weapon/Offhand/Legs/Chest/Helm/Jewelry
        // categories, and stock data only puts weapons in reach of the Gift.
        //
        // Patch the first flag test into `B 0x4F6C60`, which skips all six and falls into the
        // identified check. The is-an-item guard and the is-identified guard both survive --
        // only the category restriction is dropped.
        //
        // There are SEVERAL copies of the six-flag category check (0x4F6BE0, 0x5018C0, and
        // more near 0x1A1A0C / 0x1FAC98 / 0x201C5C). Bypassing 0x4F6BE0 changed nothing, and
        // the flag test kept being queried anyway -- logging `__builtin_return_address(0)`
        // from a hook on 0x4F44F0 named the real one outright: every category query while the
        // target list was open came from **0x5018EC..0x50193C**, i.e. the copy at 0x5018C0.
        //
        // 0x5018C0 has the same shape: is-an-item check (+0x38 == 2), gbid from +0x3C, six
        // flag tests, any set -> 0x501940, none -> 0x50197C (reject). Patch the first test
        // into `B 0x501940` so all six are skipped and the is-an-item guard survives.
        if (cheats.rama_any_item) {
            jest.Patch<dword>(PatchTable("patch_rama_listfilter_skip"), make_bytes(0x18, 0x00, 0x00, 0x14));
            jest.Patch<dword>(PatchTable("patch_rama_socketable_skip"), make_bytes(0x18, 0x00, 0x00, 0x14));
            PRINT("[d3hack-custom] Ramaladni's Gift: category filters bypassed at 0x%X and 0x4F6BE0",
                  0x5018C0)
        }

        // d3hack-custom: Caldesann's Despair records the spent gem's rank in
        // CUBEENCHANTEDGEMRANK, and 0x5011CC clamps it to 150 on the way in:
        //     5011CC  cmp  w8, #0x96
        //     5011D0  mov  w9, #0x96
        //     5011D4  csel w22, w8, w9, lt      ; w22 = min(rank, 150)
        // 10000 is too wide for a cmp immediate, so swap the first two words --
        // `movz w9,#cap` then `cmp w8,w9`. MOVZ leaves the flags alone, the CMP sets
        // them, and the untouched CSEL clamps to the configured ceiling instead.
        // Both the stored attribute and the tooltip read this one value.
        if (cheats.cube_augment_gem_rank != 150) {
            jest.Patch<ins::Movz>(PatchTable("patch_cube_augment_rank_site0"), reg::W9,
                                  static_cast<u16>(cheats.cube_augment_gem_rank));
            jest.Patch<dword>(PatchTable("patch_cube_augment_rank_site1"), make_bytes(0x1F, 0x01, 0x09, 0x6B));
            PRINT("[d3hack-custom] Kanai's Cube augment gem rank cap: 150 -> %d", cheats.cube_augment_gem_rank)
        }
        /*
        04000000 00C72F7C 2A0003F6
        04000000 00C72F80 528012C0
        04000000 00C72F84 4B160017
        04000000 00C72F88 D65F03C0
        04000000 007DF958 94124D89
        */

        // [►Equip Multiple Legendary Item]
        if (cheats.equip_multi_legendary)
            jest.Patch<ins::Branch>(PatchTable("patch_cheat_multi_legendary_01_branch"), 0x5C);
        // 04000000 004FB9E4 14000017

        // [►Socket Any Gem To Any Slot]
        if (cheats.any_gem_any_slot) {
            jest.Patch<ins::Movz>(PatchTable("patch_cheat_any_gem_any_slot_01_movz"), reg::W25, 5);
            jest.Patch<ins::Branch>(PatchTable("patch_cheat_any_gem_any_slot_02_branch"), 0xA0);
        }

        // [►Auto Pickup]
        if (cheats.auto_pickup)
            jest.Patch<ins::Nop>(PatchTable("patch_cheat_auto_pickup_01_nop"));
        // 04000000 004F98DC D503201F
    }

    void PatchInfiniteMp(const bool enabled) {
        auto jest = patch::RandomAccessPatcher();
        if (enabled) {
            jest.Patch<ins::Nop>(PatchTable("patch_infinite_mp_01_resource_bl"));
            return;
        }
        jest.Patch<dword>(PatchTable("patch_infinite_mp_01_resource_bl"), k_infinite_mp_restore_call);
    }

    // d3hack-custom -------------------------------------------------------------
    // Raise the Greater Rift ceiling by growing the tiered-rift-levels GameBalance
    // table. 0x51AD90 computes  maxGR = min(assetBytes / 56, 511)  from the very
    // same fields every lookup site uses:
    //     asset + 0x20C = table size in bytes
    //     asset + 0x210 = row data pointer   (56-byte records)
    // Repointing those two fields at a longer table therefore lifts the selector
    // cap, the rift-creation gate at 0x775174 and all 16 lookup sites at once --
    // no instruction patching required.
    //
    // Rows past the original end are extrapolated per-field from the ratio of the
    // last two real rows. In 2.7.6 those ratios are the designed constants
    // (HP x1.17, damage x1.023374, XP x1.03125, ...), so the curve simply continues.
    void ExtendTieredRiftTable() {
        static bool s_done = false;
        const int target = global_config.rare_cheats.max_greater_rift_level;
        if (s_done || target <= 0)
            return;
        if (GlobalSNOGet == nullptr || GBAssetGet == nullptr)
            return;

        auto **pp = *reinterpret_cast<void ***>(GameOffsetFromTable("gb_asset_mgr_ptr"));
        if (pp == nullptr)
            return;
        void *ptMgr = *pp;
        if (ptMgr == nullptr)
            return;

        auto *asset = reinterpret_cast<u8 *>(GBAssetGet(ptMgr, GlobalSNOGet(static_cast<SNO>(0x401))));
        if (asset == nullptr)
            return;

        constexpr u32 REC = 56u;
        auto &nBytes = *reinterpret_cast<u32 *>(asset + 0x20C);
        auto &pData  = *reinterpret_cast<u8 **>(asset + 0x210);
        if (pData == nullptr || nBytes < (2u * REC))
            return;

        const u32 rows = nBytes / REC;
        const u32 want = static_cast<u32>(target);
        if (rows >= want || want > 511u)
            return;

        auto *tNew = new (std::nothrow) u8[static_cast<size_t>(want) * REC];
        if (tNew == nullptr)
            return;
        memcpy(tNew, pData, static_cast<size_t>(rows) * REC);

        const u8 *pA = pData + static_cast<size_t>(rows - 2) * REC;
        const u8 *pB = pData + static_cast<size_t>(rows - 1) * REC;

        float fRatio[8];
        for (int j = 0; j < 8; ++j) {
            float a = 0.0f;
            float b = 0.0f;
            memcpy(&a, pA + (j * 4), 4);
            memcpy(&b, pB + (j * 4), 4);
            fRatio[j] = (a != 0.0f) ? (b / a) : 1.0f;
        }
        s64 lA = 0;
        s64 lB = 0;
        memcpy(&lA, pA + 40, 8);
        memcpy(&lB, pB + 40, 8);
        const double lRatio = (lA != 0) ? (static_cast<double>(lB) / static_cast<double>(lA)) : 1.0;

        for (u32 i = rows; i < want; ++i) {
            u8       *cur = tNew + (static_cast<size_t>(i) * REC);
            const u8 *pre = tNew + (static_cast<size_t>(i - 1) * REC);
            memcpy(cur, pre, REC);  // ints and constant fields carry over verbatim
            for (int j = 0; j < 8; ++j) {
                float v = 0.0f;
                memcpy(&v, pre + (j * 4), 4);
                v *= fRatio[j];
                memcpy(cur + (j * 4), &v, 4);
            }
            s64 lv = 0;
            memcpy(&lv, pre + 40, 8);
            lv = static_cast<s64>(static_cast<double>(lv) * lRatio);
            memcpy(cur + 40, &lv, 8);
        }

        pData  = tNew;
        nBytes = want * REC;
        s_done = true;
        PRINT("[d3hack-custom] GR table extended: %u -> %u rows (max GR now %u)", rows, want, want)
    }

    // d3hack-custom -------------------------------------------------------------
    // Paragon XP table (GlobalSNO 0x3F5), 128-byte records, 20000 rows.
    // Eleven of the twelve readers clamp the index to 19999, but 0x4C4118 does a raw
    // `sxtw x9, w20 ; add x8, x8, x9, lsl #7` with no clamp -- so once the level cap is
    // lifted, paragon 20001 reads off the end and the game dies. Growing the table keeps
    // that read in bounds. The clamped readers keep returning row 19999, so XP per level
    // simply goes flat past 20000, which is what we want anyway.
    // Layout differs from the rift table: data pointer lives at +0x50 here.
    void ExtendParagonXpTable() {
        static bool s_done = false;
        const int target = global_config.rare_cheats.max_paragon_level;
        if (s_done || target <= 20000)
            return;
        if (GlobalSNOGet == nullptr || GBAssetGet == nullptr)
            return;

        auto **pp = *reinterpret_cast<void ***>(GameOffsetFromTable("gb_asset_mgr_ptr"));
        if (pp == nullptr || *pp == nullptr)
            return;
        auto *asset = reinterpret_cast<u8 *>(GBAssetGet(*pp, GlobalSNOGet(static_cast<SNO>(0x3F5))));
        if (asset == nullptr)
            return;

        constexpr u32 REC  = 128u;
        constexpr u32 ROWS = 20000u;  // stock row count
        auto &pData = *reinterpret_cast<u8 **>(asset + 0x50);
        if (pData == nullptr)
            return;

        const u32 want = static_cast<u32>(target) + 2u;  // +2 headroom past the cap
        auto     *tNew = new (std::nothrow) u8[static_cast<size_t>(want) * REC];
        if (tNew == nullptr) {
            PRINT("[d3hack-custom] paragon xp table: alloc of %u rows FAILED", want)
            return;
        }
        memcpy(tNew, pData, static_cast<size_t>(ROWS) * REC);

        // XP-to-next lives in the leading int64; the rest of the record is int fields we
        // carry over verbatim. The stock curve is near-linear at the top, so continue it
        // by the ratio of the last two real rows.
        s64 lA = 0;
        s64 lB = 0;
        memcpy(&lA, pData + (static_cast<size_t>(ROWS - 2) * REC), 8);
        memcpy(&lB, pData + (static_cast<size_t>(ROWS - 1) * REC), 8);
        const double lRatio = (lA > 0) ? (static_cast<double>(lB) / static_cast<double>(lA)) : 1.0;

        for (u32 i = ROWS; i < want; ++i) {
            u8       *cur = tNew + (static_cast<size_t>(i) * REC);
            const u8 *pre = tNew + (static_cast<size_t>(i - 1) * REC);
            memcpy(cur, pre, REC);
            s64 lv = 0;
            memcpy(&lv, pre, 8);
            lv = static_cast<s64>(static_cast<double>(lv) * lRatio);
            memcpy(cur, &lv, 8);
        }

        pData  = tNew;
        s_done = true;
        PRINT("[d3hack-custom] paragon xp table extended: %u -> %u rows", ROWS, want)
    }

    // d3hack-custom: make Ramaladni's Gift a socket-adder for EVERY slot, not just weapons.
    //
    // The target gate at 0x4F6C90 is already forced to -1 (valid) and the UI list still shows
    // weapons only, so the list is not built from that gate. Both the inventory caller
    // (0x36D048) and the confirmation path (0x3E4D6C, beside the AddSocketsConfirmation
    // string) do go through the item-action API 0x4F60B0 and treat -1 as valid -- so the USE
    // path already accepts anything. Only the target list is filtered, and it must be reading
    // the Gift's own AddSocketsType_* flags rather than asking the gate.
    //
    // So stop bypassing readers and change what they read. The AddSocketsType_* attributes
    // have no literal-key writer anywhere in the binary, which means they come from the item
    // record's attribute-specifier list. Find that list the way 0x187 was found in the affix
    // records -- scan the record for the attribute ids themselves:
    //
    //     0x1A0 CONSUMABLEADDSOCKETS      0x1A1 ADDSOCKETSTYPE_WEAPON
    //     0x1A2 OFFHAND   0x1A3 LEGS   0x1A4 CHEST   0x1A5 HELM   0x1A6 JEWELRY
    //
    // Two ids at known offsets give the specifier stride, and the entry for 0x1A1 gives a
    // template to clone into free slots for the other five. Writes are gated on the layout
    // being unambiguous; otherwise this only logs, so a bad guess cannot corrupt the record.
    void PatchRamaladniTargetTypes() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.rama_any_item)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;
        s_done = true;

        static const char *szGift = "Consumable_Add_Sockets_1";
        s32                h      = 0;
        for (const char *p = szGift; *p != 0; ++p) {
            char c = *p;
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
            h = (h << 5) + h + static_cast<s32>(c);
        }
        struct {
            s32 eType;
            s32 gbid;
        } tKey {0x2, h};
        u8    tOut[16] {};
        s32   fFlag = 1;
        auto *pRec = reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut), &fFlag));
        if (pRec == nullptr) {
            PRINT("[d3hack-custom] gift: no record for gbid %08X", static_cast<u32>(h))
            return;
        }

        // where do the AddSockets attribute ids sit inside the record?
        s32 offConsumable = -1;
        s32 offWeapon     = -1;
        int nSeen         = 0;
        for (u32 off = 0; off <= 0x800u; off += 4u) {
            const s32 v = *reinterpret_cast<const s32 *>(pRec + off);
            if (v < 0x1A0 || v > 0x1A6)
                continue;
            ++nSeen;
            PRINT("[d3hack-custom] gift +%03X = attribute 0x%X", off, static_cast<u32>(v))
            if (v == 0x1A0 && offConsumable < 0)
                offConsumable = static_cast<s32>(off);
            if (v == 0x1A1 && offWeapon < 0)
                offWeapon = static_cast<s32>(off);
        }
        if (offConsumable < 0 || offWeapon < 0) {
            PRINT("[d3hack-custom] gift: specifier ids not found (%d seen)", nSeen)
            return;
        }

        const s32 nStride = offWeapon - offConsumable;
        if (nStride <= 0 || nStride > 0x40 || (nStride & 3) != 0) {
            PRINT("[d3hack-custom] gift: implausible specifier stride %d, not writing", nStride)
            return;
        }
        PRINT("[d3hack-custom] gift: specifiers at +%03X, stride %d", offConsumable, nStride)

        // Clone the AddSocketsType_Weapon specifier into free slots for the other five
        // target categories. A slot counts as free only if its attribute id reads -1 or 0.
        int nAdded = 0;
        for (s32 want = 0x1A2; want <= 0x1A6; ++want) {
            bool bHave = false;
            for (s32 k = 0; k < 24; ++k) {
                const s32 v = *reinterpret_cast<const s32 *>(pRec + offConsumable + (k * nStride));
                if (v == want)
                    bHave = true;
            }
            if (bHave)
                continue;
            for (s32 k = 0; k < 24; ++k) {
                const s32 off = offConsumable + (k * nStride);
                if (off < 0 || off + nStride > 0x800)
                    break;
                const s32 v = *reinterpret_cast<const s32 *>(pRec + off);
                if (v != -1 && v != 0)
                    continue;
                memcpy(pRec + off, pRec + offWeapon, static_cast<size_t>(nStride));
                *reinterpret_cast<s32 *>(pRec + off) = want;
                ++nAdded;
                break;
            }
        }
        PRINT("[d3hack-custom] gift: %d target-type specifiers added", nAdded)
    }

    // d3hack-custom: locate the ShowItemsOnGround preference.
    //
    // Ground item NAMES are culled by distance on this build. The in-game "item labels" toggle
    // only switches names<->icons (the `ItemTagsAsIcons` pref), which is not what is wanted.
    // On PC the same feature is an option that shows item names permanently, and this build
    // does register the matching preference:
    //
    //     0xE4CF51 "ShowItemsOnGround"   registered at 0x27CE50, in the same prefs table as
    //     ItemTagsAsIcons / InventoryZoom / LimitBackgroundFPS
    //     0xE7632F "UI_ToggleItemsOnGround"  + action_binding_toggleitemsonground
    //
    // No named radius string exists anywhere in rodata, so the cutoff is a hardcoded constant
    // -- but if this preference is what gates "show them all", setting it beats hunting the
    // constant.
    //
    // Find its descriptor by scanning the module's writable data for the string POINTER, then
    // log the surrounding qwords so the value field can be identified. Read-only and bounded.
    void ProbeShowItemsOnGround() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.item_socket_probe)
            return;
        s_done = true;

        const uintptr_t uBase = GameOffset(0);
        const uintptr_t uName = GameOffset(0xE4CF51);  // "ShowItemsOnGround"
        const uintptr_t uIcon = GameOffset(0xE1283C);  // "ItemTagsAsIcons", a known sibling
        PRINT("[d3hack-diag] ground: base=%p name=%p icons=%p", reinterpret_cast<void *>(uBase),
              reinterpret_cast<void *>(uName), reinterpret_cast<void *>(uIcon))

        // scan the module's data range for qwords equal to either string pointer
        const uintptr_t uStart = GameOffset(0x1000000);
        const uintptr_t uEnd   = GameOffset(0x1200000);
        int             nHit   = 0;
        for (uintptr_t p = uStart; p + 8 <= uEnd && nHit < 12; p += 8) {
            const uintptr_t v = *reinterpret_cast<const uintptr_t *>(p);
            if (v != uName && v != uIcon)
                continue;
            ++nHit;
            PRINT("[d3hack-diag] ground: \"%s\" descriptor at %06X",
                  (v == uName) ? "ShowItemsOnGround" : "ItemTagsAsIcons",
                  static_cast<u32>(p - uBase))
            for (int k = -4; k <= 6; ++k) {
                const uintptr_t q = p + (k * 8);
                if (q < uStart || q + 8 > uEnd)
                    continue;
                PRINT("[d3hack-diag] ground:   [%+d] %016llX", k,
                      static_cast<unsigned long long>(*reinterpret_cast<const uintptr_t *>(q)))
            }
        }
        PRINT("[d3hack-diag] ground: %d descriptors found", nHit)
    }

    // d3hack-custom: dump the named-function registry that holds the item predicates.
    //
    // The Gift's list is "weapons WITHOUT sockets" (user-observed). ItemHasNoSockets at
    // 0x4F8790 (returns SOCKETS == 0) is exactly the second half of that, and it has ZERO
    // direct callers -- it is referenced only as a function POINTER, from a triplet table in
    // rodata at 0xCA9868:
    //
    //     [tag = 0x403][fn][dataptr]   24 bytes per entry
    //     ... 0x5047F0 / 0x1152FC8, 0x4F8790 / 0x1152FD0, 0x5048F0 / 0x1152FD8 ...
    //
    // The same table shape holds UI panel constructors (0xCA9748 -> 0x373570), so this is a
    // registry of NAMED functions and the sequential dataptrs are almost certainly the
    // names. If the target list is defined in UI data that references predicates by name,
    // that is why ten code-level patches have not moved it.
    //
    // So read the registry and log name -> function. That gives the predicate vocabulary
    // outright, including whatever means "is a weapon" -- and a registered predicate can
    // simply be pointed at a permissive one, no UI data editing needed.
    //
    // Read-only, bounded, and every dereference is range-checked.
    void DumpPredicateRegistry() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.item_socket_probe)
            return;
        s_done = true;

        struct Local {
            static bool Ok(uintptr_t u) {
                return u >= 0x1000ull && u < 0x0000800000000000ull;
            }
            static void Ascii(char *szOut, int nCap, const void *pv) {
                int i = 0;
                if (Ok(reinterpret_cast<uintptr_t>(pv))) {
                    const char *p = reinterpret_cast<const char *>(pv);
                    for (; i < nCap - 1; ++i) {
                        const char c = p[i];
                        if (c == 0)
                            break;
                        szOut[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
                    }
                }
                szOut[i] = 0;
            }
        };

        const uintptr_t uBase = GameOffset(0);
        // walk a window of the table around the known entries
        const uintptr_t uStart = GameOffset(0xCA9400);
        const uintptr_t uEnd   = GameOffset(0xCA9C00);
        int             nShown = 0;
        for (uintptr_t p = uStart; p + 24 <= uEnd && nShown < 90; p += 8) {
            const u64 tag = *reinterpret_cast<const u64 *>(p);
            if (tag != 0x403ull)
                continue;
            const u64 fn  = *reinterpret_cast<const u64 *>(p + 8);
            const u64 dat = *reinterpret_cast<const u64 *>(p + 16);
            if (fn == 0 || fn >= 0xC72A90ull)
                continue;
            char szName[64];
            szName[0] = 0;
            // the dataptr is a module address; the name may be there directly or behind it
            if (Local::Ok(dat)) {
                const uintptr_t uDat = uBase + dat;
                Local::Ascii(szName, sizeof(szName), reinterpret_cast<const void *>(uDat));
                if (szName[0] == 0) {
                    const void *pInner = *reinterpret_cast<void *const *>(uDat);
                    Local::Ascii(szName, sizeof(szName), pInner);
                }
            }
            ++nShown;
            PRINT("[d3hack-diag] registry %06X fn=%06X \"%s\"", static_cast<u32>(p - uBase),
                  static_cast<u32>(fn), szName)
        }
        PRINT("[d3hack-diag] registry: %d entries shown", nShown)
    }

    // d3hack-custom: mark every worn item as socket-consumable-eligible.
    //
    // Nine attempts to find what filters Ramaladni's target list all failed (0x4F6C90 and
    // 0x4F60B0 are never called; 0x4F6BE0 and 0x5018C0 bypassed with no effect; the 0x1F2740
    // filter kinds are inventory tabs; 0x1F29C0 is a classifier; 0x4F38xx is a tooltip
    // builder; the 0x373570 panel is a widget factory). So stop hunting the reader and make
    // the DATA true instead -- the same move that finally worked for the socket affixes.
    //
    // 0x4F44F0(gbid, bit) is the item flag test, and 0x4F6BE0 shows the six socket-category
    // bits: 0x1B 0x30 0x1C 0x1D 0x32 0x1E. Whatever function filters the list, if it asks
    // this it gets a yes. The lookup path is all visible in 0x4F44F0:
    //
    //     mgr     = *(*(u64*)0x114A840 + 0x2210)      the same manager the affix cache uses
    //     mask    = *(u32*)(mgr + 0x98)
    //     buckets = *(u64**)(mgr + 0xA8)
    //     h       = FNV1a over the 4 bytes of gbid   (0x811C9DC5 / 0x01000193)
    //     entry   = buckets[mask & h]; walk [entry] until *(u32*)(entry+8) == gbid
    //     flags   = *(u64*)(*(u64*)(entry + 0x10) + 0x28)   -- 4 x u32, 128 bits
    //
    // Bits 27..30 land in word 0, bits 48/50 in word 1.
    //
    // Which items count as worn is decided by the item's ItemType, and the offset of the
    // type GBID inside an item record is SELF-CALIBRATED here rather than guessed: four items
    // whose type is known are scanned for the offset holding that type's GBID, and only an
    // offset that agrees for all four is used. If calibration fails the function logs and
    // writes nothing -- guessing a record offset is what crashed things earlier today.
    // Filled by PatchItemSocketCategoryFlags, consumed by the FreeSockets hook. Sorted, so
    // the hook can binary-search it instead of re-deriving an item's type on a hot path.
    std::vector<u32> g_arEquippableGbids;

    void PatchItemSocketCategoryFlags() {
        // NOTE: this builds g_arEquippableGbids, which the FreeSockets hook depends on, so it
        // must run whenever EITHER feature is enabled. Gating it on rama_any_item alone would
        // silently disable free sockets when someone turned the Gift patch off.
        static bool s_done = false;
        const bool  bFlags = global_config.rare_cheats.rama_any_item;
        const bool  bNeed  = bFlags || global_config.rare_cheats.free_sockets > 0;
        if (s_done || !bNeed)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        auto *pRootSlot = reinterpret_cast<u8 **>(GameOffset(0x114A840));
        if (pRootSlot == nullptr || reinterpret_cast<uintptr_t>(*pRootSlot) < 0x1000ull)
            return;
        u8 *pMgr = *reinterpret_cast<u8 **>(*pRootSlot + 0x2210);
        if (reinterpret_cast<uintptr_t>(pMgr) < 0x1000ull)
            return;
        s_done = true;

        struct Local {
            static s32 Djb2(const char *p) {
                s32 h = 0;
                for (; *p != 0; ++p) {
                    char c = *p;
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                    h = (h << 5) + h + static_cast<s32>(c);
                }
                return h;
            }
            static u8 *Rec(s32 eType, s32 gbid) {
                struct {
                    s32 e;
                    s32 g;
                } k {eType, gbid};
                u8  o[16] {};
                s32 f = 1;
                return reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            }
        };

        // --- calibrate: where does an item record keep its ItemType gbid? --------------
        struct Known {
            const char *szItem;
            u32         gbidType;
        };
        static const Known arKnown[] = {
            {"Unique_Boots_Set_15_x1", 0x072C2707},  // Boots
            {"Unique_Helm_009_x1", 0x003AC366},      // Helm
            {"unique_ring_107_x1", 0x00405070},      // Ring
            {"x1_Amulet_norm_unique_25", 0xEA3AD528},// Amulet
        };
        constexpr u32 SCAN = 0x400;
        static bool   arOk[SCAN / 4];
        for (u32 i = 0; i < SCAN / 4; ++i)
            arOk[i] = true;
        int nUsable = 0;
        for (const auto &k : arKnown) {
            u8 *pRec = Local::Rec(0x2, Local::Djb2(k.szItem));
            if (pRec == nullptr)
                continue;
            ++nUsable;
            for (u32 o = 0; o < SCAN; o += 4)
                if (*reinterpret_cast<const u32 *>(pRec + o) != k.gbidType)
                    arOk[o / 4] = false;
        }
        s32 nOffType = -1;
        for (u32 i = 0; i < SCAN / 4; ++i)
            if (arOk[i]) {
                nOffType = static_cast<s32>(i * 4);
                break;
            }
        if (nUsable < 2 || nOffType < 0) {
            PRINT("[d3hack-custom] socket flags: calibration failed (%d items usable)", nUsable)
            return;
        }
        PRINT("[d3hack-custom] socket flags: item record ItemType gbid at +%03X (%d items agreed)",
              nOffType, nUsable)

        // --- the set of worn item types ----------------------------------------------
        constexpr u32 MAXW = 256;
        static u32    arWorn[MAXW];
        u32           nWorn = 0;
        std::vector<GBID> typeIds;
        AllGBIDsOfType(static_cast<GameBalanceType>(0x1), typeIds);
        for (GBID tg : typeIds) {
            if (nWorn >= MAXW)
                break;
            u8 *pTR = Local::Rec(0x1, static_cast<s32>(tg));
            if (pTR == nullptr || *reinterpret_cast<const s32 *>(pTR + 0x2C) < 0)
                continue;
            arWorn[nWorn++] = *reinterpret_cast<const u32 *>(pTR + 0x10);
        }
        if (nWorn == 0)
            return;

        // --- flag every worn item ------------------------------------------------------
        const u32   uMask    = *reinterpret_cast<const u32 *>(pMgr + 0x98);
        auto      **ppBucket = *reinterpret_cast<u8 ***>(pMgr + 0xA8);
        if (reinterpret_cast<uintptr_t>(ppBucket) < 0x1000ull)
            return;

        std::vector<GBID> itemIds;
        AllGBIDsOfType(static_cast<GameBalanceType>(0x2), itemIds);
        int nHit = 0;
        int nSet = 0;
        for (GBID ig : itemIds) {
            u8 *pRec = Local::Rec(0x2, static_cast<s32>(ig));
            if (pRec == nullptr)
                continue;
            const u32 gType = *reinterpret_cast<const u32 *>(pRec + nOffType);
            bool      bWorn = false;
            for (u32 w = 0; w < nWorn; ++w)
                if (arWorn[w] == gType)
                    bWorn = true;
            if (!bWorn)
                continue;
            ++nHit;

            // FNV1a over the four bytes of the gbid, exactly as 0x4F44F0 does
            const u32 g = static_cast<u32>(ig);
            u32       h = ((g & 0xFFu) ^ 0x811C9DC5u) * 0x01000193u;
            h           = (h ^ ((g >> 8) & 0xFFu)) * 0x01000193u;
            h           = (h ^ ((g >> 16) & 0xFFu)) * 0x01000193u;
            h           = (h ^ (g >> 24)) * 0x01000193u;

            u8 *pEntry = ppBucket[uMask & h];
            while (reinterpret_cast<uintptr_t>(pEntry) >= 0x1000ull &&
                   *reinterpret_cast<const u32 *>(pEntry + 8) != static_cast<u32>(ig))
                pEntry = *reinterpret_cast<u8 **>(pEntry);
            if (reinterpret_cast<uintptr_t>(pEntry) < 0x1000ull)
                continue;
            u8 *pData = *reinterpret_cast<u8 **>(pEntry + 0x10);
            if (reinterpret_cast<uintptr_t>(pData) < 0x1000ull)
                continue;
            auto *pFlags = *reinterpret_cast<u32 **>(pData + 0x28);
            if (reinterpret_cast<uintptr_t>(pFlags) < 0x1000ull)
                continue;
            // bits 0x1B..0x1E -> word 0; bits 0x30 and 0x32 -> word 1
            g_arEquippableGbids.push_back(static_cast<u32>(ig));
            if (!bFlags)
                continue;
            pFlags[0] |= 0x78000000u;
            pFlags[1] |= 0x00050000u;
            ++nSet;
        }
        std::sort(g_arEquippableGbids.begin(), g_arEquippableGbids.end());
        PRINT("[d3hack-custom] socket flags: %u worn types, %d worn items, %d flagged", nWorn, nHit,
              nSet)

        // Verify against the GAME'S OWN reader rather than re-reading what we just wrote --
        // the circular-verification mistake made earlier today. If 0x4F44F0 reports these
        // bits set on a pair of boots, the write is live and the target list simply does not
        // consult these flags; if it reports 0, the write went somewhere the game does not
        // look and every conclusion drawn from it is void.
        using FlagFn        = int (*)(u32, int);
        auto        pFlagFn = reinterpret_cast<FlagFn>(GameOffset(0x4F44F0));
        const u32   gBoots  = static_cast<u32>(Local::Djb2("Unique_Boots_Set_15_x1"));
        static const int arBit[6] = {0x1B, 0x30, 0x1C, 0x1D, 0x32, 0x1E};
        for (int b = 0; b < 6; ++b)
            PRINT("[d3hack-custom] socket flags: verify boots bit %02X -> %d", arBit[b],
                  pFlagFn(gBoots, arBit[b]))
    }

    // d3hack-custom: let the Sockets affixes roll on EVERY equipment slot.
    //
    // Sockets come from the AFFIX system -- SOCKETS (0x187) has 25 code sites and every one
    // is a READ, so the write is data-driven. Ramaladni's Gift and the Mystic are both just
    // routes to that affix, and stock item-group lists cover only a few slots.
    //
    // EVERY EDIT TO THE AFFIX RECORDS WAS IGNORED, and 0x4F1120 is why: at load it walks the
    // affix list, fetches each record, and **copies the fields into a cache**. Eligibility
    // reads the cache, never the record. Patching records after load cannot work, whatever
    // memory they live in. That one fact explains the GBRecordGet attempt, the paged-pool
    // attempt and the run-it-earlier attempt.
    //
    // The cache, read straight out of 0x4F1120..0x4F2388:
    //
    //     mgr    = *(*(u64*)0x114A840 + 0x2210)     same manager 0x4F44F0 hashes through
    //     count  = *(s32*)(mgr + 0x78)
    //     base   = *(u8**)(mgr + 0x80)              allocated at 0x4F1F60
    //     stride = 0x144                            `add x24, x24, #0x144` at 0x4F2388
    //     entry  = base + i*0x144
    //
    // Field mapping from the copy at 0x4F1FB4 on (x8 = base + x24, x24 starts at 0xA0, so
    // the entry base is x8-0xA0):
    //
    //     affix +0x10  GBID                     -> entry +0x00
    //     affix +0xA4  ItemGroup[0]             -> entry +0x4C
    //     affix +0x104 LegendaryAllowedTypes[0] -> entry +0xAC   (0x4C + 24*4, checks out)
    //
    // ItemGroup holds only 24 entries and there are far more equippable types than that, so
    // the types are DISTRIBUTED: each socket affix takes a rotating slice of the list. A
    // type only needs one socket affix to be legal for it, and with 48 affixes x ~23 free
    // slots every type lands on many affixes across the level range.
    //
    // Worn types are found by their equipment-slot field (+0x2C >= 0; Helm 1, Chest 2,
    // hands 3, Gloves 5, Belt 6, Boots 7, Shoulders 8, Legs 9, Bracers 10, Ring 12,
    // Amulet 13; -1 for abstract parents and anything not worn), so every weapon class,
    // shield and off-hand is included without guessing at names.
    //
    // Only slots reading 0xFFFFFFFF are written, so nothing stock is clobbered.
    void PatchSocketAffixItemGroups() {
        static bool s_done = false;
        if (s_done || (!global_config.rare_cheats.socket_affix_any_slot &&
                       !global_config.rare_cheats.socket_affix_suppress))
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        // Module/global pointer here, heap pointers below -- they need different range
        // checks. Mixing them up cost a silent return and a crash earlier.
        auto *pRootSlot = reinterpret_cast<u8 **>(GameOffset(0x114A840));
        if (pRootSlot == nullptr)
            return;
        u8 *pRoot = *pRootSlot;
        if (reinterpret_cast<uintptr_t>(pRoot) < 0x1000ull)
            return;
        u8 *pMgr = *reinterpret_cast<u8 **>(pRoot + 0x2210);
        if (reinterpret_cast<uintptr_t>(pMgr) < 0x1000ull)
            return;
        s_done = true;

        constexpr u32 STRIDE    = 0x144;
        constexpr u32 OFF_GBID  = 0x00;
        constexpr u32 OFF_GROUP = 0x4C;
        constexpr u32 GROUP_N   = 24;

        const s32 nCount = *reinterpret_cast<const s32 *>(pMgr + 0x78);
        auto     *pBase  = *reinterpret_cast<u8 **>(pMgr + 0x80);
        const uintptr_t uBase = reinterpret_cast<uintptr_t>(pBase);
        if (nCount <= 0 || nCount > 8192 || uBase < 0x1000000000ull || uBase >= 0x8000000000ull) {
            PRINT("[d3hack-custom] socket affixes: cache not ready (count=%d base=%p)", nCount, pBase)
            return;
        }

        // Every worn item type: equipment-slot field (+0x2C) >= 0. Covers all weapon
        // classes, shields, off-hands, jewellery and armour without guessing at names.
        // Also record each type's ancestor chain (ParentType at +0x18), because ItemGroup
        // entries are GROUPS -- a parent covers its children.
        // d3hack-custom: SUPPRESS mode -- the inverse of everything below.
        //
        // With FreeSocketsOnEveryItem on, every item already carries its sockets, so a rolled
        // "Sockets" affix is a wasted affix slot. Worse, the Mystic will offer it as an
        // enchant option, and taking it burns a real stat to grant something the item already
        // has. A pair of bracers rolling `Sockets (1)` next to three sockets it got for free
        // is the visible symptom.
        //
        // Every affix entry carries up to 24 item-group ids at +0x4C, with 0xFFFFFFFF as the
        // empty slot. Blanking all of them on the socket affixes takes those affixes out of
        // every group, so nothing can ever select one -- not a drop, not a reroll, not an
        // enchant option. It is a data edit on the affix cache, not a hook, and it is undone
        // by a relaunch with SocketAffixSuppress = false.
        //
        // Newly generated rolls only. Items that already carry a Sockets affix keep it.
        if (global_config.rare_cheats.socket_affix_suppress) {
            int nSock    = 0;
            int nCleared = 0;
            for (s32 i = 0; i < nCount; ++i) {
                u8       *pEntry = pBase + (static_cast<size_t>(i) * STRIDE);
                const s32 gbid   = *reinterpret_cast<const s32 *>(pEntry + OFF_GBID);
                if (gbid == 0 || gbid == -1)
                    continue;
                struct {
                    s32 eType;
                    s32 gbid;
                } tKey {0x8, gbid};
                u8    tOut[16] {};
                s32   fFlag = 1;
                auto *pRec =
                    reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut), &fFlag));
                // +0x170 is the attribute the affix grants; 0x187 is SOCKETS.
                if (pRec == nullptr || *reinterpret_cast<const s32 *>(pRec + 0x170) != 0x187)
                    continue;
                ++nSock;
                auto *pGroup = reinterpret_cast<u32 *>(pEntry + OFF_GROUP);
                for (u32 g = 0; g < GROUP_N; ++g) {
                    if (pGroup[g] == 0xFFFFFFFFu)
                        continue;
                    pGroup[g] = 0xFFFFFFFFu;
                    ++nCleared;
                }
            }
            PRINT("[d3hack-custom] socket affixes SUPPRESSED: %d socket affixes, %d group slots "
                  "cleared -- Sockets can no longer drop or be offered by the Mystic",
                  nSock, nCleared)
            return;
        }

        constexpr u32 MAX_TYPES = 256;
        constexpr u32 MAX_CHAIN = 8;
        static u32 arChain[MAX_TYPES][MAX_CHAIN];
        static u32 arChainN[MAX_TYPES];
        u32        nTypes = 0;

        std::vector<GBID> typeIds;
        AllGBIDsOfType(static_cast<GameBalanceType>(0x1), typeIds);
        for (GBID tg : typeIds) {
            if (nTypes >= MAX_TYPES)
                break;
            struct {
                s32 eType;
                s32 gbid;
            } tk {0x1, static_cast<s32>(tg)};
            u8    to[16] {};
            s32   ff  = 1;
            auto *pTR = reinterpret_cast<u8 *>(GBRecordGet(&tk, reinterpret_cast<void **>(to), &ff));
            if (pTR == nullptr || *reinterpret_cast<const s32 *>(pTR + 0x2C) < 0)
                continue;
            const u32 g = *reinterpret_cast<const u32 *>(pTR + 0x10);
            if (g == 0u || g == 0xFFFFFFFFu)
                continue;
            const u32 idx = nTypes++;
            u32 cn        = 0;
            arChain[idx][cn++] = g;
            u32 cur = *reinterpret_cast<const u32 *>(pTR + 0x18);
            for (u32 d = 1; d < MAX_CHAIN && cur != 0u && cur != 0xFFFFFFFFu; ++d) {
                arChain[idx][cn++] = cur;
                struct {
                    s32 eType;
                    s32 gbid;
                } pk {0x1, static_cast<s32>(cur)};
                u8    po[16] {};
                s32   pf  = 1;
                auto *pPR = reinterpret_cast<u8 *>(GBRecordGet(&pk, reinterpret_cast<void **>(po), &pf));
                if (pPR == nullptr)
                    break;
                cur = *reinterpret_cast<const u32 *>(pPR + 0x18);
            }
            arChainN[idx] = cn;
        }
        if (nTypes == 0) {
            PRINT("[d3hack-custom] socket affixes: no worn item types (%d)", 0)
            return;
        }

        // Greedy set cover: pick the GROUP_N groups that between them cover the most worn
        // types. With a hierarchy a handful of parents usually covers everything, which
        // matters because all 24 slots have to serve EVERY affix -- distributing a slice per
        // affix (the previous attempt) left each type on only ~8 of the 48, so a slot could
        // end up only on affixes outside the item's level bracket.
        static u32  arPick[GROUP_N];
        u32         nPick = 0;
        static bool arDone[MAX_TYPES];
        for (u32 t = 0; t < nTypes; ++t)
            arDone[t] = false;
        u32 nCovered = 0;
        while (nPick < GROUP_N && nCovered < nTypes) {
            u32 best = 0;
            u32 bestGain = 0;
            for (u32 c = 0; c < nTypes; ++c) {
                for (u32 d = 0; d < arChainN[c]; ++d) {
                    const u32 cand = arChain[c][d];
                    bool bTaken = false;
                    for (u32 p = 0; p < nPick; ++p)
                        if (arPick[p] == cand)
                            bTaken = true;
                    if (bTaken)
                        continue;
                    u32 gain = 0;
                    for (u32 t = 0; t < nTypes; ++t) {
                        if (arDone[t])
                            continue;
                        for (u32 e = 0; e < arChainN[t]; ++e)
                            if (arChain[t][e] == cand) {
                                ++gain;
                                break;
                            }
                    }
                    if (gain > bestGain) {
                        bestGain = gain;
                        best     = cand;
                    }
                }
            }
            if (bestGain == 0)
                break;
            arPick[nPick++] = best;
            for (u32 t = 0; t < nTypes; ++t) {
                if (arDone[t])
                    continue;
                for (u32 e = 0; e < arChainN[t]; ++e)
                    if (arChain[t][e] == best) {
                        arDone[t] = true;
                        ++nCovered;
                        break;
                    }
            }
        }
        PRINT("[d3hack-custom] socket affixes: %u worn types, %u groups cover %u of them", nTypes,
              nPick, nCovered)
        for (u32 p = 0; p < nPick && p < 8u; ++p)
            PRINT("[d3hack-custom] socket affixes:   group[%u] = %08X", p, arPick[p])

        int nSock   = 0;
        int nFilled = 0;
        for (s32 i = 0; i < nCount; ++i) {
            u8       *pEntry = pBase + (static_cast<size_t>(i) * STRIDE);
            const s32 gbid   = *reinterpret_cast<const s32 *>(pEntry + OFF_GBID);
            if (gbid == 0 || gbid == -1)
                continue;
            struct {
                s32 eType;
                s32 gbid;
            } tKey {0x8, gbid};
            u8    tOut[16] {};
            s32   fFlag = 1;
            auto *pRec = reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut), &fFlag));
            if (pRec == nullptr || *reinterpret_cast<const s32 *>(pRec + 0x170) != 0x187)
                continue;
            ++nSock;
            // EVERY socket affix gets the SAME cover set, so no slot depends on which
            // affixes happened to receive it.
            auto *pGroup = reinterpret_cast<u32 *>(pEntry + OFF_GROUP);
            for (u32 k = 0; k < nPick; ++k) {
                bool bHave = false;
                for (u32 g = 0; g < GROUP_N; ++g)
                    if (pGroup[g] == arPick[k])
                        bHave = true;
                if (bHave)
                    continue;
                for (u32 g = 0; g < GROUP_N; ++g) {
                    if (pGroup[g] != 0xFFFFFFFFu)
                        continue;
                    pGroup[g] = arPick[k];
                    ++nFilled;
                    break;
                }
            }
        }
        PRINT("[d3hack-custom] socket affixes: %d socket entries, %d group slots filled", nSock,
              nFilled)
    }

    // d3hack-custom: give every equippable item type the full three sockets.
    //
    // Socket capacity is a property of the item TYPE, not the item. Per-item records were a
    // dead end -- chest, pants, helm and boots all read identically at every candidate
    // offset. DiIiS's `ItemTypeTable` has the real field, and a name-addressed dump of
    // GB_ITEMTYPES pinned it at **record + 0x24**, scoring 12/12 against known capacities:
    //
    //     ChestArmor 3   Legs 2   Helm 1   Ring 1   Amulet 1   Sword 1
    //     Boots 0        Gloves 0 Belt 0   Bracers 0 Shoulders 0 Shield 0
    //
    // Boots being 0 is why Ramaladni's Gift never listed them: opening the target gate at
    // 0x4F6C90 could not help while the type itself could not hold a socket.
    //
    // **record + 0x2C is the equipment slot index** (Helm 1, Chest 2, hands 3, Gloves 5,
    // Belt 6, Boots 7, Shoulders 8, Legs 9, Bracers 10, Ring 12, Amulet 13) and reads -1 for
    // abstract parents like Offhand and for everything that is not worn. That is the
    // discriminator for "is this equippable", so no guessing at type names is needed.
    //
    // Item GBIDs -- and item TYPE gbids -- are djb2 over the lowercased name
    // (hash = hash*33 + c). All 16 probe names resolved, which is what confirmed it.
    //
    // 0x6CA760 hands back a refcounted temporary and RaiseParagonStatLimits already found
    // that writes through it did not stick for ParagonBonuses. So every write here is
    // verified: re-fetch the record afterwards and read it back, and report how many
    // actually took. If the count is 0 the write path needs rethinking, and the log says so
    // outright rather than leaving a silently useless patch.
    void PatchItemTypeMaxSockets() {
        static bool s_done = false;
        const int   nWant  = global_config.rare_cheats.any_slot_max_sockets;
        if (s_done || nWant <= 0)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr)
            return;
        if (GBGetHandlePool() == nullptr)
            return;
        s_done = true;

        constexpr u32 OFF_MAXSOCKETS = 0x24;
        constexpr u32 OFF_SLOT       = 0x2C;

        std::vector<GBID> ids;
        AllGBIDsOfType(static_cast<GameBalanceType>(0x1), ids);  // GB_ITEMTYPES
        if (ids.empty()) {
            PRINT("[d3hack-custom] max sockets: no item types (%d)", 0)
            return;
        }

        int nEquip = 0;
        int nWrote = 0;
        int nStuck = 0;
        for (GBID gbid : ids) {
            struct {
                s32 eType;
                s32 gbid;
            } tKey {0x1, static_cast<s32>(gbid)};
            u8    tOut[16] {};
            s32   fFlag = 1;
            auto *pRec = reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut), &fFlag));
            if (pRec == nullptr)
                continue;
            const s32 nSlot = *reinterpret_cast<const s32 *>(pRec + OFF_SLOT);
            if (nSlot < 0)
                continue;  // abstract parent type, or not worn
            ++nEquip;
            if (*reinterpret_cast<s32 *>(pRec + OFF_MAXSOCKETS) == nWant)
                continue;
            *reinterpret_cast<s32 *>(pRec + OFF_MAXSOCKETS) = nWant;
            ++nWrote;

            // verify against a FRESH fetch, not the temporary we just wrote through
            u8    tOut2[16] {};
            s32   fFlag2 = 1;
            auto *pRe = reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut2), &fFlag2));
            if (pRe != nullptr && *reinterpret_cast<const s32 *>(pRe + OFF_MAXSOCKETS) == nWant)
                ++nStuck;
        }

        PRINT("[d3hack-custom] max sockets: %d equippable types, wrote %d, verified %d stuck (want %d)",
              nEquip, nWrote, nStuck, nWant)
        if (nWrote > 0 && nStuck == 0)
            PRINT("[d3hack-custom] max sockets: writes did NOT stick -- GBRecordGet temporary (%d)", 0)
    }

    // d3hack-custom -------------------------------------------------------------
    // Raise the per-attribute paragon spend limits.
    //
    // ParagonBonuses (GLOBALSNO 0x3DB -> SNO 19739) is a KEYED GameBalance table: the
    // asset struct holds no record array (probed: zeros past +0x18), records are fetched
    // individually by GBID through 0x6CA760, exactly as the game does at 0x7F7254 and
    // 0x7DF938. The limit offset within a record is unknown, so it is discovered by
    // fingerprint: exactly 8 records read 100000 (main stat x7 + Vitality) and 20 read 50.
    //
    // Runs from sInitializeWorld, NOT GameCommonDataInit -- AllGBIDsOfType needs the GB
    // handle pool, which is not ready that early (calling it there faulted instantly).
    void RaiseParagonStatLimits() {
        static bool s_done = false;
        const int newCap = global_config.rare_cheats.paragon_stat_cap;
        if (s_done || newCap <= 50)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr)
            return;
        if (GBGetHandlePool() == nullptr) {
            PRINT("[d3hack-custom] paragon stat limits: handle pool not ready (%d)", 0)
            return;
        }
        s_done = true;
        PRINT("[d3hack-diag] paragon limits: step1 enumerate (%d)", 0)

        std::vector<GBID> ids;
        AllGBIDsOfType(static_cast<GameBalanceType>(0x28), ids);
        PRINT("[d3hack-diag] paragon limits: step2 got %u gbids", static_cast<u32>(ids.size()))
        if (ids.size() < 28u)
            return;

        std::vector<u8 *> recs;
        for (GBID gbid : ids) {
            struct { s32 eType; s32 gbid; } tKey { 0x28, static_cast<s32>(gbid) };
            u8   tOut[16] {};
            s32  fFlag = 1;
            auto *pRec = reinterpret_cast<u8 *>(GBRecordGet(&tKey, reinterpret_cast<void **>(tOut), &fFlag));
            if (pRec != nullptr)
                recs.push_back(pRec);
        }
        PRINT("[d3hack-diag] paragon limits: step3 got %u records", static_cast<u32>(recs.size()))
        if (recs.size() < 28u)
            return;

        s32 foundOff = -1;
        for (u32 off = 0; off <= 0x600u && foundOff < 0; off += 4u) {
            u32  nBig = 0;
            u32  nCap = 0;
            bool ok   = true;
            for (auto *pRec : recs) {
                const s32 v = *reinterpret_cast<const s32 *>(pRec + off);
                if (v == 100000)
                    ++nBig;
                else if (v == 50)
                    ++nCap;
                else {
                    ok = false;
                    break;
                }
            }
            if (ok && nBig == 8u && nCap == 20u)
                foundOff = static_cast<s32>(off);
        }

        if (foundOff < 0) {
            PRINT("[d3hack-diag] paragon limits: fingerprint not matched; rec0 words: %08X %08X %08X %08X",
                  static_cast<u32>(*reinterpret_cast<const s32 *>(recs[0] + 0x00)),
                  static_cast<u32>(*reinterpret_cast<const s32 *>(recs[0] + 0x04)),
                  static_cast<u32>(*reinterpret_cast<const s32 *>(recs[0] + 0x08)),
                  static_cast<u32>(*reinterpret_cast<const s32 *>(recs[0] + 0x0C)))
            return;
        }

        // 0x5271F0 picks between TWO limit fields per record:
        //     mov w9,#0x1C / mov w10,#0x18 / csel x9,x10,x9,eq
        // selected by attribute 0x5CC (0 -> +0x18, non-zero -> +0x1C). Patch both, or the
        // game reads the one we missed and the cap appears unchanged.
        u32 changed = 0;
        u32 changedAlt = 0;
        const u32 altOff = static_cast<u32>(foundOff) + 4u;
        for (auto *pRec : recs) {
            auto *p = reinterpret_cast<s32 *>(pRec + static_cast<u32>(foundOff));
            if (*p == 50) {
                *p = newCap;
                ++changed;
            }
            auto *q = reinterpret_cast<s32 *>(pRec + altOff);
            if (*q == 50) {
                *q = newCap;
                ++changedAlt;
            }
        }
        PRINT("[d3hack-custom] paragon stat limits: +0x%X raised %u, +0x%X raised %u, 50 -> %d",
              static_cast<u32>(foundOff), changed, altOff, changedAlt, newCap)
    }

    // d3hack-custom -------------------------------------------------------------
    // Shift every set bonus down one tier: the 4pc lands on the 2pc, the 6pc on the 4pc.
    //
    // Set bonuses are GameBalance records, GB_SET_ITEM_BONUSES (eType 0x21, 656 of them).
    // The runtime record is 0xE0 bytes -- registered as such at 0x6D5DEC, and it matches
    // the .gam layout with the 256-byte name stripped:
    //
    //     +0x10  GBID          +0x14  I1
    //     +0x18  Set           the item-set this bonus belongs to
    //     +0x1C  Count         HOW MANY PIECES THE BONUS NEEDS   <-- the field we rewrite
    //     +0x20  AttributeSpecifier[8], 24 bytes each
    //
    // +0x18 and +0x1C were read off the three -- and only three -- consumers of the table,
    // found by scanning for GBEnumerate(0x21):
    //
    //     0x49DCE4  APPLY:  cmp Count, equipped-count ; b.le -> grant the attributes
    //     0x3E0E70  UI:     collect this set's bonuses, filtered on Count >= 1
    //     0x66CB5C  UI:     same, and at 0x66CC78 it SORTS the list by Count
    //
    // That last one is why this is a data edit and not a hook. All three read the same
    // field, so rewriting the record moves the bonus AND relabels the tooltip and the set
    // panel in one go. Hooking the compare at 0x49DD44 would have made a "(4) Set" bonus
    // fire at 2 pieces while every tooltip still said 4.
    //
    // The rule is "one tier down", derived per set rather than hardcoded as 6->4 / 4->2:
    //
    //     newCount = the largest ORIGINAL Count in this same set that is strictly smaller
    //
    // For a class set with tiers 2/4/6 that gives 2/2/4, exactly as intended. For the many
    // two-tier 2/3 sets (Aughild, Captain Crimson, Cain, Born) it gives 2/2, which is the
    // same rule and not a special case. A single-tier set (Endless Walk, Bastions of Will)
    // has nothing below its bottom tier, so it is left alone. Nothing is ever shifted below
    // its set's own smallest tier, so a 2pc never becomes a 1pc.
    //
    // Ring of Royal Grandeur is untouched and still stacks: it adds to the equipped count,
    // so with the shift in place it buys the top tier at three pieces.
    //
    // Writes to GB records through 0x6CA760 DO stick -- PatchItemTypeMaxSockets verifies its
    // own 116 writes on every boot and has never reported a loss. The claim in the paragon
    // comment that 0x6CA760 hands back a temporary does not hold for records in the main
    // balance asset. This function verifies anyway, with a fresh fetch, and says so in the
    // log if a write is lost.
    //
    // Runs on every sInitializeWorld, not once: the plan is built once from the ORIGINAL
    // counts and then re-applied, so it is idempotent (a record already holding its new
    // count is skipped) and it re-heals if the balance asset is ever reloaded. Rebuilding
    // the plan from live data would double-shift on the second pass.
    //
    // Runtime data only -- no save file, item or character is touched. Set
    // SetBonusTierShift = false and relaunch to get stock behaviour back.
    // d3hack-custom: dump a set bonus record whole -- the piece count and all 8
    // AttributeSpecifiers -- for every set whose name contains SetBonusInspect.
    //
    // Why: a set bonus that is gated on something ("while a melee weapon is equipped") does
    // NOT evaluate that gate in native code. WEAPONMELEE (0x20D) has ZERO references anywhere
    // in .text and CURRENT_WEAPONCLASS (0x217) has seven, all writes and no reads -- checked
    // with re/attrxref.py. So the condition is carried in the record's own data, and the only
    // way to find which specifier holds it is to look at the bytes.
    //
    // Layout, from ShiftSetBonusTiers below (already proven against three consumers):
    //     +0x18 Set   +0x1C Count   +0x20 AttributeSpecifier[8], 24 bytes each
    //
    // Read-only. Nothing is written. Budgeted, and gated on a name filter so a single set can
    // be inspected without 656 records of noise.
    // d3hack-custom: name arbitrary SNOs. Powers are absent from the generated sno.hpp, and
    // SNOToString indexes by handle alone (it ignores its group argument), so any id resolves.
    // Runs at sInitializeWorld -- the same context InspectSetBonuses uses -- because calling
    // SNOToString from a world-gen worker thread is what killed the first world-factory probe.
    void NameSnoList() {
        const std::string &sList = global_config.rare_cheats.sno_name_list;
        if (sList.empty() || SNOToString == nullptr)
            return;
        static bool s_bDone = false;
        if (s_bDone)
            return;
        s_bDone = true;

        size_t i = 0;
        while (i < sList.size()) {
            size_t j = sList.find(',', i);
            if (j == std::string::npos)
                j = sList.size();
            s32  n  = 0;
            bool ok = false;
            for (size_t k = i; k < j; ++k) {
                const char c = sList[k];
                if (c >= '0' && c <= '9') {
                    n  = (n * 10) + (c - '0');
                    ok = true;
                }
            }
            i = j + 1;
            if (!ok)
                continue;
            auto        t  = SNOToString(static_cast<SNOGroup>(0), static_cast<SNO>(n), 0);
            const char *sz = t.str();
            PRINT("[d3hack-sno] %d = \"%s\"", n, (sz != nullptr && sz[0] != 0) ? sz : "?")
        }
    }

    void InspectSetBonuses() {
        // Every early return says WHY. A silent no-op is indistinguishable from "the hook
        // never ran", and that ambiguity has already cost this project several rounds.
        const std::string &sFilter = global_config.rare_cheats.set_bonus_inspect;
        if (sFilter.empty())
            return;   // the only silent one: not asked for
        static bool s_bDone = false;
        if (s_bDone)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr) {
            PRINT("[d3hack-setb] not ready: GBRecordGet=%d GBGetHandlePool=%d pool=%d",
                  GBRecordGet != nullptr, GBGetHandlePool != nullptr,
                  (GBGetHandlePool != nullptr && GBGetHandlePool() != nullptr))
            return;
        }

        std::vector<GBID> ids;
        AllGBIDsOfType(GB_SET_ITEM_BONUSES, ids);
        PRINT("[d3hack-setb] filter=\"%s\"  enumerated %u set-bonus record(s)",
              sFilter.c_str(), static_cast<u32>(ids.size()))
        if (ids.empty())
            return;   // not latched -- the pool may not be up yet, try next world
        s_bDone = true;

        int nShown = 0;
        for (GBID g : ids) {
            struct { s32 e; s32 g; } k {0x21, static_cast<s32>(g)};
            u8  o[16] {};
            s32 f    = 1;
            u8 *pRec = reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            if (pRec == nullptr)
                continue;

            const s32   nSet = *reinterpret_cast<const s32 *>(pRec + 0x18);
            const s32   nCnt = *reinterpret_cast<const s32 *>(pRec + 0x1C);
            const char *szSet = GbidStringAll(static_cast<GBID>(nSet));
            static int s_nSample = 0;
            if (szSet != nullptr && s_nSample < 4) {
                ++s_nSample;
                PRINT("[d3hack-setb] sample set name: \"%s\" (%d pieces)", szSet, nCnt)
            }
            if (szSet == nullptr)
                continue;

            // case-insensitive substring match on the set name
            bool bHit = false;
            for (const char *p1 = szSet; *p1 != 0 && !bHit; ++p1) {
                size_t i = 0;
                for (; i < sFilter.size(); ++i) {
                    char a = p1[i];
                    char b = sFilter[i];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                    if (a == 0 || a != b)
                        break;
                }
                if (i == sFilter.size())
                    bHit = true;
            }
            if (!bHit)
                continue;
            if (++nShown > 96)
                break;

            PRINT("[d3hack-setb] %s  gbid=%08X set=%08X pieces=%d", szSet,
                  static_cast<u32>(g), static_cast<u32>(nSet), nCnt)
            for (int i = 0; i < 8; ++i) {
                const u8 *pSpec = pRec + 0x20 + i * 24;
                const s32 nAttr = *reinterpret_cast<const s32 *>(pSpec);
                const s32 nParam = *reinterpret_cast<const s32 *>(pSpec + 4);
                if (nAttr == 0 && nParam == 0)
                    continue;   // empty slot
                // Name the power. A set bonus turns out to be nothing but
                // ITEM_POWER_PASSIVE[powerSNO] = 1, so the param IS the effect and its name is
                // the only way to tell which set this record really belongs to -- the set names
                // are internal ids like "Demon_Hunter_Set_p2", not display names.
                //
                // SNOToString indexes by handle alone and ignores its group argument (see the
                // SnoName notes in hooks/util.hpp), so 0 works for Powers.
                const char *szPow = nullptr;
                if (SNOToString != nullptr && nParam > 0) {
                    auto tName = SNOToString(static_cast<SNOGroup>(0), static_cast<SNO>(nParam), 0);
                    szPow      = tName.str();
                    if (szPow != nullptr && (szPow[0] == 0 || szPow[0] == '['))
                        szPow = nullptr;
                }
                if (szPow != nullptr)
                    PRINT("[d3hack-setb]   spec%d attr=0x%03X param=%d \"%s\"", i,
                          static_cast<u32>(nAttr), nParam, szPow)
                PRINT("[d3hack-setb]   spec%d attr=0x%03X param=%d  "
                      "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                      i, static_cast<u32>(nAttr), nParam,
                      pSpec[8], pSpec[9], pSpec[10], pSpec[11],
                      pSpec[12], pSpec[13], pSpec[14], pSpec[15],
                      pSpec[16], pSpec[17], pSpec[18], pSpec[19],
                      pSpec[20], pSpec[21], pSpec[22], pSpec[23])
            }
        }
        PRINT("[d3hack-setb] inspected %d record(s) matching \"%s\"", nShown, sFilter.c_str())
    }

    // d3hack-custom: dump the whole GB_PARAGON_BONUSES table (type 0x28, 28 records).
    //
    // Layout was derived OFFLINE, not guessed, by calibrating DiIiS's ParagonBonusesTable
    // file layout against the set-bonus record whose runtime layout is already verified:
    //
    //   SetItemBonus  file Set=264 -> runtime +0x18, Count=268 -> +0x1C, Attr[8]=272 -> +0x20
    //   so runtime = file - 240 (the 256-byte Name collapses to a 16-byte header).
    //
    // Applying the same transform to ParagonBonusesTable:
    //   file Hash=256 -> +0x10   I1=260 -> +0x14   I2=264 -> +0x18   (unnamed)=268 -> +0x1C
    //   file AttributeSpecifiers[4]=272 -> +0x20, 24 bytes each
    //   file Category=368 -> +0x80   Index=372 -> +0x84   HeroClass=376 -> +0x88
    //
    // +0x18/+0x1C corroborate independently: the retired RaiseParagonStatLimits fingerprinted
    // the per-stat caps at exactly those two offsets, and 0x5271F0 picks between them with a
    // csel on attribute 0x5CC (PARAGONCAPENABLED). Two derivations, same answer.
    //
    // AttributeSpecifier is attr(4) + snoParam(4) + 8 bytes + Formula (a serialized int
    // array). The per-point stat amount is somewhere in that tail -- whether it is a plain
    // constant we can scale or script bytecode we must intercept is EXACTLY what this dump
    // exists to answer. Read-only; nothing is written.
    void InspectParagonBonuses() {
        if (!global_config.rare_cheats.paragon_bonus_inspect)
            return;   // the only silent early-out: not asked for
        static bool s_bDone = false;
        if (s_bDone)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr) {
            PRINT("[d3hack-para] not ready: GBRecordGet=%d GBGetHandlePool=%d pool=%d",
                  GBRecordGet != nullptr, GBGetHandlePool != nullptr,
                  (GBGetHandlePool != nullptr && GBGetHandlePool() != nullptr))
            return;
        }

        std::vector<GBID> ids;
        AllGBIDsOfType(GB_PARAGON_BONUSES, ids);
        PRINT("[d3hack-para] enumerated %u paragon-bonus record(s), expected 28",
              static_cast<u32>(ids.size()))
        if (ids.empty())
            return;   // not latched yet -- try again next world
        s_bDone = true;

        int nShown = 0;
        for (GBID g : ids) {
            struct { s32 e; s32 g; } k {0x28, static_cast<s32>(g)};
            u8  o[16] {};
            s32 f    = 1;
            u8 *pRec = reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            if (pRec == nullptr)
                continue;

            const s32 nCapA  = *reinterpret_cast<const s32 *>(pRec + 0x18);
            const s32 nCapB  = *reinterpret_cast<const s32 *>(pRec + 0x1C);
            const s32 nCat   = *reinterpret_cast<const s32 *>(pRec + 0x80);
            const s32 nIndex = *reinterpret_cast<const s32 *>(pRec + 0x84);
            const s32 nClass = *reinterpret_cast<const s32 *>(pRec + 0x88);

            const char *szName = GbidStringAll(static_cast<GBID>(g));
            PRINT("[d3hack-para] %-28s gbid=%08X cat=%d idx=%d class=%d capA=%d capB=%d",
                  (szName != nullptr && szName[0] != 0) ? szName : "?",
                  static_cast<u32>(g), nCat, nIndex, nClass, nCapA, nCapB)

            for (int i = 0; i < 4; ++i) {
                const u8 *pSpec  = pRec + 0x20 + i * 24;
                const s32 nAttr  = *reinterpret_cast<const s32 *>(pSpec);
                const s32 nParam = *reinterpret_cast<const s32 *>(pSpec + 4);
                if (nAttr == 0 && nParam == 0)
                    continue;   // empty slot
                // The whole 24-byte specifier, so the formula tail is visible verbatim.
                PRINT("[d3hack-para]   spec%d attr=0x%03X param=%d  "
                      "%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                      i, static_cast<u32>(nAttr), nParam,
                      pSpec[8], pSpec[9], pSpec[10], pSpec[11],
                      pSpec[12], pSpec[13], pSpec[14], pSpec[15],
                      pSpec[16], pSpec[17], pSpec[18], pSpec[19],
                      pSpec[20], pSpec[21], pSpec[22], pSpec[23])
                // The 24-byte specifier decodes as:
                //   +0x00 attr  +0x04 param  +0x08 formula PTR (8)  +0x10 file off  +0x14 size
                // Confirmed by the rec0 word dump: +0x34 held 0x0C and +0x38 was the
                // FFFFFFFF that starts spec1, so the stride is 24 and the size is in BYTES.
                // Follow the pointer -- the per-point amount is in these ints.
                const u64 uFormula = *reinterpret_cast<const u64 *>(pSpec + 8);
                const u32 uBytes   = *reinterpret_cast<const u32 *>(pSpec + 0x14);
                if (uFormula > 0x1000ull && uBytes >= 4u && uBytes <= 256u && (uBytes % 4u) == 0u) {
                    const s32 *pF = reinterpret_cast<const s32 *>(uFormula);
                    const u32  nI = uBytes / 4u;
                    // Fixed argument slots rather than a formatted buffer: the interesting
                    // formulas are 3 ints, and this cannot overrun.
                    PRINT("[d3hack-para]     formula @%lX n=%u : %d %d %d %d %d %d",
                          uFormula, nI,
                          (nI > 0u) ? pF[0] : 0, (nI > 1u) ? pF[1] : 0,
                          (nI > 2u) ? pF[2] : 0, (nI > 3u) ? pF[3] : 0,
                          (nI > 4u) ? pF[4] : 0, (nI > 5u) ? pF[5] : 0)
                } else {
                    PRINT("[d3hack-para]     formula ptr/size rejected: %lX size=%u",
                          uFormula, uBytes)
                }
            }
            ++nShown;
        }
        // Words 0x00-0x40 of the first record, as a cross-check that the derived layout is
        // actually right. If capA/capB above do not look like caps, read this instead.
        if (!ids.empty()) {
            struct { s32 e; s32 g; } k0 {0x28, static_cast<s32>(ids[0])};
            u8   o0[16] {};
            s32  f0    = 1;
            auto *pRec0 = reinterpret_cast<u8 *>(GBRecordGet(&k0, reinterpret_cast<void **>(o0), &f0));
            if (pRec0 != nullptr) {
                const s32 *w = reinterpret_cast<const s32 *>(pRec0);
                for (int b = 0; b < 4; ++b)
                    PRINT("[d3hack-para] rec0 +%02X: %08X %08X %08X %08X",
                          b * 16, static_cast<u32>(w[b * 4 + 0]), static_cast<u32>(w[b * 4 + 1]),
                          static_cast<u32>(w[b * 4 + 2]), static_cast<u32>(w[b * 4 + 3]))
            }
        }
        PRINT("[d3hack-para] dumped %d record(s)", nShown)
    }

    // d3hack-custom: set how much main stat / Vitality one paragon point grants.
    //
    // The amount is NOT a field in the GameBalance record. Each AttributeSpecifier carries a
    // pointer to a small script formula, and for every paragon bonus that formula is exactly
    // three ints: { 6, <float bits>, 0 }. Slot 1 is the per-point value as an IEEE-754 float.
    // Confirmed by dumping all 28 records -- every decoded value matched stock D3 exactly
    // (main stat and Vitality 5.0, movement 0.005 = 0.5%/pt capped at 50 pts = 25%, crit
    // damage 0.01, crit chance 0.001), which is what makes the decode trustworthy rather
    // than merely plausible.
    //
    // Specifier layout, verified against a raw word dump of record 0:
    //     +0x00 attr   +0x04 param   +0x08 formula ptr (8)   +0x10 file off   +0x14 size
    //
    // We write through the formula pointer, NOT into the record. The record from GBRecordGet
    // is a refcounted temporary -- that is why the old RaiseParagonStatLimits was retired --
    // but the pointer it carries aims at the shared, loaded asset blob, which is stable.
    //
    // Targets by attribute id, so class-specific records are all covered without name
    // matching: 0x00A Strength, 0x00B Dexterity, 0x00C Intelligence (7 class records between
    // them), 0x00D Vitality. Resistance_All is also 5.0/pt but is attr 0x060 and NOT core,
    // so it is deliberately untouched.
    void PatchParagonStatValues() {
        // Clamp again here rather than trusting the parse. A config.toml that fails to
        // parse falls back to CODE defaults, and this project has already been bitten by a
        // patch that was armed only by a config value -- so the guard lives next to the
        // write, not only at the door.
        const float flCap  = kParagonPerPointMax;
        float       flMain = global_config.rare_cheats.paragon_mainstat_per_point;
        float       flVita = global_config.rare_cheats.paragon_vitality_per_point;
        if (flMain > flCap || flVita > flCap) {
            PRINT("[d3hack-parav] value(s) above the %d/pt ceiling clamped "
                  "(main %d -> %d, vit %d -> %d); see kParagonPerPointMax",
                  static_cast<int>(flCap), static_cast<int>(flMain),
                  static_cast<int>((flMain > flCap) ? flCap : flMain),
                  static_cast<int>(flVita),
                  static_cast<int>((flVita > flCap) ? flCap : flVita))
            if (flMain > flCap) flMain = flCap;
            if (flVita > flCap) flVita = flCap;
        }
        if (flMain <= 0.0f && flVita <= 0.0f)
            return;   // 0 = leave stock; the only silent early-out
        static bool s_bDone = false;
        if (s_bDone)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr) {
            PRINT("[d3hack-parav] not ready: GBRecordGet=%d pool=%d", GBRecordGet != nullptr,
                  (GBGetHandlePool != nullptr && GBGetHandlePool() != nullptr))
            return;
        }

        std::vector<GBID> ids;
        AllGBIDsOfType(GB_PARAGON_BONUSES, ids);
        if (ids.empty()) {
            PRINT("[d3hack-parav] no paragon records yet, retrying next world (%d)", 0)
            return;
        }
        s_bDone = true;

        int nHit = 0;
        int nMiss = 0;
        for (GBID g : ids) {
            struct { s32 e; s32 g; } k {0x28, static_cast<s32>(g)};
            u8  o[16] {};
            s32 f    = 1;
            u8 *pRec = reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            if (pRec == nullptr)
                continue;

            const u8 *pSpec = pRec + 0x20;              // spec0 only: every paragon bonus uses one
            const s32 nAttr = *reinterpret_cast<const s32 *>(pSpec);

            float flWant = 0.0f;
            if ((nAttr == 0x00A || nAttr == 0x00B || nAttr == 0x00C) && flMain > 0.0f)
                flWant = flMain;
            else if (nAttr == 0x00D && flVita > 0.0f)
                flWant = flVita;
            else
                continue;

            const u64 uFormula = *reinterpret_cast<const u64 *>(pSpec + 8);
            const u32 uBytes   = *reinterpret_cast<const u32 *>(pSpec + 0x14);
            if (uFormula <= 0x1000ull || uBytes < 12u || uBytes > 256u) {
                ++nMiss;
                continue;
            }
            s32 *pF = reinterpret_cast<s32 *>(uFormula);
            if (pF[0] != 6) {   // opcode we decoded; anything else is a layout we do not know
                PRINT("[d3hack-parav] attr 0x%03X: unexpected opcode %d, left alone",
                      static_cast<u32>(nAttr), pF[0])
                ++nMiss;
                continue;
            }

            const float flOld = __builtin_bit_cast(float, pF[1]);
            pF[1]             = __builtin_bit_cast(s32, flWant);
            // Read back to prove the store landed. NOTE: this proves the MEMORY changed, not
            // that the game reads this copy -- only the stat in-game proves that.
            const float flNow = __builtin_bit_cast(float, pF[1]);
            const char *szName = GbidStringAll(static_cast<GBID>(g));
            PRINT("[d3hack-parav] %-26s attr=0x%03X  %d/1000 -> %d/1000 (readback %d/1000)",
                  (szName != nullptr && szName[0] != 0) ? szName : "?",
                  static_cast<u32>(nAttr), static_cast<int>(flOld * 1000.0f),
                  static_cast<int>(flWant * 1000.0f), static_cast<int>(flNow * 1000.0f))
            ++nHit;
        }
        PRINT("[d3hack-parav] paragon per-point values written: %d record(s), %d skipped",
              nHit, nMiss)
    }

    void ShiftSetBonusTiers() {
        if (!global_config.rare_cheats.set_bonus_tier_shift)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        constexpr u32 OFF_SET    = 0x18;
        constexpr u32 OFF_COUNT  = 0x1C;
        constexpr s32 MAX_PIECES = 12;  // sanity bound; the largest real tier in the game is 6

        struct Local {
            static u8 *Rec(s32 gbid) {
                struct {
                    s32 e;
                    s32 g;
                } k {0x21, gbid};
                u8  o[16] {};
                s32 f = 1;
                return reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            }
        };

        struct Shift {
            s32 gbid;
            s32 nSet;
            s32 nWas;
            s32 nNow;
        };
        static std::vector<Shift> s_arPlan;
        static bool               s_bBuilt = false;

        if (!s_bBuilt) {
            std::vector<GBID> ids;
            AllGBIDsOfType(GB_SET_ITEM_BONUSES, ids);
            if (ids.empty()) {
                PRINT("[d3hack-custom] set bonus tiers: no records enumerated (%d)", 0)
                return;  // not latched -- try again on the next world
            }
            s_bBuilt = true;

            std::vector<Shift> arAll;
            arAll.reserve(ids.size());
            for (GBID g : ids) {
                u8 *pRec = Local::Rec(static_cast<s32>(g));
                if (pRec == nullptr)
                    continue;
                const s32 nSet = *reinterpret_cast<const s32 *>(pRec + OFF_SET);
                const s32 nCnt = *reinterpret_cast<const s32 *>(pRec + OFF_COUNT);
                if (nCnt < 1 || nCnt > MAX_PIECES)
                    continue;  // unused or nonsense row; leave it exactly as it is
                arAll.push_back(Shift {static_cast<s32>(g), nSet, nCnt, nCnt});
            }

            // One tier down, resolved against the ORIGINAL counts of the same set.
            for (auto &a : arAll) {
                s32 nBelow = 0;
                for (const auto &b : arAll)
                    if (b.nSet == a.nSet && b.nWas < a.nWas && b.nWas > nBelow)
                        nBelow = b.nWas;
                if (nBelow > 0)
                    a.nNow = nBelow;
            }

            for (const auto &a : arAll) {
                if (global_config.rare_cheats.set_bonus_dump)
                    PRINT("[d3hack-setbonus] set %08X: %d pieces -> %d", static_cast<u32>(a.nSet),
                          a.nWas, a.nNow)
                if (a.nNow != a.nWas)
                    s_arPlan.push_back(a);
            }
            PRINT("[d3hack-custom] set bonus tiers: %u records, %u bonuses move down a tier",
                  static_cast<u32>(arAll.size()), static_cast<u32>(s_arPlan.size()))
        }

        if (s_arPlan.empty())
            return;

        int nWrote = 0;
        int nStuck = 0;
        for (const auto &a : s_arPlan) {
            u8 *pRec = Local::Rec(a.gbid);
            if (pRec == nullptr)
                continue;
            auto *p = reinterpret_cast<s32 *>(pRec + OFF_COUNT);
            if (*p == a.nNow)
                continue;  // already shifted -- idempotent re-entry
            *p = a.nNow;
            ++nWrote;
            // verify against a FRESH fetch, not the pointer we just wrote through
            u8 *pRe = Local::Rec(a.gbid);
            if (pRe != nullptr && *reinterpret_cast<const s32 *>(pRe + OFF_COUNT) == a.nNow)
                ++nStuck;
        }
        if (nWrote == 0)
            return;
        PRINT("[d3hack-custom] set bonus tiers: rewrote %d bonuses, %d verified stuck", nWrote, nStuck)
        if (nStuck == 0)
            PRINT("[d3hack-custom] set bonus tiers: writes did NOT stick -- GBRecordGet temporary (%d)",
                  0)
    }

    // d3hack-custom -------------------------------------------------------------
    // One-shot dump of GB_MONSTER_AFFIXES (eType 0x12, 65 records) -- the elite/champion
    // affix table. Groundwork for switching individual affixes off; nothing is written here.
    //
    // Runtime record layout is derived the same way the set-bonus one was: DiIiS parses the
    // .gam record as a 256-byte name followed by the fields, and the runtime record drops the
    // name and starts those fields at +0x10. That mapping is already confirmed on
    // SetItemBonuses (file I0,I1,Set,Count -> runtime +0x10,+0x14,+0x18,+0x1C), so
    // runtime = file - 0xF0:
    //
    //     +0x10 I0   +0x14 I1   +0x18 I2   +0x1C I3   +0x20 I4
    //     +0x24 MonsterAffix   +0x28 Resistance   +0x2C AffixType
    //     +0x30 I5   +0x34 I6   +0x38 I7   +0x3C I8
    //     +0x40  Attributes[10]       , 24 bytes each -> 0x40..0x130
    //     +0x130 MinionAttributes[10] , 24 bytes each -> 0x130..0x220
    //     +0x220 on-spawn power SNOs
    //
    // The +0x220 end is corroborated by the game itself: both affix application sites,
    // 0x1420BC and 0x1426A4, do `ldr w5, [x0, #0x220]` and pass it on as a power.
    //
    // Names are not in the executable, so each record is identified by GBID. Monster-affix
    // GBIDs are djb2 over the lowercased name, the same hash PatchItemSocketCategoryFlags
    // already relies on for item types -- so a candidate name list resolves the roll call
    // without guessing at offsets.
    //
    // WHY A DUMP AND NOT A PATCH: which field gates selection is not yet known. AffixType at
    // +0x2C is the obvious candidate but I0..I8 could hold a weight or a level gate just as
    // easily, and neutralising the wrong one either does nothing or breaks every elite in the
    // game. The attribute slots are printed as well, because clearing those is the fallback
    // if no gate field turns up -- and for that the empty-slot value has to be observed, not
    // assumed. Measure first; that trap has been paid for once already.
    void DumpMonsterAffixes() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.monster_affix_dump)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        struct Local {
            static s32 Djb2(const char *p) {
                s32 h = 0;
                for (; *p != 0; ++p) {
                    char c = *p;
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                    h = (h << 5) + h + static_cast<s32>(c);
                }
                return h;
            }
            static u8 *Rec(s32 gbid) {
                struct {
                    s32 e;
                    s32 g;
                } k {0x12, gbid};
                u8  o[16] {};
                s32 f = 1;
                return reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            }
        };

        std::vector<GBID> ids;
        AllGBIDsOfType(GB_MONSTER_AFFIXES, ids);
        if (ids.empty()) {
            PRINT("[d3hack-diag] monster affixes: none enumerated (%d)", 0)
            return;
        }
        s_done = true;
        PRINT("[d3hack-diag] monster affixes: %u records", static_cast<u32>(ids.size()))

        for (GBID g : ids) {
            u8 *p = Local::Rec(static_cast<s32>(g));
            if (p == nullptr)
                continue;
            const auto *w = reinterpret_cast<const s32 *>(p);
            PRINT("[d3hack-diag] maffix %08X hdr %d %d %d %d %d | affix=%d resist=%d type=%d | "
                  "%d %d %d %d",
                  static_cast<u32>(g), w[0x10 / 4], w[0x14 / 4], w[0x18 / 4], w[0x1C / 4],
                  w[0x20 / 4], w[0x24 / 4], w[0x28 / 4], w[0x2C / 4], w[0x30 / 4], w[0x34 / 4],
                  w[0x38 / 4], w[0x3C / 4])
            PRINT("[d3hack-diag] maffix %08X attr %08X %08X | %08X %08X | %08X %08X | spawn "
                  "%08X %08X %08X %08X",
                  static_cast<u32>(g), static_cast<u32>(w[0x40 / 4]), static_cast<u32>(w[0x44 / 4]),
                  static_cast<u32>(w[0x58 / 4]), static_cast<u32>(w[0x5C / 4]),
                  static_cast<u32>(w[0x70 / 4]), static_cast<u32>(w[0x74 / 4]),
                  static_cast<u32>(w[0x220 / 4]), static_cast<u32>(w[0x224 / 4]),
                  static_cast<u32>(w[0x228 / 4]), static_cast<u32>(w[0x22C / 4]))
        }

        // Roll call: which candidate name hashes to a record that actually exists. Several
        // spellings per affix, because the internal name is not the display name and is not in
        // the executable to be read.
        static const char *arNames[] = {
            "Juggernaut",      "MonsterAffix_Juggernaut", "Molten",        "Frozen",
            "Arcane",          "ArcaneEnchanted",         "Jailer",        "Waller",
            "Vortex",          "Nightmarish",             "Illusionist",   "Fast",
            "Electrified",     "Desecrator",              "Thunderstorm",  "Wormhole",
            "Poison",          "PoisonEnchanted",         "MissileDampening", "HealthLink",
            "Knockback",       "Teleporter",              "Avenger",       "Orbiter",
            "Shielding",       "ReflectsDamage",          "Mortar",        "FireChains",
            "Horde",           "ExtraHealth",             "Plagued",       "Extra_Health",
            "Reflects_Damage", "Fire_Chains",             "Missile_Dampening", "Health_Link",
        };
        constexpr int NAMES_N = static_cast<int>(sizeof(arNames) / sizeof(arNames[0]));
        for (int i = 0; i < NAMES_N; ++i) {
            const s32 gbid = Local::Djb2(arNames[i]);
            bool      bIn  = false;
            for (GBID g : ids)
                if (static_cast<s32>(g) == gbid)
                    bIn = true;
            if (bIn)
                PRINT("[d3hack-diag] maffix NAME \"%s\" = %08X  <-- resolves", arNames[i],
                      static_cast<u32>(gbid))
        }
        PRINT("[d3hack-diag] monster affixes: dump complete (%d)", 0)
    }

    // d3hack-custom -------------------------------------------------------------
    // Stop named elite/champion affixes from ever rolling.
    //
    // `DisabledMonsterAffixes` is a comma-separated list of internal affix names, default
    // "Juggernaut". Empty string = stock.
    //
    // **+0x18 in a GB_MONSTER_AFFIXES record is the selection weight.** Read straight off the
    // 65-record dump rather than guessed:
    //
    //     value    count   which
    //     100      31      every real elite affix -- Molten, Frozen, Vortex, Fast, Plagued,
    //                      Juggernaut, Jailer, Waller, Mortar, Horde, ...
    //      80       1      Shielding, which is genuinely a rarer roll than the rest
    //      20       1      one unresolved record
    //       0      32      the remainder -- minion/internal rows that never appear as a pack
    //                      affix
    //
    // A clean 100/80/20/0 spread across exactly the affixes you see in game, with zero on the
    // ones you never do. Writing 0 therefore puts Juggernaut into a state the game already
    // handles for 32 other rows in the same table -- the safest possible edit, and the reason
    // this is a one-field write rather than an attempt to gut the affix's payload.
    //
    // Record layout, confirmed against the dump (runtime record, 0x12 / GB_MONSTER_AFFIXES):
    //
    //     +0x10  GBID          (matches djb2 of the lowercased name for all 25 resolved rows)
    //     +0x18  weight        <-- this
    //     +0x38  1 on exactly the rare-pack-only affixes (Juggernaut, Shielding, Horde,
    //            Illusionist, Avenger, ...), and Juggernaut's spawn-power slots agree:
    //            +0x224 minion -1, +0x228 champion -1, +0x22C rare 0x6F30C
    //     +0x40  AttributeSpecifier[10], 24 bytes each, empty slot = 0xFFFFFFFF
    //
    // Names are not in the executable, so a name is resolved by djb2 over its lowercased form
    // -- the same hash PatchItemSocketCategoryFlags uses for item types, and verified here
    // against 25 of the 65 records. A name that does not resolve is reported rather than
    // silently ignored, so a typo in the config is visible in the log.
    //
    // Runs every world and is idempotent: a record already at weight 0 is skipped. Writes are
    // verified with a fresh fetch, the same way PatchItemTypeMaxSockets does -- and that has
    // now been confirmed twice on this branch (max sockets 116/116, set bonus tiers 140/140),
    // so GB record writes do stick.
    void DisableMonsterAffixes() {
        const std::string &sList = global_config.rare_cheats.disabled_monster_affixes;
        if (sList.empty())
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        constexpr u32 OFF_WEIGHT = 0x18;

        struct Local {
            static s32 Djb2(const char *p, size_t n) {
                s32 h = 0;
                for (size_t i = 0; i < n; ++i) {
                    char c = p[i];
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + 32);
                    h = (h << 5) + h + static_cast<s32>(c);
                }
                return h;
            }
            static u8 *Rec(s32 gbid) {
                struct {
                    s32 e;
                    s32 g;
                } k {0x12, gbid};
                u8  o[16] {};
                s32 f = 1;
                return reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            }
        };

        size_t i = 0;
        while (i < sList.size()) {
            size_t j = sList.find(',', i);
            if (j == std::string::npos)
                j = sList.size();
            size_t a = i;
            size_t b = j;
            while (a < b && (sList[a] == ' ' || sList[a] == '\t'))
                ++a;
            while (b > a && (sList[b - 1] == ' ' || sList[b - 1] == '\t'))
                --b;
            i = j + 1;
            if (a >= b)
                continue;

            const std::string sName = sList.substr(a, b - a);
            const s32         gbid  = Local::Djb2(sName.c_str(), sName.size());
            u8               *pRec  = Local::Rec(gbid);
            if (pRec == nullptr) {
                PRINT("[d3hack-custom] monster affix \"%s\" (gbid %08X) has no record -- check the "
                      "spelling; it is the internal name, not the display name",
                      sName.c_str(), static_cast<u32>(gbid))
                continue;
            }
            auto     *p    = reinterpret_cast<s32 *>(pRec + OFF_WEIGHT);
            const s32 nWas = *p;
            if (nWas == 0)
                continue;  // already off -- idempotent re-entry
            *p = 0;
            // verify against a FRESH fetch, not the pointer we just wrote through
            u8        *pRe   = Local::Rec(gbid);
            const bool bOk   = (pRe != nullptr && *reinterpret_cast<const s32 *>(pRe + OFF_WEIGHT) == 0);
            PRINT("[d3hack-custom] monster affix \"%s\" (%08X) weight %d -> 0%s", sName.c_str(),
                  static_cast<u32>(gbid), nWas, bOk ? "" : "  -- WRITE DID NOT STICK")
        }
    }

    // d3hack-custom -------------------------------------------------------------
    // One-shot dump of the two Greater Rift GameBalance tables.
    //
    // Hunting the Orek's Dream trigger. What is known so far:
    //
    //   - It is patch 2.7.3 content and IS in this build: `p73_OreksDream_*` assets live in
    //     Patch2_7_3.cpk through Patch2_7_6.cpk (the live patch level).
    //   - Its entire asset set is ELEVEN COSMETIC FILES -- particles, sounds, textures, one
    //     effect group. No world, no level area, no scene, no monster, no power. Everything
    //     else under p73_ is Echoing Nightmare (SwarmRift): 5 worlds, 97 monsters, 10 scenes.
    //   - The executable contains no "orek" and no "dream" string at all.
    //
    // So the "curated maps and monsters" are not new content -- they are a SELECTION over
    // existing tilesets and monster pools, and the VFX just play when the selection fires.
    // A selection weight lives in data, and these are the two rift tables it can live in.
    //
    // Deliberately a table dump and NOT a hook on the event: Orek's Dream cannot be forced, so
    // anything that only reports when one occurs is untestable. This runs on world entry, every
    // launch, with no rare trigger required.
    //
    // Records are printed raw. LootRunQuestTiers is 3 rows, TieredLootRunLevels is the GR
    // ladder. A percentage-shaped field -- a lone 5, 10 or 25 among level/reward numbers -- is
    // what to look for, the same way +0x18 on the monster-affix table gave itself away by
    // reading 100 for every real affix and 0 for every unused one.
    void DumpRiftTables() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.rift_table_dump)
            return;
        if (GBRecordGet == nullptr || GBGetHandlePool == nullptr || GBGetHandlePool() == nullptr)
            return;

        struct Local {
            static u8 *Rec(s32 eType, s32 gbid) {
                struct {
                    s32 e;
                    s32 g;
                } k {eType, gbid};
                u8  o[16] {};
                s32 f = 1;
                return reinterpret_cast<u8 *>(GBRecordGet(&k, reinterpret_cast<void **>(o), &f));
            }
        };

        const s32   arType[2] = {0x27, 0x31};
        const char *arName[2] = {"LOOTRUN_QUEST_TIERS", "TIERED_LOOT_RUN_LEVELS"};

        for (int t = 0; t < 2; ++t) {
            std::vector<GBID> ids;
            AllGBIDsOfType(static_cast<GameBalanceType>(arType[t]), ids);
            PRINT("[d3hack-diag] %s (0x%X): %u records", arName[t], static_cast<unsigned>(arType[t]),
                  static_cast<u32>(ids.size()))
            if (ids.empty())
                continue;
            s_done = true;

            int nRow = 0;
            for (GBID g : ids) {
                if (nRow >= 64)
                    break;  // the GR ladder is long; the head is where a weight would sit
                u8 *p = Local::Rec(arType[t], static_cast<s32>(g));
                if (p == nullptr)
                    continue;
                const auto *w = reinterpret_cast<const s32 *>(p);
                for (u32 o = 0; o < 0x60u; o += 0x20u) {
                    PRINT("[d3hack-diag] %s[%02d] gbid %08X +%02X  %d %d %d %d %d %d %d %d",
                          arName[t], nRow, static_cast<u32>(g), o, w[(o + 0x00) / 4],
                          w[(o + 0x04) / 4], w[(o + 0x08) / 4], w[(o + 0x0C) / 4],
                          w[(o + 0x10) / 4], w[(o + 0x14) / 4], w[(o + 0x18) / 4],
                          w[(o + 0x1C) / 4])
                }
                ++nRow;
            }
        }
        PRINT("[d3hack-diag] rift tables: dump complete (%d)", 0)
    }

    // d3hack-custom -------------------------------------------------------------
    // Dump the TieredLootRunLevels asset -- the Greater Rift ladder.
    //
    // ORek's Dream hunt, last untested table. The previous attempt asked
    // AllGBIDsOfType(0x31) and got "0 records", which I nearly took as "empty". It is not
    // empty: TieredLootRunLevels is NOT A KEYED TABLE, it has no GBIDs to enumerate, so that
    // API is structurally blind to it. ExtendTieredRiftTable already reaches it correctly and
    // this copies its access path exactly:
    //
    //     mgr    = **GameOffsetFromTable("gb_asset_mgr_ptr")
    //     asset  = GBAssetGet(mgr, GlobalSNOGet(0x401))
    //     nBytes = *(u32*)(asset + 0x20C)
    //     rows   = *(u8**)(asset + 0x210)      , 56 bytes per record
    //
    // Record shape, from ExtendTieredRiftTable's own extrapolation code: eight floats at
    // +0x00..+0x1F, an s64 at +0x28, and integer/constant fields elsewhere. Printed as both
    // u32 and float because a weight could be either -- 0.25f and 25 look nothing alike in hex.
    //
    // Also dumps asset+0x200..0x260 so any OTHER array hanging off the same asset shows up as
    // a (size, pointer) pair, the way 0x20C/0x210 do.
    //
    // No Orek's Dream has to occur for this: it runs on world entry.
    void DumpTieredRiftLevels() {
        static bool s_done = false;
        if (s_done || !global_config.rare_cheats.rift_level_dump)
            return;
        if (GlobalSNOGet == nullptr || GBAssetGet == nullptr)
            return;

        auto **pp = *reinterpret_cast<void ***>(GameOffsetFromTable("gb_asset_mgr_ptr"));
        if (pp == nullptr || *pp == nullptr)
            return;
        auto *asset = reinterpret_cast<u8 *>(GBAssetGet(*pp, GlobalSNOGet(static_cast<SNO>(0x401))));
        if (asset == nullptr) {
            PRINT("[d3hack-diag] rift levels: asset not loaded yet (%d)", 0)
            return;
        }
        s_done = true;

        const u32 nBytes = *reinterpret_cast<const u32 *>(asset + 0x20C);
        auto     *pData  = *reinterpret_cast<u8 **>(asset + 0x210);
        constexpr u32 REC = 56u;
        const u32 rows = (REC != 0u) ? (nBytes / REC) : 0u;
        PRINT("[d3hack-diag] rift levels: asset=%p bytes=%u rows=%u data=%p", asset, nBytes, rows,
              pData)

        // Neighbouring (size, pointer) pairs -- any other list on this asset.
        for (u32 o = 0x200; o < 0x260; o += 0x10) {
            PRINT("[d3hack-diag] rift asset +%03X  %08X %08X %08X %08X", o,
                  *reinterpret_cast<const u32 *>(asset + o + 0x0),
                  *reinterpret_cast<const u32 *>(asset + o + 0x4),
                  *reinterpret_cast<const u32 *>(asset + o + 0x8),
                  *reinterpret_cast<const u32 *>(asset + o + 0xC))
        }

        if (pData == nullptr || rows == 0u)
            return;
        const u32 nShow = (rows < 10u) ? rows : 10u;
        for (u32 i = 0; i < nShow; ++i) {
            const u8 *r = pData + (static_cast<size_t>(i) * REC);
            PRINT("[d3hack-diag] rift row %02u u32  %08X %08X %08X %08X %08X %08X %08X", i,
                  *reinterpret_cast<const u32 *>(r + 0x00), *reinterpret_cast<const u32 *>(r + 0x04),
                  *reinterpret_cast<const u32 *>(r + 0x08), *reinterpret_cast<const u32 *>(r + 0x0C),
                  *reinterpret_cast<const u32 *>(r + 0x10), *reinterpret_cast<const u32 *>(r + 0x14),
                  *reinterpret_cast<const u32 *>(r + 0x18))
            // milli-units: a 0.25 weight reads as 250 here and as 3E800000 above
            int arM[8];
            for (int j = 0; j < 8; ++j) {
                float f = 0.0f;
                memcpy(&f, r + (j * 4), 4);
                arM[j] = static_cast<int>(f * 1000.0f);
            }
            PRINT("[d3hack-diag] rift row %02u flt(x1000) %d %d %d %d %d %d %d %d", i, arM[0],
                  arM[1], arM[2], arM[3], arM[4], arM[5], arM[6], arM[7])
        }
        PRINT("[d3hack-diag] rift levels: dump complete (%u rows)", rows)
    }

    void PatchBase() {
        auto jest = patch::RandomAccessPatcher();

        PortCheatCodes();

        XVarBool_Set(&g_varLocalLoggingEnable, true, 3u);
        XVarBool_Set(&g_varChallengeRiftEnabled, global_config.challenge_rifts.active, 3u);
        XVarBool_Set(&g_varSeasonsOverrideEnabled, global_config.seasons.active, 3u);
        XVarBool_Set(&g_varOnlineServicePTR, global_config.seasons.spoof_ptr, 3u);
        XVarBool_Set(&g_varExperimentalScheduling, global_config.resolution_hack.exp_scheduler, 3u);
        // d3hack-custom: raise the paragon level ceiling (XVar; game default 20000)
        if (global_config.rare_cheats.max_paragon_level > 0) {
            XVarUint32_Set(&g_varMaxParagonLevel, static_cast<u32>(global_config.rare_cheats.max_paragon_level), 3u);
            PRINT("[d3hack-custom] MaxParagonLevel set to %d", global_config.rare_cheats.max_paragon_level)
        }
        jest.Patch<ins::Movz>(PatchTable("patch_signin_01_movz"), reg::W0, 1);  // always Console::GamerProfile::IsSignedInOnline
        jest.Patch<ins::Ret>(PatchTable("patch_signin_02_ret"));                // ^ ret
        const bool infinite_mp = global_config.rare_cheats.active &&
                                 (global_config.rare_cheats.super_god_mode || global_config.rare_cheats.infinite_mp);
        PatchInfiniteMp(infinite_mp);

        /* String swap for autosave screen */
        MakeAdrlPatch(PatchTable("patch_autosave_string_01_adrl"), reinterpret_cast<uintptr_t>(g_szHackVerAutosave), reg::X0);

        /* String swap for start screen */
        MakeAdrlPatch(PatchTable("patch_start_string_01_adrl"), reinterpret_cast<uintptr_t>(&g_szHackVerStart), reg::X0);

        /* Enable local logging */
        jest.Patch<ins::Movz>(PatchTable("patch_local_logging_01_movz"), reg::W0, 1);

        // 0x00A2C614 - E5 0A 00 12
        // AND     reg::W5, reg::W23, #7
        // jest.Patch<exl::patch::inst::

        /* Pretend we are NOT in a mounted ROM */
        // jest.Patch<ins::Movz>(PatchTable("patch_rom_mount_01_movz"), reg::W1, 0);

        /* No timed wait for autosave warning */
        // jest.Patch<ins::Nop>(PatchTable("patch_autosave_wait_01_nop"));

        /* Inside ItemSetIsBeingManipulated(): BeingManipulated Attrib = 0 */
        // jest.Patch<ins::Branch>(PatchTable("patch_item_manipulated_01_branch"), 0x20);  // CBZ reg::W1, loc_BF090

        if (global_config.debug.enable_crashes) {
            /* Don't try to find a slot to equip newly duped item */
            jest.Patch<ins::Nop>(PatchTable("patch_dupe_noequip_01_nop"));  // BL ACDInventoryWhereCanThisGo()
            /* Stub functions that interfere with item duping */
            jest.Patch<ins::Ret>(PatchTable("patch_dupe_stub_01_ret")); /* void GameSoftPause(BOOL bPause) */
            jest.Patch<ins::Ret>(PatchTable("patch_dupe_stub_02_ret")); /* void ActorCommonData::SetAssignedHeroID(ActorCommonData *this, const Player *ptPlayer) */
            jest.Patch<ins::Ret>(PatchTable("patch_dupe_stub_03_ret")); /* void ActorCommonData::SetAssignedHeroID(ActorCommonData *this, OnlineService::HeroId idHero) */
            jest.Patch<ins::Ret>(PatchTable("patch_dupe_stub_04_ret")); /* void SGameSoftPause(BOOL bPause) */
        }

        /* Stub logging to RingBuffer */
        // jest.Patch<ins::Ret>(PatchTable("patch_log_ringbuffer_01_ret")); /* void FileOutputStream::LogToRingBuffer(int, char const*, char const*) */

        /* Stub net message tracing */
        // jest.Patch<ins::Ret>(PatchTable("patch_trace_message_01_ret")); /* void __fastcall sTraceMessage(const void *pMessage) */

        /* Fix path for stat tracing */
        MakeAdrlPatch(PatchTable("patch_trace_stat_path_01_adrl"), reinterpret_cast<uintptr_t>(&g_szTraceStat), reg::X0);

        if (!global_config.seasons.allow_online) {
            // Hide "Connect to Diablo Servers" menu entry (main menu item 12).
            // jest.Patch<ins::Branch>(PatchTable("patch_hideonline_01_branch"), -0x90);  // force false path in ItemShouldBeVisible

            /* Pause + Main menu: hide "Connect to Diablo Servers" list entry. */
            // 0x061620: STR reg::X19, [SP,#-0x10+var_10]!
            // 0x061624: STP reg::X29, reg::X30, [SP,#0x10+var_s0]
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_02_movz"), reg::W0, 0);  // return false
            jest.Patch<ins::Ret>(PatchTable("patch_hideonline_03_ret"));

            /* Main menu: hide Diablo Network status label in UIMainMenu::Console::OnUpdate. */
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_04_movz"), reg::W1, 0);  // off_1151298 visibility = 0 (connected path)
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_05_movz"), reg::W1, 0);  // off_1151290 visibility = 0 (disconnected path)
            // Hide the status text itself by calling SetVisible(0) on the text control instead of SetText.
            jest.Patch<dword>(PatchTable("patch_hideonline_06_bytes"), make_bytes(0x08, 0x29, 0x40, 0xF9));  // LDR reg::X8, [reg::X8,#0x50]
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_07_movz"), reg::W1, 0);

            /* Pause menu: hide Diablo Network status label in UIPause::Console::sOnWarningAccepted. */
            // 0x221324: MOV reg::W1, #1
            // 0x221338: MOV reg::W1, #1
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_08_movz"), reg::W1, 0);  // force SetVisible(0) for connected path
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_09_movz"), reg::W1, 0);  // force SetVisible(0) for disconnected path
            // Hide the status text itself by calling SetVisible(0) on the text control instead of SetText.
            jest.Patch<dword>(PatchTable("patch_hideonline_10_bytes"), make_bytes(0x08, 0x29, 0x40, 0xF9));  // LDR reg::X8, [reg::X8,#0x50]
            jest.Patch<ins::Movz>(PatchTable("patch_hideonline_11_movz"), reg::W1, 0);                       // reg::W1 = 0 (invisible)
        }

        /* Unlock all difficulties */
        // 0x5281B0: SXTW reg::X8, reg::W2
        // 0x5281B4: TBZ reg::W3, #0, loc_5281D8
        if (global_config.rare_cheats.active && global_config.rare_cheats.unlock_all_difficulties) {
            jest.Patch<ins::Movz>(PatchTable("patch_handicap_unlock_01_movz"), reg::W0, 1);  // return true
            jest.Patch<ins::Ret>(PatchTable("patch_handicap_unlock_02_ret"));
        }

        /* ItemCanDrop bypass */
        // jest.Patch<dword>(PatchTable("patch_itemcandrop_01_bytes"), make_bytes(0x10, 0x00, 0x00, 0x14));

        /* Increase the damage for ACTOR_EASYKILL_BIT */
        if (global_config.rare_cheats.active && global_config.rare_cheats.easy_kill_damage) {
            jest.Patch<ins::Movz>(PatchTable("patch_easykill_01_movz"), reg::W9, 0x7bc7);
            jest.Patch<ins::Movk>(PatchTable("patch_easykill_02_movk"), reg::W9, 0x5d94, ins::ShiftValue_16);
        }
        // 5863 5FA9 = 1,000 T
        // 5d94 7bc7 = 1,337,420T
        // 60e8 0dae = 133,769,696T
        // 6070 D958 = 69,420,000 T <--- lol
        // 6291 07B5 = 1,337,666,688 T
        // 61B6 53FF = 420,420,000 T
        // 61B6 6F61 = 420,666,656 T <--
        // 6210 8F6F = 666,666,688 T <--
    }

}  // namespace d3
