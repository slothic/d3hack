#pragma once

#include "program/gui2/ui/overlay.hpp"

namespace d3::imgui_overlay {
    void Initialize();
    void PrepareFonts();

    const d3::gui2::ui::GuiFocusState &GetGuiFocusState();
    // Post a formatted notification to the overlay notifications queue.
    // Safe to call from other modules; does nothing if overlay/windows are not yet created.
    void PostOverlayNotification(const ImVec4 &color, float ttl_s, const char *fmt, ...);

    auto GetTitleFont() -> ImFont *;
    auto GetBodyFont() -> ImFont *;
    // d3hack-custom: post one line to the combat log. Safe to call from game hooks;
    // it is a no-op until the overlay has built its windows.
    void PostCombatLog(float r, float g, float b, const char *fmt, ...);
    // d3hack-custom: set the persistent map panel under the minimap. Empty current hides it.
    void SetMapInfo(const char *current_map, const char *next_map, int gr_level);

}  // namespace d3::imgui_overlay
