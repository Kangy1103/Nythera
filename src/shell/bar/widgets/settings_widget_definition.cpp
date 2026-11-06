#include "shell/bar/widgets/settings_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const nythera::bar::WidgetDefinition<SettingsWidget::Options>& settingsWidgetDefinition() {
  using Options = SettingsWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "settings",
      .fields = nythera::bar::glyphButtonFields<Options>(),
      .glyph = [](const Options& options) { return options.glyph; },
  };
  return definition;
}
