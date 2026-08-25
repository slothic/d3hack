#include "program/gui2/ui/windows/combat_log_window.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace d3::gui2::ui::windows {

    CombatLogWindow::CombatLogWindow()
        : ui::Window("d3hack Combat Log", true) {
        // NoInputs is the important one -- this window sits on screen for the whole
        // session, so it must never take a click, a stick, or nav focus. NoNav and
        // NoFocusOnAppearing keep it out of gamepad navigation entirely.
        SetFlags(ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_AlwaysAutoResize);
        SetOpen(true);
    }

    void CombatLogWindow::SetViewportSize(ImVec2 viewport_size) {
        viewport_size_ = viewport_size;
    }

    void CombatLogWindow::SetMaxLines(int lines) {
        max_lines_ = std::clamp(lines, 1, 40);
        while (static_cast<int>(entries_.size()) > max_lines_) {
            entries_.pop_front();
        }
    }

    void CombatLogWindow::AddLine(const ImVec4 &color, std::string_view text) {
        // Session clock rather than wall time: getting a localised wall clock out of the
        // Switch here would be a dependency for a cosmetic prefix, and mm:ss is what you
        // actually want mid-rift anyway ("how long ago did I pull this").
        const int nTotal = static_cast<int>(clock_s_);
        const int nMin   = (nTotal / 60) % 100;
        const int nSec   = nTotal % 60;

        std::array<char, 320> buf {};
        std::snprintf(buf.data(), buf.size(), "[%02d:%02d] %.*s", nMin, nSec,
                      static_cast<int>(text.size()), text.data());

        CombatLogEntry e {};
        e.text  = buf.data();
        e.color = color;
        e.age_s = 0.0f;
        entries_.push_back(std::move(e));

        while (static_cast<int>(entries_.size()) > max_lines_) {
            entries_.pop_front();
        }
        SetOpen(true);
    }

    void CombatLogWindow::Clear() {
        entries_.clear();
    }

    void CombatLogWindow::Update(float dt_s) {
        clock_s_ += dt_s;
        for (auto &e : entries_) {
            e.age_s += dt_s;
        }
        // Lines are dropped only once fully faded, so the pane empties itself between
        // fights instead of sitting there with stale text.
        // hold_s_ <= 0 means "never expire": entries stay until the ring is full and the
        // oldest is pushed out by a newer one. The pack you killed two minutes ago is still
        // worth reading, and a log that erases itself is the thing the fade was for when it
        // was a transient toast rather than a history.
        while (hold_s_ > 0.0f && !entries_.empty() &&
               entries_.front().age_s > (hold_s_ + fade_s_)) {
            entries_.pop_front();
        }
    }

    void CombatLogWindow::BeforeBegin() {
        if (viewport_size_.x > 0.0f && viewport_size_.y > 0.0f) {
            // Bottom-CENTRE, anchored by its bottom-middle so the pane grows upward as
            // lines arrive -- the same direction PC's chat history grows. The bottom strip
            // either side is occupied: the orb and skill bar on the left, the minimap on the
            // right, so the middle is the free lane.
            // Sit in the empty strip along the BOTTOM of the screen. The HUD only occupies
            // the corners -- orb and skills on the left, minimap on the right -- so the
            // centre column is free all the way down, and using it keeps the log out of the
            // play area entirely. Proportional rather than a fixed pixel count so it holds
            // at handheld and docked resolutions alike.
            const float bottom_hud = 5.0f;   // right down on the edge
            ImGui::SetNextWindowPos(
                ImVec2(viewport_size_.x * 0.5f, viewport_size_.y - bottom_hud),
                ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        }
        ImGui::SetNextWindowBgAlpha(0.0f);
    }

    void CombatLogWindow::RenderContents() {
        if (entries_.empty()) {
            return;
        }

        // Smaller than the rest of the overlay: this is a history you glance at, not a
        // notification, and the affix lists make the lines long enough to want the room.
        // ImGui 1.92 dropped SetWindowFontScale in favour of PushFont(font, size); NULL
        // keeps the current font and only changes the size.
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.80f);

        for (const auto &e : entries_) {
            float fade = 1.0f;
            if (hold_s_ > 0.0f && e.age_s > hold_s_ && fade_s_ > 0.0f) {
                fade = 1.0f - ((e.age_s - hold_s_) / fade_s_);
                fade = std::clamp(fade, 0.0f, 1.0f);
            }
            if (fade <= 0.0f) {
                continue;
            }

            // Split the line into coloured runs. The producer marks a colour change with
            // 0x01 followed by six hex digits; everything up to the next marker is drawn in
            // that colour. This keeps PostCombatLog's printf-style interface intact -- the
            // alternative was passing arrays of segments through every call site, which
            // would have made the game-side code far messier for a purely visual gain.
            struct Seg {
                const char *p;
                int         n;
                ImVec4      c;
            };
            Seg    segs[24];
            int    nseg = 0;
            ImVec4 cur  = e.color;
            const char *p = e.text.c_str();
            const char *run_start = p;
            auto push = [&](const char *q) {
                if (nseg < 24 && q > run_start) {
                    segs[nseg++] = {run_start, static_cast<int>(q - run_start), cur};
                }
            };
            auto hexval = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            while (*p != '\0') {
                if (static_cast<unsigned char>(*p) == 0x01) {
                    push(p);
                    int v[6] = {};
                    bool ok = true;
                    for (int i = 0; i < 6; ++i) {
                        v[i] = hexval(p[1 + i]);
                        if (v[i] < 0) { ok = false; break; }
                    }
                    if (ok) {
                        cur = ImVec4(static_cast<float>(v[0] * 16 + v[1]) / 255.0f,
                                     static_cast<float>(v[2] * 16 + v[3]) / 255.0f,
                                     static_cast<float>(v[4] * 16 + v[5]) / 255.0f, 1.0f);
                        p += 7;
                    } else {
                        p += 1;
                    }
                    run_start = p;
                    continue;
                }
                ++p;
            }
            push(p);
            if (nseg == 0) {
                continue;
            }

            // Centre on the TOTAL width of all runs, not on one string.
            float total = 0.0f;
            for (int i = 0; i < nseg; ++i) {
                total += ImGui::CalcTextSize(segs[i].p, segs[i].p + segs[i].n).x;
            }
            const float pane_w = ImGui::GetContentRegionAvail().x;
            const float x0     = ImGui::GetCursorPosX() +
                             ((pane_w > total) ? (pane_w - total) * 0.5f : 0.0f);
            ImGui::SetCursorPosX(x0);

            const ImVec2 row = ImGui::GetCursorPos();
            // Drop shadow first, as one pass over the same runs: the log sits over whatever
            // the game is drawing and light text on a bright floor is unreadable without it.
            ImGui::SetCursorPos(ImVec2(row.x + 1.0f, row.y + 1.0f));
            for (int i = 0; i < nseg; ++i) {
                if (i > 0) ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.75f * fade), "%.*s",
                                   segs[i].n, segs[i].p);
            }
            ImGui::SetCursorPos(row);
            for (int i = 0; i < nseg; ++i) {
                if (i > 0) ImGui::SameLine(0.0f, 0.0f);
                ImVec4 c = segs[i].c;
                c.w *= fade;
                ImGui::TextColored(c, "%.*s", segs[i].n, segs[i].p);
            }
        }

        ImGui::PopFont();
    }

}  // namespace d3::gui2::ui::windows
