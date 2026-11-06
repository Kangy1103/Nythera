#include "shell/bar/widgets/control_center_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const nythera::bar::WidgetDefinition<ControlCenterWidget::Options>& controlCenterWidgetDefinition() {
  using Options = ControlCenterWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "control-center",
      .fields = nythera::bar::glyphButtonFields<Options>(),
      .glyph = [](const Options& options) { return options.glyph; },
  };
  return definition;
}
