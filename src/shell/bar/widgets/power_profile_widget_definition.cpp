#include "shell/bar/widgets/power_profile_widget_definition.h"

const nythera::bar::WidgetDefinition<std::monostate>& powerProfileWidgetDefinition() {
  static const nythera::bar::WidgetDefinition<std::monostate> definition{
      .type = "power_profile",
  };
  return definition;
}
