#pragma once
#include "ui/controls/box.h"
#include "ui/controls/flex.h"

namespace ui {
  // The root filled container owns a declarative layer's material. Never synthesize
  // a rectangle for a chromeless scene or tag button/text/image internals.
  inline bool markRootSurfaceBackground(Node& root) {
    if (auto* box = dynamic_cast<Box*>(&root); box && box->style().fill.a > 0) {
      box->setSurfaceBackground();
      return true;
    }
    if (auto* flex = dynamic_cast<Flex*>(&root); flex && flex->hasBackgroundFill()) {
      flex->setSurfaceBackground();
      return true;
    }
    if (root.type() != NodeType::Base)
      return false;
    for (const auto& child : root.children())
      if (markRootSurfaceBackground(*child))
        return true;
    return false;
  }
} // namespace ui
