#pragma once

#include "shell/bar/widget_definition.h"
#include "shell/bar/widgets/taskbar_widget.h"

[[nodiscard]] const nythera::bar::WidgetDefinition<TaskbarWidgetOptions>& taskbarWidgetDefinition();
