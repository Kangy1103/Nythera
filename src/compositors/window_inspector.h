#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class InspectorWindowAction { Floating, Pinned, Fullscreen, Maximized, MoveToWorkspace, Kill };

struct InspectorWorkspace {
  std::string id;
  std::string name;
  std::string output;
  std::string layout;
  std::optional<int> index;
  std::optional<bool> named;
  bool operator==(const InspectorWorkspace&) const = default;
};

struct InspectorWindowDetails {
  std::string workspaceId;
  std::string output;
  std::string scratchpad;
  std::string parent;
  std::string region;
  std::optional<std::int64_t> pid;
  std::optional<int> x, y, width, height, column, row, tabIndex;
  std::optional<bool> floating, pinned, fullscreen, maximized, xwayland, tabbed, tabHidden, visible;
  bool active = false;
  bool operator==(const InspectorWindowDetails&) const = default;
};

struct InspectorWindowCapabilities {
  bool floating = false;
  bool pinned = false;
  bool fullscreen = false;
  bool maximized = false;
  bool moveToWorkspace = false;
  bool kill = false;
};
