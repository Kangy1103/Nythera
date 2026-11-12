#pragma once

#include "compositors/window_inspector.h"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <type_traits>

namespace compositors::umbriel {
  template <typename T> std::optional<T> inspectorValue(const nlohmann::json& entry, std::string_view key) {
    const auto it = entry.find(key);
    if (it == entry.end())
      return std::nullopt;
    if constexpr (std::is_same_v<T, bool>) {
      if (it->is_boolean())
        return it->get<bool>();
    } else {
      if (it->is_number_integer()) {
        if (it->is_number_unsigned()
            && it->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
          return std::nullopt;
        const auto value = it->get<std::int64_t>();
        if (value >= std::numeric_limits<T>::min() && value <= std::numeric_limits<T>::max())
          return static_cast<T>(value);
      }
    }
    return std::nullopt;
  }

  inline InspectorWindowDetails parseInspectorDetails(const nlohmann::json& entry) {
    InspectorWindowDetails details;
    const auto stringValue = [&](std::string_view key) {
      const auto it = entry.find(key);
      return it != entry.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    details.workspaceId = stringValue("workspace");
    details.output = stringValue("output");
    details.scratchpad = stringValue("scratchpad");
    details.parent = stringValue("parent");
    details.region = stringValue("region");
    details.pid = inspectorValue<std::int64_t>(entry, "pid");
    if (details.pid && *details.pid <= 0)
      details.pid.reset();
    details.x = inspectorValue<int>(entry, "x");
    details.y = inspectorValue<int>(entry, "y");
    details.width = inspectorValue<int>(entry, "w");
    details.height = inspectorValue<int>(entry, "h");
    details.column = inspectorValue<int>(entry, "column");
    details.row = inspectorValue<int>(entry, "row");
    details.tabIndex = inspectorValue<int>(entry, "tab_index");
    details.floating = inspectorValue<bool>(entry, "floating");
    details.pinned = inspectorValue<bool>(entry, "pinned");
    details.fullscreen = inspectorValue<bool>(entry, "fullscreen");
    details.maximized = inspectorValue<bool>(entry, "maximized");
    details.xwayland = inspectorValue<bool>(entry, "xwayland");
    details.tabbed = inspectorValue<bool>(entry, "tabbed");
    details.tabHidden = inspectorValue<bool>(entry, "tab_hidden");
    details.visible = inspectorValue<bool>(entry, "visible");
    details.active = inspectorValue<bool>(entry, "active").value_or(false);
    return details;
  }

  inline InspectorWindowCapabilities
  inspectorCapabilities(const InspectorWindowDetails& details, const std::vector<InspectorWorkspace>& workspaces) {
    const auto workspace = std::ranges::find(workspaces, details.workspaceId, &InspectorWorkspace::id);
    const bool tiledLayout = workspace != workspaces.end()
        && (workspace->layout == "scrolling" || workspace->layout == "dwindle" || workspace->layout == "master");
    return {
        .floating = details.floating.has_value() && tiledLayout && details.scratchpad.empty(),
        .pinned = details.active && details.pinned.has_value() && details.scratchpad.empty(),
        .fullscreen = details.active && details.fullscreen.has_value(),
        .maximized = details.active && details.maximized.has_value(),
        .moveToWorkspace = details.active && workspace != workspaces.end() && details.scratchpad.empty(),
        .kill = details.pid.has_value() || details.xwayland.value_or(false),
    };
  }

  inline bool inspectorActionNeedsFocus(InspectorWindowAction action) {
    return action != InspectorWindowAction::Floating && action != InspectorWindowAction::Kill;
  }

  inline std::optional<nlohmann::json> inspectorActionFields(
      std::string_view id, const InspectorWindowDetails& details, const std::vector<InspectorWorkspace>& workspaces,
      InspectorWindowAction action, std::string_view destination, std::string_view effectiveFocusedId
  ) {
    if (id.empty() || (inspectorActionNeedsFocus(action) && effectiveFocusedId != id))
      return std::nullopt;
    // The authoritative focused-window query can differ from the cached activation
    // bit while a layer surface holds keyboard focus.
    auto current = details;
    if (inspectorActionNeedsFocus(action))
      current.active = true;
    const auto capabilities = inspectorCapabilities(current, workspaces);
    switch (action) {
    case InspectorWindowAction::Floating:
      if (capabilities.floating)
        return nlohmann::json{{"action", "window-toggle-floating"}, {"window", id}};
      break;
    case InspectorWindowAction::Pinned:
      if (capabilities.pinned)
        return nlohmann::json{{"action", "window-toggle-pinned"}};
      break;
    case InspectorWindowAction::Fullscreen:
      if (capabilities.fullscreen)
        return nlohmann::json{{"action", "window-toggle-fullscreen"}};
      break;
    case InspectorWindowAction::Maximized:
      if (capabilities.maximized)
        return nlohmann::json{{"action", "window-toggle-maximize"}};
      break;
    case InspectorWindowAction::MoveToWorkspace: {
      const auto workspace = std::ranges::find(workspaces, destination, &InspectorWorkspace::id);
      if (capabilities.moveToWorkspace
          && workspace != workspaces.end()
          && workspace->named.has_value()
          && !workspace->output.empty()) {
        nlohmann::json destinationValue;
        if (*workspace->named && !workspace->name.empty())
          destinationValue = workspace->name;
        else if (!*workspace->named && workspace->index && *workspace->index > 0)
          destinationValue = *workspace->index;
        else
          return std::nullopt;
        return nlohmann::json{
            {"action", "window-move-to-workspace-silent"},
            {"workspace", destinationValue},
            {"output", workspace->output}
        };
      }
      break;
    }
    case InspectorWindowAction::Kill:
      if (capabilities.kill)
        return nlohmann::json{{"action", "window-kill"}, {"window", id}};
      break;
    }
    return std::nullopt;
  }
} // namespace compositors::umbriel
