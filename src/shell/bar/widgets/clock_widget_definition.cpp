#include "shell/bar/widgets/clock_widget_definition.h"

const nythera::bar::WidgetDefinition<ClockWidget::Options>& clockWidgetDefinition() {
  using nythera::bar::field;
  using Options = ClockWidget::Options;

  static const nythera::bar::WidgetDefinition<Options> definition{
      .type = "clock",
      .fields = {
          field<&Options::format>({
              .key = "format",
          }),
          field<&Options::verticalFormat>({
              .key = "vertical_format",
          }),
          field<&Options::tooltipFormat>({
              .key = "tooltip_format",
          }),
          field<&Options::timezone>({
              .key = "timezone",
          }),
      },
  };
  return definition;
}
