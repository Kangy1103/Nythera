#include "config/schema/config_schema.h"
#include "config/schema/engine.h"
#include "core/toml.h"
#include "shell/bar/dynamic_island.h"
#include "shell/bar/island_activities.h"
#include "shell/bar/island_notice_queue.h"
#include "shell/bar/island_notification.h"
#include "shell/osd/media_osd.h"
#include "shell/settings/settings_registry.h"

#include <cassert>

int main() {
  {
    struct Notice { std::uint32_t id; int value; };
    IslandNoticeQueue<Notice> queue;
    assert(queue.push({1, 10}, 3500));
    assert(queue.push({2, 20}, 3500));
    assert(!queue.advance(1000, false, false));
    assert(!queue.advance(9000, false, false)); // a call has hidden both notices
    assert(queue.front()->remaining == 3500);
    assert(!queue.advance(9001, true, false));
    assert(!queue.advance(10001, true, false));
    assert(queue.front()->remaining == 2500);
    assert(!queue.advance(10002, true, true));
    assert(!queue.advance(20000, true, true)); // hover/native tool holds its dwell
    assert(queue.front()->remaining == 2500);
    assert(!queue.advance(20001, true, false));
    assert(queue.advance(22501, true, false)->id == 1);
    assert(queue.front()->notice.id == 2 && queue.front()->remaining == 3500);
    assert(queue.push({2, 99}, 4000) && queue.size() == 1);
    assert(queue.front()->notice.value == 99 && queue.front()->remaining == 4000);
    for (std::uint32_t id = 3; id <= 9; ++id) assert(queue.push({id, 0}, 3500));
    assert(!queue.push({10, 0}, 3500)); // caller falls back to its ordinary toast
    assert(queue.contains(2));
    queue.erase(2);
    assert(!queue.contains(2) && queue.front()->notice.id == 3);
  }
  MediaPlaybackStartState playback;
  assert(!playback.observe("Playing", true, true)); // Discovery is silent.
  assert(!playback.observe("Paused", true, true));
  assert(playback.observe("Playing", true, true));  // Resume.
  assert(!playback.observe("Playing", true, true)); // Position/metadata updates cannot retrigger.
  assert(!playback.observe("Stopped", true, true));
  assert(!playback.observe("Playing", false, true)); // Wait for title.
  assert(playback.observe("Playing", true, true));
  assert(!playback.observe("Paused", true, true));
  assert(!playback.observe("Playing", true, false)); // Background playback stays silent.
  assert(!playback.observe("Playing", true, true));  // Focus/player selection is not Play.
  assert(!playback.observe("Stopped", true, true));
  assert(!playback.observe("Playing", false, true));
  assert(!playback.observe("Paused", false, true)); // Pause cancels a pending start.
  assert(!playback.observe("Paused", true, true));

  assert(IslandActivities::validTime("08:00"));
  assert(!IslandActivities::validTime("24:00"));
  assert(!IslandActivities::validTime("09:60"));
  assert(!IslandActivities::validTime("garbage"));
  IslandActivities activities;
  int timerAlerts = 0, alarmAlerts = 0;
  activities.alert = [&](bool timer, bool alarm) {
    timerAlerts += timer;
    alarmAlerts += alarm;
  };
  activities.startTimer(3, nullptr, 100);
  assert(activities.countdown(101) == "0:02");
  activities.tick(103);
  assert(activities.timerFinished && !activities.timerEnd);
  activities.cancelTimer();
  assert(!activities.timerFinished);
  activities.alarmAt = 110;
  activities.tick(110);
  assert(activities.alarmRinging);
  assert(timerAlerts == 1 && alarmAlerts == 1);
  activities.tick(111);
  assert(timerAlerts == 1 && alarmAlerts == 1);
  activities.dismissAlarm();
  assert(!activities.alarmRinging);
  Notification call{};
  call.appName = "Vesktop";
  call.summary = "Alice is calling you";
  assert(isIslandCall(call));
  call.summary = "A normal message";
  assert(!isIslandCall(call));
  call.appName = "Other app";
  call.category = "call.incoming";
  assert(isIslandCall(call));
  call.category.reset();
  call.appName = "KDE Connect";
  call.category = "callReceived";
  assert(isIslandCall(call));

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

  state.push(IslandEvent::Notification, 15000, 1000);
  state.push(IslandEvent::Call, 15000, 1000);
  state.push(IslandEvent::Alarm, 15000, 1000);
  assert(state.resolve(15001) == IslandEvent::Alarm);
  state.clear(IslandEvent::Alarm);
  assert(state.resolve(15001) == IslandEvent::Call);

  assert(DynamicIsland::nativeToolContext(IslandEvent::Resting) == "home");
  assert(DynamicIsland::nativeToolContext(IslandEvent::Media) == "media");
  assert(DynamicIsland::nativeToolContext(IslandEvent::Volume) == "audio");
  assert(DynamicIsland::nativeToolContext(IslandEvent::Brightness) == "monitor");
  assert(DynamicIsland::nativeToolContext(IslandEvent::Notification) == "notifications");
  assert(DynamicIsland::nativeToolContext(IslandEvent::Call) == "notifications");
  BarConfig bar;
  using namespace noctalia::config::schema;
  Diagnostics diagnostics;
  readInto(
      toml::parse("surface_mode='dynamic'\ndynamic_visualizer=true\ndynamic_devices=false\ndynamic_media=false\ndynamic_width=900\ndynamic_dwell_ms=50"), bar,
      barFieldsSchema(), "bar.default", diagnostics
  );
  assert(bar.surfaceMode == "dynamic" && !bar.dynamicMedia && bar.dynamicVisualizer && !bar.dynamicDevices);
  assert(bar.dynamicWidth == 600 && bar.dynamicDwellMs == 500);
  BarMonitorOverride monitor;
  readInto(
      toml::parse("surface_mode='dynamic'\ndynamic_privacy=false\ndynamic_activity_indicators=false\ndynamic_visualizer=false\ndynamic_volume=false\ndynamic_width=300"), monitor,
      barMonitorOverrideSchema(), "bar.default.monitor.DP-1", diagnostics
  );
  assert(monitor.surfaceMode == "dynamic" && monitor.dynamicVolume == false && monitor.dynamicWidth == 300);
  BarMonitorOverride restored;
  readInto(
      writeTable(monitor, barMonitorOverrideSchema()), restored, barMonitorOverrideSchema(), "monitor", diagnostics
  );
  assert(restored == monitor);
  assert(monitor.dynamicPrivacy == false && monitor.dynamicActivityIndicators == false && monitor.dynamicVisualizer == false);

  Config config;
  bar.dynamicWidth = 360;
  monitor.match = "DP-1";
  bar.monitorOverrides = {monitor};
  config.bars = {bar};
  for (bool perMonitor : {false, true}) {
    const auto entries = settings::buildSettingsRegistry(config, &bar, perMonitor ? &monitor : nullptr);
    for (auto key :
         {"dynamic_notifications", "dynamic_timers", "dynamic_alarms", "dynamic_calls", "dynamic_timer_seconds",
          "dynamic_alarm_time", "dynamic_snooze_seconds", "dynamic_media", "dynamic_volume", "dynamic_brightness",
          "dynamic_hover_expand", "dynamic_dwell_ms", "dynamic_clock_format",
          "dynamic_activity_indicators", "dynamic_privacy", "dynamic_devices", "dynamic_visualizer"}) {
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
    assert(surface.nameSpace.starts_with("noctalia-bar-")); // Matches native bar layer-effect rules.
    assert(surface.width <= 853 && surface.height <= 533);
    assert(surface.width * surface.height < 853 * 533 / 2);
    assert(surface.exclusiveZone == (bar.screenWrap ? -1 : bar.thickness));
    const bool side = std::string_view(edge) == "left" || std::string_view(edge) == "right";
    assert((side ? surface.width : surface.height) == static_cast<unsigned>(bar.thickness));
    bar.reserveSpace = false;
    assert(DynamicIsland::surfaceConfig(bar, 853, 533).exclusiveZone == (bar.screenWrap ? -1 : 0));
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
