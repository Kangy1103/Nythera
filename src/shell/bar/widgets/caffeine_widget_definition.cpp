#include "shell/bar/widgets/caffeine_widget_definition.h"

const nythera::bar::WidgetDefinition<std::monostate>& caffeineWidgetDefinition() {
  static const nythera::bar::WidgetDefinition<std::monostate> definition{
      .type = "caffeine",
  };
  return definition;
}
