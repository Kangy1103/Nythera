#include "config/schema/config_schema.h"
#include "config/schema/engine.h"
#include "core/toml.h"
#include "shell/bar/dynamic_island.h"
#include "shell/settings/settings_registry.h"

#include <cassert>

int main() {
  DynamicIslandState state;
  state.push(IslandEvent::Media, 0, 10000);
  state.push(IslandEvent::Volume, 100, 1000);
  assert(state.resolve(500) == IslandEvent::Volume);
  assert(state.resolve(1500) == IslandEvent::Media);
  state.push(IslandEvent::Brightness, 1600, 500);
  state.push(IslandEvent::Volume, 1700, 500);
  assert(state.resolve(1800) == IslandEvent::Volume);
  assert(state.resolve(11000, IslandEvent::Volume) == IslandEvent::Volume);
  assert(state.resolve(11000) == IslandEvent::Resting);
  state.push(IslandEvent::Volume, 12000, 1000);
  state.push(IslandEvent::Volume, 12500, 1000);
  assert(state.resolve(13200) == IslandEvent::Volume);
  assert(state.resolve(14000) == IslandEvent::Resting);

  BarConfig bar;
  using namespace noctalia::config::schema;
  Diagnostics diagnostics;
  readInto(
      toml::parse("surface_mode='dynamic'\ndynamic_media=false\ndynamic_width=900\ndynamic_dwell_ms=50"), bar,
      barFieldsSchema(), "bar.default", diagnostics
  );
  assert(bar.surfaceMode == "dynamic" && !bar.dynamicMedia);
  assert(bar.dynamicWidth == 600 && bar.dynamicDwellMs == 500);
  BarMonitorOverride monitor;
  readInto(
      toml::parse("surface_mode='dynamic'\ndynamic_volume=false\ndynamic_width=300"), monitor,
      barMonitorOverrideSchema(), "bar.default.monitor.DP-1", diagnostics
  );
  assert(monitor.surfaceMode == "dynamic" && monitor.dynamicVolume == false && monitor.dynamicWidth == 300);
  BarMonitorOverride restored;
  readInto(
      writeTable(monitor, barMonitorOverrideSchema()), restored, barMonitorOverrideSchema(), "monitor", diagnostics
  );
  assert(restored == monitor);

  Config config;
  bar.dynamicWidth = 360;
  monitor.match = "DP-1";
  bar.monitorOverrides = {monitor};
  config.bars = {bar};
  for (bool perMonitor : {false, true}) {
    const auto entries = settings::buildSettingsRegistry(config, &bar, perMonitor ? &monitor : nullptr);
    for (auto key :
         {"dynamic_media", "dynamic_volume", "dynamic_brightness", "dynamic_hover_expand", "dynamic_width",
          "dynamic_dwell_ms", "dynamic_clock_format"}) {
      const auto path = perMonitor ? std::vector<std::string>{"bar", "default", "monitor", "DP-1", key}
                                   : std::vector<std::string>{"bar", "default", key};
      assert(std::ranges::find(entries, path, &settings::SettingEntry::path) != entries.end());
    }
    const auto path = perMonitor ? std::vector<std::string>{"bar", "default", "monitor", "DP-1", "surface_mode"}
                                 : std::vector<std::string>{"bar", "default", "surface_mode"};
    const auto entry = std::ranges::find(entries, path, &settings::SettingEntry::path);
    assert(entry != entries.end());
    const auto& options = std::get<settings::SelectSetting>(entry->control).options;
    assert(std::ranges::any_of(options, [](const auto& option) { return option.value == "dynamic"; }));
  }
  for (auto edge : {"top", "bottom", "left", "right"}) {
    bar.position = edge;
    const auto surface = DynamicIsland::surfaceConfig(bar, 853, 533);
    assert(surface.width < 853 && surface.height < 533);
    assert(surface.width * surface.height < 853 * 533 / 2);
    assert(surface.exclusiveZone == bar.thickness);
    bar.reserveSpace = false;
    assert(DynamicIsland::surfaceConfig(bar, 853, 533).exclusiveZone == 0);
    bar.reserveSpace = true;
  }
  OsdOverlay overlay;
  int intercepted = 0;
  overlay.setContentInterceptor([&](const OsdContent&) {
    ++intercepted;
    return true;
  });
  overlay.show({.kind = OsdKind::Volume});
  assert(intercepted == 1);
}
