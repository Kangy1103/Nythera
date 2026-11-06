#pragma once

#include "config/config_types.h"
#include "ui/controls/color_swatch_preview.h"

namespace nythera::theme {
  struct AvailablePalette;
  struct BuiltinPalette;
} // namespace nythera::theme

// Swatch strips shown next to palette names in pickers. Light mode previews the light variant; every other mode,
// including Auto, previews the dark one.
[[nodiscard]] ColorSwatchPreview
builtinPaletteSwatchPreview(const nythera::theme::BuiltinPalette& entry, ThemeMode mode);
// Community and custom palettes carry preview metadata; a palette without a light preview falls back to its dark one.
[[nodiscard]] ColorSwatchPreview
availablePaletteSwatchPreview(const nythera::theme::AvailablePalette& entry, ThemeMode mode);
