#pragma once

// d3hack-custom: a small persistent panel under the minimap naming the current rift map.
//
// Deliberately NOT the combat log. The log is a scrolling history that fades, which is wrong
// for this: the map name is state, not an event. You want to be able to glance up at any point
// in a floor and read what you are standing in -- especially while deciding what to put in
// BannedRiftMaps -- and a line that scrolled away ten pulls ago cannot do that.
//
// Same two properties as the combat log, for the same reasons:
//   - ImGuiWindowFlags_NoInputs, so it can sit on screen permanently and never take focus.
//   - Renders whenever the overlay is RENDERING, not when it is VISIBLE, so it works with
//     `[gui] Enabled = true, Visible = false` -- HUD, no menu, no input capture.

#include <string>

#include "program/gui2/ui/window.hpp"

namespace d3::gui2::ui::windows {

    class MapInfoWindow : public ui::Window {
       public:
        MapInfoWindow();
        ~MapInfoWindow() override = default;

        void SetViewportSize(ImVec2 viewport_size);

        // Empty current_map hides the panel entirely -- outside a rift there is nothing to say.
        void SetMap(std::string_view current_map, std::string_view next_map, int gr_level);
        void Clear();

        void SetColor(const ImVec4 &c) { color_ = c; }

       protected:
        void BeforeBegin() override;
        void RenderContents() override;

       private:
        ImVec2      viewport_size_ {};
        std::string current_map_ {};
        std::string next_map_ {};
        int         gr_level_ = 0;
        ImVec4      color_    = ImVec4(0.85f, 0.20f, 0.20f, 1.0f);
    };

}  // namespace d3::gui2::ui::windows
