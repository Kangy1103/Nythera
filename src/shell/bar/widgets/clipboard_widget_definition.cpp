#include "shell/bar/widgets/clipboard_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const nythera::bar::WidgetDefinition<ClipboardWidget::Options>& clipboardWidgetDefinition() {
  using Options = ClipboardWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "clipboard",
      .fields = nythera::bar::glyphButtonFields<Options>(),
      .glyph = [](const Options& options) { return options.glyph; },
  };
  return definition;
}
