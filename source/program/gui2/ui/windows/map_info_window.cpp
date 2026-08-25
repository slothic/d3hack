#include "program/gui2/ui/windows/map_info_window.hpp"

namespace d3::gui2::ui::windows {

    MapInfoWindow::MapInfoWindow()
        : ui::Window("d3hack Map Info", true) {
        SetFlags(ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_AlwaysAutoResize);
        SetOpen(true);
    }

    void MapInfoWindow::SetViewportSize(ImVec2 viewport_size) {
        viewport_size_ = viewport_size;
    }

    void MapInfoWindow::SetMap(std::string_view current_map, std::string_view next_map,
                               int gr_level) {
        current_map_.assign(current_map);
        next_map_.assign(next_map);
        gr_level_ = gr_level;
        SetOpen(true);
    }

    void MapInfoWindow::Clear() {
        current_map_.clear();
        next_map_.clear();
        gr_level_ = 0;
    }

    void MapInfoWindow::BeforeBegin() {
        if (viewport_size_.x > 0.0f && viewport_size_.y > 0.0f) {
            // Anchored by its TOP-RIGHT, tucked under the minimap. The minimap occupies the
            // top-right corner and is roughly square, so drop below it by a proportion of
            // height rather than a pixel count -- that holds at handheld and docked alike,
            // which a fixed offset would not.
            const float right_pad = viewport_size_.x * 0.015f;
            const float below_map = viewport_size_.y * 0.30f;
            ImGui::SetNextWindowPos(ImVec2(viewport_size_.x - right_pad, below_map),
                                    ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        }
        ImGui::SetNextWindowBgAlpha(0.0f);
    }

    void MapInfoWindow::RenderContents() {
        // Nothing to say outside a rift: an empty panel is worse than no panel.
        if (current_map_.empty()) {
            return;
        }

        // Same 0.80 scale as the combat log so the two read as one HUD rather than two
        // unrelated overlays.
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.80f);

        if (gr_level_ > 0) {
            ImGui::TextColored(color_, "GR: %d - Current map:", gr_level_);
        } else {
            ImGui::TextColored(color_, "Current map:");
        }
        ImGui::TextColored(color_, "%s", current_map_.c_str());
        ImGui::TextColored(color_, "Next Map:");
        ImGui::TextColored(color_, "%s", next_map_.empty() ? "Unknown" : next_map_.c_str());

        ImGui::PopFont();
    }

}  // namespace d3::gui2::ui::windows
