#include "shell/bar/screen_wrap_geometry.h"
#include "shell/settings/settings_registry.h"

#include <cassert>
#include <string_view>
#include <utility>

int main() {
  const auto square = screen_wrap::geometry(3440, 1440, 6, 0, 50, "top", false, 2);
  assert(square.radius == 0 && square.strips[0].height == 8 && square.strips[2].width == 8);
  Config config;
  BarConfig bar;
  bar.screenWrap = true;
  BarMonitorOverride monitor;
  monitor.match = "DP-1";
  monitor.screenWrap = false;
  monitor.wrapWidth = 9;
  monitor.wrapRadius = 24;
  bar.monitorOverrides = {monitor};
  config.bars = {bar};
  for (bool perMonitor : {false, true}) {
    const auto entries = settings::buildSettingsRegistry(config, &bar, perMonitor ? &monitor : nullptr);
    for (auto key : {"screen_wrap", "wrap_width", "wrap_radius"}) {
      const std::vector<std::string> path = perMonitor
          ? std::vector<std::string>{"bar", "default", "monitor", "DP-1", key}
          : std::vector<std::string>{"bar", "default", key};
      const auto entry = std::ranges::find(entries, path, &settings::SettingEntry::path);
      assert(entry != entries.end());
      assert(entry->group == "effects" && !entry->advanced);
      if (std::string_view(key) == "screen_wrap")
        assert(std::get<settings::ToggleSetting>(entry->control).checked == !perMonitor);
    }
  }
  for (auto edge : {"top", "bottom", "left", "right"}) {
    for (bool full : {false, true}) {
      for (auto size : {std::pair{3440, 1440}, std::pair{1440, 2560}, std::pair{853, 533}}) {
        const auto g = screen_wrap::geometry(size.first, size.second, 6, 20, 50, edge, full);
        int area = 0;
        for (std::size_t i = 0; i < g.strips.size(); ++i) {
          const auto& a = g.strips[i];
          assert(a.x >= 0 && a.y >= 0 && a.width > 0 && a.height > 0);
          assert(a.x + a.width <= g.width && a.y + a.height <= g.height);
          area += a.width * a.height;
          for (std::size_t j = i + 1; j < g.strips.size(); ++j) {
            const auto& b = g.strips[j];
            assert(a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y);
          }
        }
        assert(area < g.width * g.height / 3);
        const std::string_view position = edge;
        assert(g.top == (full && position == "top" ? 50 : 6));
        assert(g.bottom == (full && position == "bottom" ? 50 : 6));
        assert(g.left == (full && position == "left" ? 50 : 6));
        assert(g.right == (full && position == "right" ? 50 : 6));
      }
    }
  }
}
