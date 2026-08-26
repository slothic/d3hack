#pragma once

// Back-compat shim: this file historically contained a large header-only utility
// implementation. The heavy helpers live in d3/util.cpp now (and the declarations
// are split across smaller headers included by d3/util.hpp).
#include "d3/util.hpp"
#include "d3/patches.hpp"

#if D3HACK_ENABLE_UTILITY_DEBUG_HOOKS
#include "program/config.hpp"
#include "program/d3/types/attributes.hpp"
#include "program/d3/types/common.hpp"
#include "lib/hook/inline.hpp"
#include "lib/hook/replace.hpp"
#include "lib/hook/trampoline.hpp"
#include "nn/oe.hpp"

#include <array>
#include <iterator>

namespace nn::os {
    auto GetHostArgc() -> u64;
    auto GetHostArgv() -> const char **;
};  // namespace nn::os

namespace d3 {

    [[maybe_unused]] inline bool g_testRanOnce = false;
    [[maybe_unused]] inline int  g_refCount    = 0;
    [[maybe_unused]] inline int  g_drawCount   = 0;

    inline bool g_pending_perf_notify = false;
    inline bool g_pending_dock_notify = false;

    inline void RequestPerfNotify() { g_pending_perf_notify = true; }
    inline void RequestDockNotify() { g_pending_dock_notify = true; }

    // NOTE: Command line injection is extremely fragile on the console build and can
    // provoke early fatal UI errors in Ryujinx (missing UIID -> DisplayInternalError).
    // Keep the implementation available for investigation, but default it off.
    inline constexpr bool kEnableCmdlineHooks = false;

    inline constinit auto g_cmdArgs = std::to_array<const char *>({
        "<<main>>",
        "-ignoreversioncheck",
        // "-w",
        "-d3d11",
        "-launch",
        // "-locale",
        // "-battlenetaddress",
        // "-battlenetaccount",
        // "-battlenetpassword",
        "-autologin",
        // "-fakeinput",
        // "-noinput",
        // "-quicktest",
        // "-test",
        // "-perftestminfpsscreenshot",
        // "-perftestfilter",
        // "-perftestlist",
        // "-perftestentrypoint",
        // "-perftestwaittime",
        // "-perftestsampletime",
        // "-perftestdumpfreq",
        // "-perftestplayernum",
        // "-perftestformat",
        "-ngdp",
        "-cdp",
        // "-perftestclass",
        // "-blizzcon",
        // "-blizzconpvp",
        // "-blizzcondrop",
        // "-joingamename",
        // "-jointeam",
        // "-gametimelimit",
        // "-uid",
        // "-agenttype",
        "-3dv",
        "-highdpicursor",
        // "-cursorignorehighdpi",
        "-nofpslimit",
        "-usepageflip",
        // "-debugd3d",
        "-overridelogfolder",
        "sd:/config/d3hack-nx/",
        // "-overridefmodemergencymem",
        // "-nod4license"
    });

    HOOK_DEFINE_TRAMPOLINE(CmdLineParse) {
        static void Callback(const char *szCmdLine) {
            Orig(szCmdLine);
            auto &array = CmdLineGetParams()->tBitArray;
            array.SetNthBit(38, true);
            array.SetNthBit(40, true);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(Enum_GB_Types) {
        static void Callback(GameBalanceType eType, GBHandleList *listResults) {
            Orig(eType, listResults);
            if (global_config.defaults_only || !global_config.debug.active || !global_config.debug.enable_debug_flags) {
                return;
            }

            g_tAppGlobals.eDebugFadeMode  = DFM_FADE_NEVER;
            g_tAppGlobals.eDebugSoundMode = DSM_FULL;
            AppDrawFlagSet(APP_DRAW_CONTROLLER_AUTO_TARGETING, 1);
            AppDrawFlagSet(APP_DRAW_LOW_POLY_SHADOWS_BIT, 1);
            AppDrawFlagSet(APP_DRAW_NO_ACTOR_SHADOWS_BIT, 1);
            AppDrawFlagSet(APP_DRAW_SHOW_BUFFS_BIT, 0);
            AppDrawFlagSet(APP_DRAW_BONES_BIT, 1);
            // AppDrawFlagSet(APP_DRAW_FULL_BRIGHT_BIT, 1);
            AppDrawFlagSet(APP_DRAW_TEXTURE_MEMORY_BIT, 1);
            AppDrawFlagSet(APP_DRAW_HWCLASS_UI_OUTLINES_BIT, 1);
            AppDrawFlagSet(APP_DRAW_HWCLASS_DISTORTIONS_BIT, 1);
            g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_ALL_HITS_CRIT_BIT);
            g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_INSTANT_RESURRECTION_BIT);
            g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_ALWAYS_PLAY_GETHIT_BIT);
            g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_DONT_IDENTIFY_RARES_BIT);
            g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_ALWAYS_PLAY_KNOCKBACK_BIT);
            g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_ALL_HITS_OVERKILL_BIT);
            g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_ALL_HITS_CRUSHING_BLOW_BIT);
            g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_DONT_THROTTLE_FLOATING_NUMBERS);
            g_tAppGlobals.dwDebugFlags3 |= (1 << APP_GLOBAL3_CONSOLE_COLLECTORS_EDITION);

            if (eType == GB_ITEMS) {
            }
        }
    };

    HOOK_DEFINE_REPLACE(HostArgC) {
        static auto Callback() -> u64 { return static_cast<u64>(std::size(g_cmdArgs)); }
    };

    HOOK_DEFINE_REPLACE(HostArgV) {
        static auto Callback() -> const char ** { return std::data(g_cmdArgs); }
    };

    HOOK_DEFINE_INLINE(FontStringGetRenderedSizeHook) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            auto *tProps      = *reinterpret_cast<TextRenderProperties **>(&ctx->X[0]);
            tProps->nFontSize = 19;
            tProps->flScale   = 1.5f;
        };
    };

    // d3hack-custom: WHAT does the game render once a number passes 999T?
    //
    // Its abbreviation vocabulary is exactly four localized keys -- General:StatAbbr (K),
    // ...Millions, ...Billions, ...Trillions. There is no quadrillion suffix anywhere in the
    // binary, and none of those four keys is referenced by native code at all: no adrp/add
    // pair, no pointer in .rodata or .data, no literal pool. They are consumed by the asset
    // layer, so the tier logic is not a constant table that can simply be extended.
    //
    // That leaves the question this probe answers: past a trillion, does the game print raw
    // digits, or a huge number still carrying the T suffix? The two need different fixes, and
    // guessing which is how a day gets spent on the wrong one.
    //
    // Self-discovering on purpose. Which register holds the text differs between the three
    // draw entry points, so rather than disassembling all three, scan X0..X3 for something
    // that reads as a string. Bounds-checked like every other pointer read in this fork.
    //
    // !! FONT DRAW IS EXTREMELY HOT !! Every string, every frame. Hence: off by default, one
    // cheap bool test when off, distinct values only, and a hard cap.
    inline constexpr int kNumFmtMax = 24;

    inline u64 s_arNumFmtSeen[kNumFmtMax] = {};
    inline int s_nNumFmtSeen              = 0;

    HOOK_DEFINE_INLINE(FontStringDrawHook) {
        static void Callback(exl::hook::InlineCtx *ctx) {
            if (!global_config.rare_cheats.number_format_probe)
                return;
            {
                static int s_nAll = 0;
                if (++s_nAll == 1)
                    PRINT_LINE("[d3hack-numfmt] font draw hook is LIVE");
            }
            if (s_nNumFmtSeen >= kNumFmtMax)
                return;

            for (int r = 0; r <= 3; ++r) {
                const auto uPtr = static_cast<uintptr_t>(ctx->X[r]);
                if (uPtr < 0x1000000000ull || uPtr >= 0x8000000000ull)
                    continue;
                const auto *pc = reinterpret_cast<const char *>(uPtr);

                // Accept 8-bit or UTF-16LE, whichever the entry point happens to use.
                char szBuf[48];
                int  nLen  = 0;
                bool bWide = (pc[0] != 0 && pc[1] == 0 && pc[2] != 0 && pc[3] == 0);
                for (int i = 0; i < 40; ++i) {
                    const char c = bWide ? pc[i * 2] : pc[i];
                    if (c == 0)
                        break;
                    if (c < 0x20 || c > 0x7E) {
                        nLen = 0;
                        break;
                    }
                    szBuf[nLen++] = c;
                }
                szBuf[nLen < 47 ? nLen : 47] = 0;
                if (nLen < 6)
                    continue;

                // Only strings that look like a BIG number: a digit run long enough that
                // abbreviation should already have kicked in. Without this the log fills with
                // UI labels before anything interesting is drawn -- the same budget mistake
                // the shard and area probes made.
                int nRun = 0;
                int nMax = 0;
                for (int i = 0; i < nLen; ++i) {
                    if (szBuf[i] >= '0' && szBuf[i] <= '9') {
                        ++nRun;
                        if (nRun > nMax)
                            nMax = nRun;
                    } else {
                        nRun = 0;
                    }
                }
                if (nMax < 7)
                    continue;

                u64 h = 1469598103934665603ull;
                for (int i = 0; i < nLen; ++i) {
                    h ^= static_cast<u64>(static_cast<unsigned char>(szBuf[i]));
                    h *= 1099511628211ull;
                }
                for (int i = 0; i < s_nNumFmtSeen; ++i)
                    if (s_arNumFmtSeen[i] == h)
                        return;
                s_arNumFmtSeen[s_nNumFmtSeen++] = h;
                PRINT("[d3hack-numfmt] X%d %s= \"%s\"  (longest digit run %d)", r,
                      bWide ? "utf16 " : "", szBuf, nMax)
                return;
            }
        };
    };

    HOOK_DEFINE_TRAMPOLINE(TryPopNotificationMessageHook) {
        static auto Callback(u32 *pOutMessage) -> bool {
            const bool ret = Orig(pOutMessage);
            if (ret && pOutMessage != nullptr) {
                const auto msg = static_cast<u32>(*pOutMessage);
                if (global_config.debug.log_oe_notification_messages) {
                    PRINT("TryPopNotificationMessage: %u (0x%x)", msg, msg)
                }
                return ret;
            }
            if (g_pending_dock_notify && pOutMessage != nullptr) {
                g_pending_dock_notify = false;
                *pOutMessage          = 0x1Eu;
                if (global_config.debug.log_oe_notification_messages) {
                    PRINT_LINE("TryPopNotificationMessage: forced 0x1E");
                }
                return true;
            }
            if (g_pending_perf_notify && pOutMessage != nullptr) {
                g_pending_perf_notify = false;
                *pOutMessage          = 0x1Fu;
                if (global_config.debug.log_oe_notification_messages) {
                    PRINT_LINE("TryPopNotificationMessage: forced 0x1F");
                }
                return true;
            }
            return ret;
        }
    };

    inline void SetupUtilityDebugHooks() {
        Enum_GB_Types::
            InstallAtFuncPtr(GBEnumerate);  // A relatively consistent call to refresh without spamming every frame

        if (!kEnableCmdlineHooks && global_config.debug.enable_debug_flags) {
            PRINT_LINE("[util] debug_flags enabled but cmdline hooks are disabled by build (kEnableCmdlineHooks=0)");
        }
        if (kEnableCmdlineHooks && global_config.debug.enable_debug_flags) {
            CmdLineParse::InstallAtFuncPtr(cmd_line_parse);
            HostArgC::InstallAtFuncPtr(nn::os::GetHostArgc);
            HostArgV::InstallAtFuncPtr(nn::os::GetHostArgv);
        }

        // TODO: scale based on output target (res hack).
        // FontStringGetRenderedSizeHook::
        //     InstallAtSymbol("sym_font_string_get_rendered_size");
        // d3hack-custom: NumberFormatProbe lives on this hook, so it must install for the
        // probe too -- not only under debug.active, which is not even present in the
        // shipped config. A flag that switches a feature on has to appear in that
        // feature's install condition; four things in this fork were dead today for
        // exactly this reason.
        if (global_config.debug.active || global_config.rare_cheats.number_format_probe) {
            FontStringDrawHook::
                InstallAtSymbol("sym_font_string_draw_03ff50");
            FontStringDrawHook::
                InstallAtSymbol("sym_font_string_draw_03e5b0");
            FontStringDrawHook::
                InstallAtSymbol("sym_font_string_draw_0010c8");
        }

        if (global_config.debug.enable_oe_notification_hook) {
            TryPopNotificationMessageHook::InstallAtFuncPtr(nn::oe::TryPopNotificationMessage);
        }
    }

#if D3HACK_ENABLE_UTILITY_SCRATCH
    [[maybe_unused]] inline void UtilityScratchNotes() {
        // VarResHook scratch.
        // if (global_config.overlays.active && global_config.overlays.var_res_label) {
        //     ctx->X[1] = reinterpret_cast<uintptr_t>(&g_szVariableResString);
        //     ctx->X[2] = g_ptGfxData->workerData[0].dwRTHeight;
        //     ctx->X[3] = static_cast<uint32>(ptVarRWindow->flPercent * g_ptGfxData->tCurrentMode.dwHeight);
        // }
        // if (global_config.overlays.active && global_config.overlays.var_res_label)
        //     AppDrawFlagSet(APP_DRAW_VARIABLE_RES_DEBUG_BIT, 1);
        // if (global_config.overlays.active && global_config.overlays.fps_label)
        //     AppDrawFlagSet(APP_DRAW_FPS_BIT, 1);
        // [[maybe_unused]] auto dwHeight     = g_ptGfxData->tCurrentMode.dwHeight;
        // [[maybe_unused]] auto dwUIHeight   = g_ptGfxData->tCurrentMode.dwUIOptHeight;
        // [[maybe_unused]] auto rtHeight     = g_ptGfxData->workerData[0].dwRTHeight;
        // [[maybe_unused]] auto flResScale   = g_ptMainRWindow->m_ptVariableResRWindowData->flPercent;
        // ptVarRWindow->flPercent = (ptVarRWindow->flPercent * rtHeight);
        // ptVarRWindow->dwAvgHistoryUs = dwHeight;
        // PRINT("VarResHook: UIHeight=%u, RTHeight=%u, Height=%u, Scale=%.3f", dwUIHeight, rtHeight, dwHeight, flResScale)
        // PRINT("VarResHook: RTHeight=%u, RTWidth=%u, flRTHeight=%.3f, flRTWidth=%.3f, flRTInvHeight=%.3f, flRTInvWidth=%.3f", g_ptGfxData->workerData[0].dwRTHeight, g_ptGfxData->workerData[0].dwRTWidth, g_ptGfxData->workerData[0].flRTHeight, g_ptGfxData->workerData[0].flRTWidth, g_ptGfxData->workerData[0].flRTInvHeight, g_ptGfxData->workerData[0].flRTInvWidth)
        // ptVarRWindow->flMinPercent   = 0.75f;
        // ptVarRWindow->flMaxPercent   = 1.25f;
        // ptVarRWindow->dwAvgHistoryUs = dwHeight;
        // g_ptGfxData->tCurrentMode.dwHeight     = static_cast<uint32>(1080);
        // g_ptGfxData->tCurrentMode.dwWidth      = static_cast<uint32>(1920);
        // g_ptGfxData->tCurrentMode.dwUIOptWidth = static_cast<uint32>(1080);
        // g_ptGfxData->tCurrentMode.dwUIOptWidth = static_cast<uint32>(1920);
        // PRINT_EXPR("%f", ptVarRWindow->flMinPercent)
        // PRINT_EXPR("%f", ptVarRWindow->flMaxPercent)
        // ptVarRWindow->flMinPercent = 0.15f;
        // 4096 =  3.8209f; // 2160 = 2.015625f; // 1440 = 1.34375f; // 1080 = 1.0075f;
        // ptVarRWindow->flMaxPercent = 1.0f;
        // return;
        // PRINT_EXPR("%i", ptVarRWindow->dwMinUs)
        // PRINT_EXPR("%i", ptVarRWindow->dwMaxUs)
        // PRINT_EXPR("%i", ptVarRWindow->dwAdjustCooldown)
        // PRINT_EXPR("%f", ptVarRWindow->flPercentIncr)

        // Enum_GB_Types scratch.
        // PRINT_EXPR("uint: %s", XVarUint32_ToString(&g_varSeasonNum).m_elements)
        // PRINT_EXPR("uint: %s", XVarUint32_ToString(&g_varSeasonState).m_elements)
        // auto pszFileData = std::make_shared<std::string>(c_szSeasonSwap);
        // *pszFileData = testingok;
        // OnSeasonsFileRetrieved(nullptr, 0, &pszFileData);
        // XVarUint32_Set(&g_varSeasonNum, 30, 3u);
        // XVarUint32_Set(&g_varSeasonState, 1, 3u);
        // PRINT_EXPR("uint-post: %s", XVarUint32_ToString(&g_varSeasonNum).m_elements)
        // PRINT_EXPR("uint-post: %s", XVarUint32_ToString(&g_varSeasonState).m_elements)
        // XVarBool_Set(&g_varOnlineServicePTR, true, 3u);
        // XVarBool_Set(&g_varLocalLoggingEnable, true, 3u);
        // XVarBool_Set(&g_varChallengeRiftEnabled, true, 3u);
        // XVarBool_Set(&g_varExperimentalScheduling, true, 3u);
        // g_tAppGlobals->eDebugDisplayMode = DDM_CONVERSATIONS // DDM_SOUND_STATUS // DDM_FPS_SIMPLE; // DDM_FPS_QA // DDM_FPS_PROGRAMMER
        // g_tAppGlobals.eDebugDisplayMode = DDM_LOOT_REST_BONUS;
        // g_tAppGlobals.flDebugDrawRadius = 0.4f;
        // g_tAppGlobals.eStringDebugMode  = STRINGDEBUG_LABEL;
        // AppDrawFlagSet(APP_DRAW_VARIABLE_RES_DEBUG_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_FPS_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_NO_VARIABLE_RES_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_SSAO_DEBUG_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_DRLG_INFO_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_DISABLE_LOS_BIT, 1);
        // AppDrawFlagSet(APP_DRAW_NO_CONSOLE_ICONS_BIT, 1);
        // AppGlobalFlagDisable(-1, 0u);
        // g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_FLYTHROUGH_BIT);
        // g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_FORCE_SPAWN_ALL_GIZMO_LOCATIONS);
        // g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_DISPLAY_ALL_SKILLS);
        // g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_DISPLAY_ITEM_AFFIXES);
        // g_tAppGlobals.dwDebugFlags |= (1 << APP_GLOBAL_DISPLAY_ITEM_ATTRIBUTES);
        // g_tAppGlobals.dwDebugFlags2 |= (1 << APP_GLOBAL2_DISPLAY_ITEM_TARGETED_CLASS);
        // g_tAppGlobals.dwDebugFlags3 |= (1 << APP_GLOBAL3_ANCIENT_ROLLS_WILL_ALWAYS_SUCCEED);
        // g_tAppGlobals.dwDebugFlags3 |= (1 << APP_GLOBAL3_ANCIENTS_ALWAYS_ROLL_PRIMAL);
        // g_tAppGlobals.dwDebugFlags3 |= (1 << APP_GLOBAL3_LOW_HEALTH_SCREEN_GLOW_DISABLED);
        // g_tAppGlobals.dwDebugFlags3 = 0xFFFFFFFF;
        // g_tAppGlobals.dwDebugFlags3 ^= ~(1 << APP_GLOBAL3_ANCIENT_ROLLS_WILL_ALWAYS_SUCCEED);
        // AppGlobalFlagDisable(1, APP_GLOBAL_NET_COLLECT_DATA_BIT);
        // AppGlobalFlagDisable(1, APP_GLOBAL_AI_DISABLED_BIT);
        // AppGlobalFlagDisable(1, APP_GLOBAL_AI_BLIND_BIT);
        // AppGlobalFlagDisable(1, APP_GLOBAL_AI_BLIND_TO_PLAYERS_BIT);
        // AppGlobalFlagDisable(1, APP_GLOBAL_FORCE_WALK_BIT);
        // AppGlobalFlagDisable(1, APP_GLOBAL_TRACE_STAT_BIT);
        // AppGlobalFlagDisable(2, APP_GLOBAL2_DONT_IDENTIFY_RARES_BIT);
        // AppGlobalFlagDisable(2, APP_GLOBAL2_USE_CONSOLE_OBSERVER_BIT);
        // AppGlobalFlagDisable(3, APP_GLOBAL3_PLAYER_MUTE_REQUESTED_BIT);

        // FontStringDrawHook scratch.
        // ++g_drawCount;
        // auto *tProps = *reinterpret_cast<TextRenderProperties **>(&ctx->X[0]);
        // auto *vecPointUIC = *reinterpret_cast<Vector2D **>(&ctx->X[2]);
        // if (g_drawCount < 2) {
        //     if (vecPointUIC != nullptr) {
        //         PRINT_EXPR("%f %f", vecPointUIC->x, vecPointUIC->y);
        //         PRINT_EXPR("%f %f %f", tProps->flStereoDepth, tProps->flAdvanceScalar, tProps->flScale);
        //         PRINT_EXPR("%i %i", tProps->nFontSize, tProps->snoFont);
        //         PRINT_EXPR("%i %i", tProps->fSnapToPixelCenterX, tProps->fSnapToPixelCenterY);
        //     }
        // }
        // vecPointUIC->x = 0.75 * vecPointUIC->x;
        // vecPointUIC->y += 2.0f;
    }
#endif  // D3HACK_ENABLE_UTILITY_SCRATCH

}  // namespace d3
#endif  // D3HACK_ENABLE_UTILITY_DEBUG_HOOKS
