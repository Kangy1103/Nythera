#include "shell/bar/widgets/test_widget_definition.h"

const nythera::bar::WidgetDefinition<std::monostate>& testWidgetDefinition() {
  static const nythera::bar::WidgetDefinition<std::monostate> definition{
      .type = "test",
  };
  return definition;
}
