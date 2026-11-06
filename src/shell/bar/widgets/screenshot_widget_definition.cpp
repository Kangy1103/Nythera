#include "shell/bar/widgets/screenshot_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const nythera::bar::WidgetDefinition<ScreenshotWidget::Options>& screenshotWidgetDefinition() {
  using Options = ScreenshotWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "screenshot",
      .fields = nythera::bar::glyphButtonFields<Options>(),
  };
  return definition;
}
