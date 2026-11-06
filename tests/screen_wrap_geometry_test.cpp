#include "render/core/screen_frame_geometry.h"
#include "shell/bar/dynamic_island_style.h"
#include "shell/bar/screen_wrap_geometry.h"
#include "shell/bar/wrap_layout.h"
#include "shell/settings/settings_registry.h"

#include <cassert>
#include <string_view>
#include <utility>

int main() {
  const std::array<ScreenFrameSpan, 3> spans{{{40, 200}, {450, 550}, {800, 960}}};
  const auto joined = joinedScreenFrameSpans(spans, 1000, 80, 50);
  assert(joined[0].begin == -50 && joined[2].end == 1050 && joined[1] == spans[1]);
  assert(joinedScreenFrameSpans(spans, 1000, 0, 50) == spans);
  assert(joinedScreenFrameSpans(spans, 1000, 39, 50) == spans);
  auto far = spans;
  far[0].begin = 81;
  far[2].end = 919;
  assert(joinedScreenFrameSpans(far, 1000, 80, 50) == far);
  auto empty = spans;
  empty[0] = empty[2] = {};
  assert(joinedScreenFrameSpans(empty, 1000, 500, 50) == empty);
  ScreenFrameContour contour;
  contour.width = 1000;
  contour.height = 600;
  contour.islands = spans;
  assert(screenFrameDistance(contour, 20, 35) > 0);  // detached outer gap
  assert(screenFrameDistance(contour, 35, 8) < 0);   // curved shoulder fills the old sharp junction
  assert(screenFrameDistance(contour, 300, 20) > 0); // gaps between islands remain open
  assert(screenFrameDistance(contour, 100, 49) < 0);
  assert(screenFrameDistance(contour, 200, 50) > 0); // rounded lower corner
  contour.islands = joined;
  assert(screenFrameDistance(contour, 20, 35) < 0); // outer island reaches the frame
  for (int edge = 0; edge < 4; ++edge) {
    auto rotated = contour;
    rotated.edge = edge;
    const bool vertical = edge == 1 || edge == 3;
    rotated.width = vertical ? 600 : 1000;
    rotated.height = vertical ? 1000 : 600;
    for (const auto [u, v] : {std::pair{20.0F, 35.0F}, std::pair{201.0F, 12.0F}, std::pair{500.0F, 49.0F}}) {
      const float x = edge == 1 ? 600 - v : vertical ? v : u;
      const float y = edge == 2 ? 600 - v : vertical ? u : v;
      assert(std::abs(screenFrameDistance(rotated, x, y) - screenFrameDistance(contour, u, v)) < 0.001F);
    }
  }
  // The shared join is inside the fill, never on an outline or rounded cutout,
  // on all four edges. Closing restores the compact rounded contour.
  assert(dynamic_island_style::joinExtension(0, 16, 2) == 0);
  for (int edge = 0; edge < 4; ++edge) {
    ScreenFrameContour neck;
    neck.width = neck.height = 600;
    neck.edge = edge;
    neck.islands[1] = {100, 500};
    neck.depth = 50 + dynamic_island_style::joinExtension(0.5F, 16, 2);
    for (float u : {103.0F, 300.0F, 497.0F}) {
      const float x = edge == 1 ? 550 : edge == 3 ? 50 : u;
      const float y = edge == 2 ? 550 : edge == 0 ? 50 : u;
      assert(screenFrameDistance(neck, x, y) < -2.0F);
    }
  }
  const auto topStroke = dynamic_island_style::outlineExtension("top", 2);
  const auto bottomStroke = dynamic_island_style::outlineExtension("bottom", 2);
  const auto leftStroke = dynamic_island_style::outlineExtension("left", 2);
  const auto rightStroke = dynamic_island_style::outlineExtension("right", 2);
  assert(topStroke.top > 2 && topStroke.left == 0 && topStroke.right == 0 && topStroke.bottom == 0);
  assert(bottomStroke.bottom > 2 && bottomStroke.top == 0);
  assert(leftStroke.left > 2 && leftStroke.right == 0);
  assert(rightStroke.right > 2 && rightStroke.left == 0);
  assert(dynamic_island_style::outlineExtension("top", 0).top == 0);
  assert(dynamic_island_style::panelRadii("top", 16) == Radii(0, 0, 16, 16));
  assert(dynamic_island_style::panelRadii("bottom", 16) == Radii(16, 16, 0, 0));
  assert(dynamic_island_style::panelRadii("left", 16) == Radii(0, 16, 16, 0));
  assert(dynamic_island_style::panelRadii("right", 16) == Radii(16, 0, 0, 16));
  {
    BarConfig top, left;
    top.name = "top";
    top.screenWrap = true;
    top.surfaceMode = "full";
    top.position = "top";
    top.thickness = 50;
    left = top;
    left.name = "left";
    left.position = "left";
    left.thickness = 40;
    std::vector<BarConfig> bars{top, left};
    const auto t = screen_wrap::tracks(bars, 0), l = screen_wrap::tracks(bars, 1);
    assert(t.offset == 0 && l.offset == 0 && t.endInset >= 40 && l.endInset >= 50);
    bars.push_back(left);
    assert(screen_wrap::tracks(bars, 2).offset == 40);
    std::array<int, 3> requests{50, 40, 80};
    bars[0].wrapClearance = 12;
    bars[1].wrapClearance = 8;
    const auto zones = screen_wrap::reservations(bars, requests);
    assert(zones[0] == 62 && zones[3] == 92 && zones[1] == 18 && zones[2] == 18);
    auto contour = ScreenFrameContour{};
    contour.width = 1000;
    contour.height = 600;
    ScreenFrameBand a;
    a.name = "top";
    a.edge = 0;
    a.depth = 50;
    a.radius = 12;
    a.join = 24;
    a.spans[0] = {-50, 1050};
    auto b = a;
    b.name = "left";
    b.edge = 3;
    b.depth = 40;
    b.spans[0] = {-40, 640};
    contour.bands = {a, b};
    assert(screenFrameDistance(contour, 20, 20) < -2);
    assert(screenFrameDistance(contour, 40, 50) < 0);
    assert(screenFrameDistance(contour, 90, 90) > 0);
    auto mixed = bars;
    mixed[2].wrapWidth = 20;
    const auto mixedZones = screen_wrap::reservations(mixed, std::array<int, 3>{50, 40, 0});
    assert(mixedZones[1] == 32 && mixedZones[2] == 32); // shared rail includes a non-reserving bar's request
    auto expanded = contour;
    expanded.bands[0].depth = 300; // transient native panel cannot change work-area requests
    assert(screenFrameDistance(expanded, 500, 200) < 0);
    assert(screen_wrap::reservations(bars, requests) == zones);
    auto limited = screen_wrap::boundedReservations({500, 500, 500, 500}, 853, 533);
    assert(limited[0] + limited[2] == 532 && limited[1] + limited[3] == 852);
    assert((screen_wrap::reservations(bars, std::array<int, 3>{}) == std::array<int, 4>{}));
  }
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
    for (auto key : {"screen_wrap", "wrap_width", "wrap_clearance", "wrap_radius", "wrap_join_distance"}) {
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
  // Visibility follows live base/monitor choices, not the config used to build the registry.
  // Hidden values remain stored and reappear when their feature is enabled again.
  {
    const auto entries = settings::buildSettingsRegistry(config, &bar, nullptr);
    const auto visible = [&](std::string key, bool perMonitor = false) {
      const auto path = perMonitor
          ? std::vector<std::string>{"bar", "default", "monitor", "DP-1", key}
          : std::vector<std::string>{"bar", "default", key};
      const auto entry = std::ranges::find(entries, path, &settings::SettingEntry::path);
      assert(entry != entries.end());
      return !entry->visibleWhen || entry->visibleWhen(config);
    };
    auto& b = config.bars[0];
    b.surfaceMode = "full";
    assert(visible("surface_mode") && visible("screen_wrap"));
    assert(!visible("dynamic_media") && visible("start") && visible("dynamic_center"));
    assert(visible("wrap_width") && !visible("wrap_width", true));
    assert(!visible("wrap_join_distance") && !visible("margin_edge") && !visible("shadow"));
    b.surfaceMode = "islands";
    assert(visible("wrap_join_distance") && visible("start"));
    b.surfaceMode = "dynamic";
    assert(visible("dynamic_media") && visible("dynamic_clock_format"));
    assert(!visible("wrap_join_distance"));
    assert(!visible("start") && !visible("capsule") && !visible("dynamic_center"));
    assert(visible("font_family") && visible("font_weight") && visible("font_family", true));
    assert(visible("dynamic_privacy") && visible("dynamic_devices") && visible("dynamic_activity_indicators"));
    assert(visible("dynamic_visualizer"));
    b.dynamicMedia = false;
    assert(!visible("dynamic_visualizer"));
    b.dynamicMedia = true;
    assert(!visible("auto_hide") && !visible("padding") && !visible("panel_overlap"));
    b.dynamicTimers = false;
    b.dynamicAlarms = false;
    assert(!visible("dynamic_timer_seconds") && !visible("dynamic_alarm_time"));
    assert(!visible("dynamic_snooze_seconds"));
    b.dynamicTimers = true;
    b.dynamicAlarms = true;
    assert(visible("dynamic_timer_seconds") && visible("dynamic_alarm_time"));
    assert(visible("dynamic_snooze_seconds"));
    b.reserveSpace = false;
    assert(!visible("wrap_clearance"));
    b.reserveSpace = true;
    assert(visible("wrap_clearance"));
    b.transparencyMode = BarTransparencyMode::Soft;
    assert(!visible("background_opacity"));
    b.transparencyMode = BarTransparencyMode::Custom;
    assert(visible("background_opacity"));
    auto& o = b.monitorOverrides[0];
    o.surfaceMode = "full";
    o.transparencyMode = BarTransparencyMode::Glass;
    assert(!visible("dynamic_media", true) && visible("start", true));
    assert(!visible("background_opacity", true));
    o.surfaceMode.reset();
    o.screenWrap.reset();
    assert(visible("dynamic_media", true) && visible("wrap_width", true));
    o.dynamicTimers = false;
    assert(!visible("dynamic_timer_seconds", true) && visible("dynamic_timer_seconds"));
    b.screenWrap = false;
    assert(!visible("wrap_width") && !visible("wrap_width", true));
    assert(visible("margin_edge"));
    b.surfaceMode = "full";
    assert(visible("start") && visible("shadow") && !visible("dynamic_media"));
    assert(b.dynamicTimers && !o.dynamicTimers.value());
  }
  for (auto edge : {"top", "bottom", "left", "right"}) {
    for (bool full : {false, true}) {
      for (auto size : {std::pair{3440, 1440}, std::pair{1440, 2560}, std::pair{853, 533}}) {
        auto g = screen_wrap::geometry(size.first, size.second, 6, 20, 50, edge, full);
        if (!full)
          screen_wrap::reserveIslandSpace(g, edge, 75);
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
