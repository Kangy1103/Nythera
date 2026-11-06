#pragma once

#include "shell/bar/widget_definition.h"
#include "shell/bar/widgets/battery_widget.h"

struct BatteryConfig;

struct BatteryWidgetDefinitionContext {
  const BatteryConfig* batteryConfig = nullptr;
  UPowerService* upower = nullptr;
};

[[nodiscard]] const nythera::bar::WidgetDefinition<BatteryWidget::Options, BatteryWidgetDefinitionContext>&
batteryWidgetDefinition();
