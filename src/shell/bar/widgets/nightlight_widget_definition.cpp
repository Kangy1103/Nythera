#include "shell/bar/widgets/nightlight_widget_definition.h"

const nythera::bar::WidgetDefinition<std::monostate>& nightlightWidgetDefinition() {
  static const nythera::bar::WidgetDefinition<std::monostate> definition{
      .type = "nightlight",
  };
  return definition;
}
