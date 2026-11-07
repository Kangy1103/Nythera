#include "render/core/screen_frame_geometry.h"
#include "compositors/umbriel/umbriel_runtime.h"
#include "compositors/umbriel/umbriel_workspace_backend.h"
#include "shell/bar/dynamic_island_style.h"
#include "shell/bar/screen_wrap_geometry.h"
#include "shell/bar/wrap_layout.h"
#include "shell/panel/attached_panel_context.h"
#include "shell/panel/panel_snapshot.h"
#include "shell/settings/settings_registry.h"

#include <cassert>
#include <string_view>
#include <utility>

int main() {
  {
    // Exercise actual IPC snapshot parsing, output filtering and geometry-only
    // change notifications, without connecting to or moving any real windows.
    compositors::umbriel::UmbrielRuntime runtime;
    UmbrielWorkspaceBackend backend(runtime);
    int changes = 0;
    backend.setWindowChangeCallback([&] { ++changes; });
    auto window = nlohmann::json{{"id", "wide"}, {"workspace", "DP-3:1"},
        {"x", 99}, {"w", 500}, {"visible", true}, {"active", false},
        {"minimized", false}, {"minimizable", true}};
    const auto underlap = [&] {
      backend.handleEvent("windows", nlohmann::json::array({window}));
      return backend.hasVisibleLeftUnderlap("DP-3", 140, 1000);
    };
    assert(!underlap()); // exactly half-width retains its backing
    const int before = changes;
    window["w"] = 501;
    assert(underlap() && changes > before); // width-only changes wake the bar
    window["x"] = 100;
    assert(underlap()); // crossed the screen edge, still underneath the bar
    window["x"] = 139;
    assert(underlap()); // keep backing hidden through the last overlapping pixel
    window["x"] = 140;
    assert(!underlap()); // restore only after the complete bar track is clear
    window["x"] = -800;
    window["w"] = 900;
    assert(!underlap()); // only an offscreen remnant beneath the bar
    window["x"] = -759;
    assert(underlap()); // actual content reaches into usable space
    window["visible"] = false;
    assert(!underlap()); // inactive workspace / hidden window
    window["visible"] = true;
    window["minimized"] = true;
    assert(!underlap());
    window["minimized"] = false;
    window["workspace"] = "DP-1:1";
    assert(!underlap()); // independent monitors, regardless of focus
    window["workspace"] = "DP-3:1";
    window["x"] = -1600;
    window["w"] = 700;
    backend.handleEvent("windows", nlohmann::json::array({window}));
    assert(backend.hasVisibleLeftUnderlap("DP-3", -1460, 1000));
    backend.handleStreamReset();
    assert(!backend.hasVisibleLeftUnderlap("DP-3", -1460, 1000));
    backend.setWindowChangeCallback({});
  }
  {
    // Back the full stable track, including empty island gaps. Panel expansion
    // never enters this geometry, and corners belong to exactly one rectangle.
    const auto tracks = screen_wrap::wallpaperBackingRects(1000, 600, {50, 30, 20, 40});
    assert(tracks[0].width == 1000 && tracks[0].height == 50);
    assert(tracks[3].x == 0 && tracks[3].y == 50 && tracks[3].width == 40 && tracks[3].height == 530);
    assert(tracks[1].x == 970 && tracks[1].width == 30);
    assert(tracks[2].y == 580 && tracks[2].height == 20);
    for (int y = 0; y < 600; ++y) {
      for (int x = 0; x < 1000; ++x) {
        int hits = 0;
        for (const auto& r : tracks)
          hits += x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
        assert(hits == static_cast<int>(x < 40 || x >= 970 || y < 50 || y >= 580));
      }
    }
    const auto leftOnly = screen_wrap::wallpaperBackingRects(1000, 600, {0, 0, 0, 40});
    assert(leftOnly[3].width == 40 && leftOnly[3].height == 600);
    assert(leftOnly[0].height == 0 && leftOnly[1].width == 0 && leftOnly[2].height == 0);
    const auto tiny = screen_wrap::wallpaperBackingRects(10, 10, {50, 50, 50, 50});
    for (const auto& r : tiny)
      assert(r.x >= 0 && r.y >= 0 && r.x + r.width <= 10 && r.y + r.height <= 10);
  }
  {
    BarConfig top, left;
    top.position = "top"; top.screenWrap = true; top.thickness = 50;
    left = top; left.position = "left"; left.thickness = 40;
    std::vector<BarConfig> bars{top, left};
    auto mark = screen_wrap::topLeftBrandingRect(bars);
    assert(mark.x == 0 && mark.y == 0 && mark.width == 65 && mark.height == 65);
    bars[0].thickness = 100; bars[1].thickness = 80;
    assert(screen_wrap::topLeftBrandingRect(bars).width == 130);
    bars[0].thickness = 25; bars[1].thickness = 20;
    assert(screen_wrap::topLeftBrandingRect(bars).width == 33);
    bars[0].thickness = 50; bars[1].thickness = 40;
    bars[0].marginEdge = 20; bars[1].marginEdge = 10;
    assert(screen_wrap::topLeftBrandingRect(bars).width == 65);
    bars[1].enabled = false;
    assert(screen_wrap::topLeftBrandingRect(bars).width == 0);
    bars[1].enabled = true; bars[0].screenWrap = false;
    assert(screen_wrap::topLeftBrandingRect(bars).width == 0);
    bars[0].screenWrap = true; bars[0].position = "bottom";
    assert(screen_wrap::topLeftBrandingRect(bars).width == 0);
  }
  for (int edge = 0; edge < 4; ++edge) {
    ScreenFrameContour frame;
    frame.width = frame.height = 1000;
    ScreenFrameBand bar;
    bar.edge = edge; bar.depth = 50; bar.spans[1] = {200, 800};
    appendScreenFrameTool(frame, bar, {450, 550}, 150, 1);
    assert((bar.spans[1] == ScreenFrameSpan{}));
    assert((frame.bands[0].neckSpan == ScreenFrameSpan{200, 800}));
    frame.bands.push_back(bar);
    const auto at = [&](float u, float v) {
      return screenFrameDistance(frame, edge == 1 ? 1000-v : edge == 3 ? v : u,
                                 edge == 2 ? 1000-v : edge == 0 ? v : u);
    };
    assert(at(250, 25) < 0); // label/visualiser keep their original lane backdrop
    assert(at(500, 100) < 0); // small drawer grows below the lane
    assert(at(250, 100) > 0); // do not turn the entire wide lane into a large drawer
    // A drawer aligned to either outer side must not make that side bulge
    // around the old bar depth. Inward step rounding remains inside the bounds.
    for (const auto span : {ScreenFrameSpan{200, 300}, ScreenFrameSpan{700, 800}}) {
      frame.bands.clear();
      bar.spans[1] = {200, 800};
      appendScreenFrameTool(frame, bar, span, 150, 1);
      frame.bands.push_back(bar);
      const float side = span.begin == 200 ? 200.0F : 800.0F;
      for (float v : {35.0F, 40.0F, 45.0F, 50.0F, 55.0F, 70.0F, 110.0F})
        assert(std::abs(at(side, v)) < 0.001F);
    }
  }
  {
    ScreenFrameContour frame;
    frame.rail = 6; frame.radius = 20;
    ScreenFrameBand top, left;
    top.edge = 0; top.depth = 50;
    left.edge = 3; left.depth = 40;
    frame.bands = {top, left};
    const auto crop = panelSnapshotRegion({0, 0, 600, 500}, 1000, 800, frame);
    assert(crop.x == 40 && crop.y == 50 && crop.width == 560 && crop.height == 450);
    const auto offscreen = panelSnapshotRegion({990, 790, 100, 100}, 1000, 800, frame);
    assert(offscreen.width == 0 && offscreen.height == 0);
  }
  // Panel joins are based on the panel's actual edges, regardless of lane.
  assert((joinedPanelFrameSpan({40, 500}, 1000, 80, 50) == ScreenFrameSpan{-50, 500}));
  assert((joinedPanelFrameSpan({500, 960}, 1000, 80, 50) == ScreenFrameSpan{500, 1050}));
  assert((joinedPanelFrameSpan({40, 960}, 1000, 80, 50) == ScreenFrameSpan{-50, 1050}));
  assert((joinedPanelFrameSpan({81, 919}, 1000, 80, 50) == ScreenFrameSpan{81, 919}));
  assert((joinedPanelFrameSpan({40, 960}, 1000, 0, 50) == ScreenFrameSpan{40, 960}));
  // Expanded native tools replace their island, giving straight shared sides
  // below the curved wrap junction, without a second waist at the old bar edge.
  for (int edge = 0; edge < 4; ++edge) for (int lane = 0; lane < 3; ++lane) for (bool wrapped : {false, true}) {
    ScreenFrameContour frame;
    frame.width = frame.height = 1000;
    if (!wrapped) frame.rail = -1;
    ScreenFrameBand bar;
    bar.edge = edge;
    bar.edgeAttached = wrapped;
    bar.depth = 50;
    bar.spans[lane] = {300, 700};
    const int sibling = (lane + 1) % 3;
    bar.spans[sibling] = {40, 80};
    appendScreenFrameTool(frame, bar, {300, 700}, 500, lane);
    assert(bar.spans[lane] == ScreenFrameSpan{});
    assert((bar.spans[sibling] == ScreenFrameSpan{40, 80}));
    frame.bands.push_back(bar);
    const auto at = [&](float u, float v) {
      return screenFrameDistance(frame,
          edge == 1 ? 1000-v : edge == 3 ? v : u,
          edge == 2 ? 1000-v : edge == 0 ? v : u);
    };
    if (wrapped) assert(at(295, 8) < 0); // concave curve still joins the wrap
    for (float v : {35.0F, 45.0F, 50.0F, 60.0F, 100.0F, 450.0F}) {
      assert(std::abs(at(300, v)) < 0.001F);
      assert(std::abs(at(700, v)) < 0.001F);
      assert(at(299, v) > 0 && at(301, v) < 0);
    }
  }
  // The staged native-panel trial keeps the whole surface width clipped in both
  // directions; ordinary island reveals retain their compact-to-panel width change.
  assert(attached_panel::mainAxisRevealClipExtent(760, 120, 700, 0.0F, true) == 760);
  assert(attached_panel::mainAxisRevealClipExtent(760, 120, 700, 0.5F, true) == 760);
  assert(attached_panel::mainAxisRevealClipExtent(760, 120, 700, 1.0F, true) == 760);
  assert(attached_panel::mainAxisRevealClipExtent(760, 120, 700, 0.5F, false) == 410);

  // A full-width native panel slides only into the body region. During island
  // widening (slide=0) no controls can leak into the neck, in any orientation.
  for (auto direction : {AttachedRevealDirection::Down, AttachedRevealDirection::Up,
                         AttachedRevealDirection::Right, AttachedRevealDirection::Left}) {
    const bool side = direction == AttachedRevealDirection::Right || direction == AttachedRevealDirection::Left;
    const bool forward = direction == AttachedRevealDirection::Down || direction == AttachedRevealDirection::Right;
    const float panelW = 600, panelH = 500;
    const float sw = side ? 670 : 660, sh = side ? 560 : 570;
    const float ix = side ? (forward ? 50.0F : 20.0F) : 30.0F;
    const float iy = side ? 30.0F : (forward ? 50.0F : 20.0F);
    const auto clip = attached_panel::stagedPanelBodyClip(direction, sw, sh, ix, iy, panelW, panelH);
    const auto overlap = [](float a, float length, float b, float otherLength) {
      return std::max(0.0F, std::min(a + length, b + otherLength) - std::max(a, b));
    };
    for (float progress : {0.0F, 0.25F, 0.5F, 1.0F}) {
      const float travel = (side ? panelW : panelH) * (1 - progress) * (forward ? -1 : 1);
      const float visibleX = overlap(ix + (side ? travel : 0), panelW, clip.x, clip.width);
      const float visibleY = overlap(iy + (side ? 0 : travel), panelH, clip.y, clip.height);
      assert(std::abs((side ? visibleX : visibleY) - (side ? panelW : panelH) * progress) < 0.01F);
      assert((side ? visibleY : visibleX) == (side ? panelH : panelW));
    }
  }

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
    assert(t.offset == 0 && l.offset == 0 && t.startInset == 52 && l.startInset == 62
           && t.endInset == 18 && l.endInset == 18);
    bars[0].marginEnds = 100; // saved unwrapped margins cannot cap the frame control
    bars[0].wrapJoinedCornerGap = 0;
    bars[0].wrapFreeCornerGap = 0;
    assert(screen_wrap::tracks(bars, 0).startInset == 40);
    assert(screen_wrap::tracks(bars, 0).endInset == 6);
    bars[0].wrapJoinedCornerGap = 12;
    bars[0].wrapFreeCornerGap = 12;
    for (int clearance = 0; clearance <= 7; ++clearance) {
      auto sample = bars;
      for (auto& bar : sample) bar.wrapClearance = clearance;
      const auto reserved = screen_wrap::reservations(sample, std::array<int, 2>{50, 40});
      assert(reserved[0] == 50 + clearance && reserved[3] == 40 + clearance);
      assert(reserved[1] == 6 + clearance && reserved[2] == 6 + clearance);
    }
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
  {
    ScreenFrameContour c;
    c.width = 1000;
    c.height = 600;
    c.rail = 6;
    ScreenFrameBand inner;
    inner.edge = 3;
    inner.outer = 50;
    inner.depth = 100;
    inner.radius = 12;
    inner.join = 24;
    inner.spans[1] = {250, 350};
    c.bands = {inner};
    for (int x = 7; x < 95; ++x)
      assert(screenFrameDistance(c, x, 300) < -2);
    assert(screenFrameDistance(c, 102, 300) > 0);
    for (int edge = 0; edge < 4; ++edge) {
      c.bands[0].edge = edge;
      const float x = edge == 0 || edge == 2 ? 300 : edge == 3 ? 50 : 950;
      const float y = edge == 1 || edge == 3 ? 300 : edge == 0 ? 50 : 550;
      assert(screenFrameDistance(c, x, y) < -2);
    }
  }
  const auto square = screen_wrap::geometry(3440, 1440, 6, 0, 50, "top", false, 2);
  assert(square.radius == 0 && square.strips[0].height == 8 && square.strips[2].width == 8);
  Config config;
  {
    const auto entries = settings::buildSettingsRegistry(config, nullptr, nullptr);
    const auto it = std::find_if(entries.begin(), entries.end(), [](const auto& entry) {
      return entry.path == std::vector<std::string>{"shell", "panel", "snapshot_blur_strength"};
    });
    assert(it != entries.end());
    assert(it->visibleWhen && it->visibleWhen(config));
    config.shell.panel.snapshotBlur = false;
    assert(!it->visibleWhen(config));
    config.shell.panel.snapshotBlur = true;
  }
  BarConfig bar;
  bar.screenWrap = true;
  BarMonitorOverride monitor;
  monitor.match = "DP-1";
  bar.monitorOverrides = {monitor};
  config.bars = {bar};
  config.shell.screenWrap.enabled = true;

  {
    const auto entries = settings::buildSettingsRegistry(config, &bar, nullptr);
    const auto backing = std::ranges::find(entries,
        std::vector<std::string>{"shell", "screen_wrap", "wallpaper_backing"}, &settings::SettingEntry::path);
    assert(backing != entries.end());
    Config visibility;
    assert(!backing->visibleWhen(visibility));
    visibility.shell.screenWrap.enabled = true;
    assert(backing->visibleWhen(visibility));
    assert(
        std::ranges::find(
            entries, std::vector<std::string>{"shell", "screen_wrap", "enabled"}, &settings::SettingEntry::path
        )
        != entries.end()
    );
    assert(
        std::ranges::find(
            entries, std::vector<std::string>{"bar", "default", "screen_wrap"}, &settings::SettingEntry::path
        )
        == entries.end()
    );
  }
  {
    const auto entries = settings::buildSettingsRegistry(config, &bar, nullptr);
    for (const auto& entry : entries)
      if (entry.path.size() > 2 && entry.path[0] == "shell" && entry.path[1] == "screen_wrap")
        assert(entry.path[2] != "monitor");
    for (int edge = 0; edge < 4; ++edge) {
      ScreenFrameContour detached;
      detached.width = detached.height = 600;
      detached.rail = -1;
      ScreenFrameBand band;
      band.edge = edge; band.edgeAttached = false;
      band.outer = 20; band.depth = 70; band.radius = 12;
      band.spans[1] = {100, 300};
      detached.bands.push_back(band);
      const auto at = [&](float u, float v) {
        return screenFrameDistance(detached,
            edge == 1 ? 600-v : edge == 3 ? v : u,
            edge == 2 ? 600-v : edge == 0 ? v : u);
      };
      assert(at(200, 10) > 0); // no rail or fill in the saved unwrapped edge gap
      assert(at(200, 40) < 0);
      assert(at(400, 40) > 0); // other gaps stay transparent
      detached.bands[0].depth = 250; // native tool grows out of that island
      assert(at(200, 70) < -2); // the former bar edge is inside the union, not an outlined seam
    }
  }
  // Visibility follows live base/monitor choices, not the config used to build the registry.
  // Hidden values remain stored and reappear when their feature is enabled again.
  {
    const auto entries = settings::buildSettingsRegistry(config, &bar, nullptr);
    const auto visible = [&](std::string key, bool perMonitor = false) {
      std::vector<std::string> path;
      if (key == "screen_wrap" || key.starts_with("wrap_")) {
        const std::string field = key == "screen_wrap" ? "enabled" : key.substr(5);
        path = perMonitor ? std::vector<std::string>{"shell", "screen_wrap", "monitor", "DP-1", field}
                          : std::vector<std::string>{"shell", "screen_wrap", field};
      } else
        path = perMonitor ? std::vector<std::string>{"bar", "default", "monitor", "DP-1", key}
                          : std::vector<std::string>{"bar", "default", key};
      const auto entry = std::ranges::find(entries, path, &settings::SettingEntry::path);
      assert(entry != entries.end());
      return !entry->visibleWhen || entry->visibleWhen(config);
    };
    auto& b = config.bars[0];
    b.surfaceMode = "full";
    assert(visible("surface_mode") && visible("screen_wrap"));
    assert(!visible("dynamic_media") && visible("start") && visible("dynamic_center"));
    assert(visible("wrap_width"));
    assert(visible("wrap_joined_corner_gap") && visible("wrap_free_corner_gap"));
    assert(visible("wrap_join_distance") && !visible("margin_edge") && !visible("shadow"));
    b.surfaceMode = "islands";
    assert(visible("wrap_join_distance") && visible("start"));
    b.surfaceMode = "dynamic";
    assert(visible("dynamic_media") && visible("dynamic_clock_format"));
    assert(visible("wrap_join_distance"));
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
    assert(visible("wrap_clearance"));
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

    assert(visible("dynamic_media", true) && visible("wrap_width"));
    o.dynamicTimers = false;
    assert(!visible("dynamic_timer_seconds", true) && visible("dynamic_timer_seconds"));
    config.shell.screenWrap.enabled = false;
    assert(!visible("wrap_width"));
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
