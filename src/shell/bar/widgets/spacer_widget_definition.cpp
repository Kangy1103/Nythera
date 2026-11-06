#include "shell/bar/widgets/spacer_widget_definition.h"

const nythera::bar::WidgetDefinition<SpacerWidget::Options>& spacerWidgetDefinition() {
  using nythera::bar::field;
  using Options = SpacerWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "spacer",
      .fields =
          {
              field<&Options::length>({
                  .key = "length",
                  .minValue = 0.0,
                  .maxValue = 400.0,
                  .step = 1.0,
              }),
              field<&Options::expand>({
                  .key = "expand",
              }),
          },
      // Spacers are click-through by default; the schema default has to match so a
      // configured interactive = true reads as an override rather than a default.
      .commonOverrides = {
          {
              .key = "interactive",
              .defaultValue = false,
          },
      },
  };
  return definition;
}
