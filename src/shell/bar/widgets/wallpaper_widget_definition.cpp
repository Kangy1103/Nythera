#include "shell/bar/widgets/wallpaper_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const nythera::bar::WidgetDefinition<WallpaperWidget::Options>& wallpaperWidgetDefinition() {
  using Options = WallpaperWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "wallpaper",
      .fields = nythera::bar::glyphButtonFields<Options>(),
      .glyph = [](const Options& options) { return options.glyph; },
  };
  return definition;
}
