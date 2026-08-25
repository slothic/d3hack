#pragma once

// d3hack-custom: a D3-PC-style combat log.
//
// Deliberately NOT the NotificationsWindow. That one is the big centred toast used for
// "Applied.", "Saved config.toml" and so on; this is the scrolling, timestamped,
// bottom-left pane that PC players read to see which pack they just pulled.
//
// Two properties matter and both are why this is its own window:
//   - ImGuiWindowFlags_NoInputs. It can never take a click, a stick or nav focus, so it
//     is safe to leave on screen permanently while playing.
//   - It renders whenever the overlay is RENDERING, not when the overlay is VISIBLE.
//     `[gui] Enabled = true` with `Visible = false` gives the log with no menu and no
//     input capture, which is the whole point -- the menu was never wanted.

#include <deque>
#include <string>
#include <string_view>

#include "program/gui2/ui/window.hpp"

namespace d3::gui2::ui::windows {

    struct CombatLogEntry {
        std::string text;
        ImVec4      color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        float       age_s = 0.0f;
    };

    class CombatLogWindow : public ui::Window {
       public:
        CombatLogWindow();
        ~CombatLogWindow() override = default;

        void SetViewportSize(ImVec2 viewport_size);

        // Text is prefixed with a [mm:ss] stamp taken from the session clock.
        void AddLine(const ImVec4 &color, std::string_view text);
        void Clear();

        void SetMaxLines(int lines);
        void SetHoldSeconds(float hold_s) { hold_s_ = hold_s; }
        void SetFadeSeconds(float fade_s) { fade_s_ = fade_s; }

       protected:
        void Update(float dt_s) override;
        void BeforeBegin() override;
        void RenderContents() override;

       private:
        ImVec2                    viewport_size_ {};
        std::deque<CombatLogEntry> entries_ {};
        int                        max_lines_ = 10;
        // Hold for three minutes, then fade. Long enough that the whole pull is still
        // readable after a fight, short enough that a stale rift's packs are not still on
        // screen in the next one. 0 would mean never fade.
        float                      hold_s_    = 180.0f;
        float                      fade_s_    = 5.0f;
        float                      clock_s_   = 0.0f;
    };

}  // namespace d3::gui2::ui::windows
