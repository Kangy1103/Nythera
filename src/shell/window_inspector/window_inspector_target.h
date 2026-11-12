#pragma once

#include "wayland/wayland_toplevels.h"

namespace window_inspector {
  // Exact compositor identities are authoritative. Never fall back to a title,
  // application, or reused protocol handle when an exact identity differs.
  [[nodiscard]] inline bool sameTarget(const ToplevelInfo& target, const ToplevelInfo& candidate) {
    if (target.exactIdentity) {
      return !target.identifier.empty() && candidate.exactIdentity && candidate.identifier == target.identifier;
    }
    return target.handle != nullptr && candidate.handle == target.handle;
  }
} // namespace window_inspector
