#include "shell/bar/widgets/text_widget_definition.h"

const nythera::bar::WidgetDefinition<TextWidget::Options>& textWidgetDefinition() {
  using nythera::bar::field;
  using Options = TextWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "text",
      .fields = {
          field<&Options::text>({
              .key = "text",
          }),
      },
  };
  return definition;
}
