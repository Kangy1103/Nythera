#include "compositors/compositor_platform.h"
#include "config/config_service.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "shell/workspace_map/workspace_map_panel.h"
#include "test_check.h"
#include "ui/controls/box.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
  class LayoutRenderer final : public Renderer {
  public:
    TextMetrics measureText(
        std::string_view text, float size, const TextFont&, float maxWidth, int, TextAlign, TextEllipsize, bool
    ) override {
      const float width = static_cast<float>(text.size()) * size * 0.5F;
      return {.width = maxWidth > 0 ? std::min(width, maxWidth) : width, .bottom = size, .lineCount = 1};
    }
    TextMetrics measureFont(float size, const TextFont&) override { return {.bottom = size}; }
    void measureTextCursorStops(
        std::string_view, float, const std::vector<std::size_t>&, std::vector<float>&, const TextFont&
    ) override {}
    void measureTextCursorStopsWrapped(
        std::string_view, float, const std::vector<std::size_t>&, float, std::vector<TextCursorStop>&, const TextFont&
    ) override {}
    TextMetrics measureGlyph(char32_t, float size) override { return {.width = size, .bottom = size}; }
    TextureManager& textureManager() override { std::abort(); }
    float renderScale() const noexcept override { return 1; }
  };
} // namespace

class WorkspaceMapPanelTestAccess {
public:
  static void checkSceneLayout() {
    i18n::Service::instance().init("en");
    WaylandConnection wayland;
    CompositorPlatform platform(wayland);
    WorkspaceMapPanel panel(platform);
    LayoutRenderer renderer;
    panel.create();
    panel.m_outputBounds = {-1000, -200, 1600, 900};
    panel.ensureWorkspace("DP-3:1", "Main").tiles = {
        {.id = "one",
         .title = "First window",
         .appName = "First App",
         .x = -1000,
         .y = -200,
         .width = 800,
         .height = 600,
         .focused = true},
        {.id = "two",
         .title = "Second window",
         .appName = "Second App",
         .x = -100,
         .y = -200,
         .width = 800,
         .height = 600},
    };
    const auto firstTiles = panel.m_workspaces.front().tiles;
    panel.ensureWorkspace("DP-3:2", "Gaming").tiles = firstTiles;
    // Unlike Main, Gaming has a further column to the right and a taller
    // window. Per-workspace fitting would move and rescale the monitor.
    panel.m_workspaces[1].tiles.push_back(
        {.id = "far", .title = "Off-screen column", .x = 1800, .y = -350, .width = 1000, .height = 1200}
    );
    panel.ensureWorkspace("DP-3:3", "Empty");
    panel.m_workspaces.front().visible = true;
    TEST_CHECK(panel.ensureWorkspace("DP-3:2").name == "Gaming");
    TEST_CHECK(panel.m_workspaces.size() == 3);
    float readableWindowWidth = 0;
    for (const float width : {600.0F, 320.0F}) {
      panel.layout(renderer, width, 260);
      TEST_CHECK(panel.m_header->width() == width);
      TEST_CHECK(panel.m_scroll->scrollable());
      TEST_CHECK(panel.m_horizontal->scrollable());
      const float tileWidth = panel.m_workspaces.front().tiles.front().frame.width;
      if (readableWindowWidth == 0) {
        readableWindowWidth = tileWidth;
        const auto* outline = panel.m_workspaces.front().outputOutline;
        TEST_CHECK(
            std::abs(
                outline->x()
                + outline->width() * 0.5F
                - panel.m_horizontal->scrollOffset()
                - panel.m_horizontal->contentViewportWidth() * 0.5F
            )
            < 0.01F
        );
      } else {
        TEST_CHECK(std::abs(readableWindowWidth - tileWidth) < 0.001F);
      }
      for (const auto& workspace : panel.m_workspaces) {
        TEST_CHECK(workspace.outputOutline != nullptr);
        const auto* monitor = dynamic_cast<RectNode*>(workspace.outputOutline->children().front().get());
        TEST_CHECK(monitor && monitor->width() > 0 && monitor->height() > 0);
        TEST_CHECK(!workspace.outputOutline->hitTestVisible());
        TEST_CHECK(workspace.outputOutline->visible() == workspace.visible);
        TEST_CHECK(workspace.outputFill->visible() == workspace.visible);
        const auto* reference = panel.m_workspaces.front().outputOutline;
        TEST_CHECK(workspace.outputOutline->x() == reference->x());
        TEST_CHECK(workspace.outputOutline->y() == reference->y());
        TEST_CHECK(workspace.outputOutline->width() == reference->width());
        TEST_CHECK(workspace.outputOutline->height() == reference->height());
        TEST_CHECK(
            std::abs(
                workspace.outputOutline->x() + workspace.outputOutline->width() * 0.5F - workspace.scene->width() * 0.5F
            )
            < 0.001F
        );
        for (const auto& tile : workspace.tiles) {
          const auto* rect = dynamic_cast<RectNode*>(tile.box->children().front().get());
          TEST_CHECK(rect != nullptr);
          TEST_CHECK(rect->width() == tile.frame.width && rect->width() > 0);
          TEST_CHECK(rect->height() == tile.frame.height && rect->height() > 0);
          TEST_CHECK(rect->style().borderWidth > 0);
          TEST_CHECK(tile.area->width() == rect->width());
        }
      }
      const auto& first = panel.m_workspaces[0];
      const auto& second = panel.m_workspaces[1];
      TEST_CHECK(second.scene->y() >= first.scene->y() + first.scene->height());
      TEST_CHECK(first.tiles[0].area->parent() != second.tiles[0].area->parent());
      TEST_CHECK(first.tiles[0].frame.x == second.tiles[0].frame.x);
      TEST_CHECK(first.tiles[0].frame.y == second.tiles[0].frame.y);
      TEST_CHECK(first.tiles[0].frame.width == second.tiles[0].frame.width);
      TEST_CHECK(first.tiles[0].frame.height == second.tiles[0].frame.height);
      TEST_CHECK(panel.m_workspaces[2].tiles.empty());
    }
    panel.layout(renderer, 600, 420);
    TEST_CHECK(panel.m_scroll->scrollable());
    TEST_CHECK(panel.m_horizontal->scrollable());
    const float windowWidth = panel.m_workspaces.front().tiles.front().frame.width;
    panel.m_horizontal->setScrollOffset(panel.m_horizontal->maxScrollOffset());
    TEST_CHECK(panel.m_horizontal->scrollOffset() == panel.m_horizontal->maxScrollOffset());
    const auto& far = panel.m_workspaces[1].tiles.back().frame;
    TEST_CHECK(
        far.x + far.width - panel.m_horizontal->scrollOffset() <= panel.m_horizontal->contentViewportWidth() + 0.01F
    );
    panel.layout(renderer, 600, 420);
    TEST_CHECK(panel.m_workspaces.front().tiles.front().frame.width == windowWidth);
    const auto* firstScene = panel.m_workspaces[0].scene;
    const auto* secondScene = panel.m_workspaces[1].scene;
    // Flex snaps child origins to physical pixels; fractional heights may leave a subpixel difference.
    TEST_CHECK(std::abs(secondScene->y() - firstScene->y() - firstScene->height() - Style::spaceXs) <= 1.0F);
  }
  static void checkInactiveWindowInclusion() {
    InspectorWindowDetails details;
    details.workspaceId = "DP-3:2";
    details.output = "DP-3";
    details.visible = false;
    details.x = 0;
    details.y = -200;
    details.width = 800;
    details.height = 600;
    TEST_CHECK(WorkspaceMapPanel::includesWindow(details, "DP-3"));
    TEST_CHECK(!WorkspaceMapPanel::includesWindow(details, "DP-2"));
    details.width = 0;
    TEST_CHECK(!WorkspaceMapPanel::includesWindow(details, "DP-3"));
    details.width = 800;
    details.workspaceId.clear();
    TEST_CHECK(!WorkspaceMapPanel::includesWindow(details, "DP-3"));
  }
  static void checkRememberedSize() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("nythera-map-state-" + std::to_string(unique));
    TEST_CHECK(setenv("NYTHERA_CONFIG_HOME", directory.c_str(), 1) == 0);
    TEST_CHECK(setenv("NYTHERA_STATE_HOME", directory.c_str(), 1) == 0);
    {
      ConfigService config;
      WaylandConnection wayland;
      CompositorPlatform platform(wayland);
      WorkspaceMapPanel panel(platform, &config);
      TEST_CHECK(panel.panelPlacement() == PanelPlacement::Attached);
      panel.m_output = "DP-3";
      panel.setPendingOpenContext("DP-3");
      panel.onResizeFinished(1100, 780);
      panel.m_output = "DP-1";
      panel.setPendingOpenContext("DP-1");
      panel.onResizeFinished(480, 640);
      TEST_CHECK(
          WorkspaceMapPanel::outputStateKey("DP-1", "width") != WorkspaceMapPanel::outputStateKey("DP_1", "width")
      );
      panel.setPendingOpenContext("DP-3");
      TEST_CHECK(panel.preferredWidth() == 1100 && panel.preferredHeight() == 780);
      ConfigService restored;
      WorkspaceMapPanel reopened(platform, &restored);
      reopened.setPendingOpenContext("DP-1");
      TEST_CHECK(reopened.preferredWidth() == 480 && reopened.preferredHeight() == 640);
      // The pending output must win over the previous snapshot's output before
      // PanelManager creates the next surface, not only after onOpen().
      reopened.m_output = "DP-1";
      reopened.setPendingOpenContext("DP-3");
      TEST_CHECK(reopened.preferredWidth() == 1100 && reopened.preferredHeight() == 780);
      TEST_CHECK(
          restored.setStateString("workspace_map", WorkspaceMapPanel::outputStateKey("DP-3", "width"), "invalid")
      );
      TEST_CHECK(restored.setStateString(
          "workspace_map", WorkspaceMapPanel::outputStateKey("DP-3", "height"), "999999999999999999"
      ));
      WorkspaceMapPanel invalid(platform, &restored);
      invalid.setPendingOpenContext("DP-3");
      TEST_CHECK(invalid.preferredWidth() == 960 && invalid.preferredHeight() == 640);
      // Do not apply the old oversized global size to every new monitor.
      TEST_CHECK(restored.setStateString("workspace_map", "width", "3000"));
      TEST_CHECK(restored.setStateString("workspace_map", "height", "2000"));
      invalid.setPendingOpenContext("DP-2");
      TEST_CHECK(invalid.preferredWidth() == 960 && invalid.preferredHeight() == 640);
    }
    std::filesystem::remove_all(directory);
  }
  static void checkPortraitProjection() {
    const LayoutRect portrait{256, -1100, 1440, 2560};
    std::vector<WorkspaceMapPanel::Tile> tiles = {
        {.x = 306, .y = -1050, .width = 1374, .height = 1241},
        {.x = 306, .y = 203, .width = 1374, .height = 1241},
    };
    const auto viewport = WorkspaceMapPanel::normalizeTiles(
        tiles, portrait, 480, WorkspaceMapPanel::monitorMapHeight(portrait, 1), 3, true
    );
    TEST_CHECK(viewport.width >= 239 && viewport.width <= 241);
    TEST_CHECK(tiles[0].frame.width > 220);
    TEST_CHECK(tiles[0].frame.x == tiles[1].frame.x);
    TEST_CHECK(tiles[0].frame.width == tiles[1].frame.width);
    TEST_CHECK(tiles[1].frame.y > tiles[0].frame.y + tiles[0].frame.height);
    TEST_CHECK(std::abs(tiles[0].frame.width / tiles[0].frame.height - 1374.0F / 1241.0F) < 0.001F);
  }
  static void checkPortraitWorkspaceDirection() {
    WaylandConnection wayland;
    CompositorPlatform platform(wayland);
    WorkspaceMapPanel panel(platform);
    LayoutRenderer renderer;
    panel.create();
    panel.m_outputBounds = {296, -1060, 1394, 2514};
    auto& first = panel.ensureWorkspace("DP-1:1", "1");
    first.visible = true;
    first.tiles = {
        {.x = 306, .y = -1050, .width = 1374, .height = 1241}, {.x = 306, .y = 203, .width = 1374, .height = 1241}
    };
    panel.ensureWorkspace("DP-1:2", "2").tiles = {{.x = 306, .y = -424, .width = 1374, .height = 1241}};
    panel.ensureWorkspace("DP-1:3", "3");
    panel.layout(renderer, 400, 640);
    const auto& one = panel.m_workspaces[0];
    const auto& two = panel.m_workspaces[1];
    TEST_CHECK(one.scene->y() == two.scene->y());
    TEST_CHECK(two.scene->x() >= one.scene->x() + one.scene->width());
    TEST_CHECK(panel.m_workspaces[2].scene->x() >= two.scene->x() + two.scene->width());
    TEST_CHECK(one.tiles[1].frame.y > one.tiles[0].frame.y + one.tiles[0].frame.height);
    TEST_CHECK(std::abs(one.tiles[0].frame.width - two.tiles[0].frame.width) < 0.001F);
    TEST_CHECK(panel.m_horizontal->scrollable());
    TEST_CHECK(!panel.m_scroll->scrollable());
    panel.m_horizontal->setScrollOffset(panel.m_horizontal->maxScrollOffset());
  }
  static void checkInitialPanelSizes() {
    const auto landscape = WorkspaceMapPanel::initialPanelSize(3440, 1440, 520);
    const auto portrait = WorkspaceMapPanel::initialPanelSize(1440, 2560, 600);
    TEST_CHECK(landscape.first > landscape.second);
    TEST_CHECK(portrait.first < portrait.second);
    TEST_CHECK(std::abs(landscape.first / landscape.second - 3440.0F / 1440.0F) < 0.001F);
    TEST_CHECK(std::abs(portrait.first / portrait.second - 1440.0F / 2560.0F) < 0.001F);
    const auto crowded = WorkspaceMapPanel::initialPanelSize(853, 533, 4000);
    TEST_CHECK(crowded.first <= 853 * 0.8F + 0.01F);
    TEST_CHECK(crowded.second <= 533 * 0.8F + 0.01F);
  }
  static void checkWorkspaceProjection() {
    // A monitor at a non-zero global origin, a column extending left of it,
    // stacked unequal windows, and a small floating window crossing its edge.
    // Every position, dimension and gap must use the same affine transform.
    std::vector<WorkspaceMapPanel::Tile> tiles = {
        {.x = -800, .y = -390, .width = 900, .height = 780}, {.x = 120, .y = -390, .width = 600, .height = 500},
        {.x = 120, .y = 130, .width = 600, .height = 260},   {.x = 2000, .y = -200, .width = 300, .height = 180},
        {.x = 900, .y = -100, .width = 20, .height = 10},
    };
    const LayoutRect output{100, -400, 2000, 800};
    const auto viewport = WorkspaceMapPanel::normalizeTiles(tiles, output, 640, 260, 10);
    const float scale = viewport.width / output.width;
    const auto near = [](double a, double b) { return std::abs(a - b) < 0.001; };
    TEST_CHECK(near(viewport.height / output.height, scale));
    TEST_CHECK(near(viewport.x + viewport.width * 0.5F, 320));
    TEST_CHECK(near(viewport.y + viewport.height * 0.5F, 130));
    for (const auto& tile : tiles) {
      const auto& frame = tile.frame;
      TEST_CHECK(near(frame.width, tile.width * scale));
      TEST_CHECK(near(frame.height, tile.height * scale));
      TEST_CHECK(near(frame.x - viewport.x, (tile.x - output.x) * scale));
      TEST_CHECK(near(frame.y - viewport.y, (tile.y - output.y) * scale));
      TEST_CHECK(frame.x >= 9.999F && frame.y >= 9.999F);
      TEST_CHECK(frame.x + frame.width <= 630.001F && frame.y + frame.height <= 250.001F);
    }
    TEST_CHECK(tiles[0].frame.x < viewport.x);
    TEST_CHECK(tiles[3].frame.x + tiles[3].frame.width > viewport.x + viewport.width);
    TEST_CHECK(tiles[1].frame.x == tiles[2].frame.x);
    TEST_CHECK(tiles[2].frame.y > tiles[1].frame.y + tiles[1].frame.height);
    TEST_CHECK(tiles.back().frame.width < 32); // No minimum-width inflation.

    // Scrolling keeps a monitor-relative scale even when the popup narrows
    // or another distant column is added. Neither may shrink existing windows.
    (void)WorkspaceMapPanel::normalizeTiles(tiles, output, 640, 166, 3, true);
    const float readableWidth = tiles.front().frame.width;
    const float readableHeight = tiles.front().frame.height;
    tiles.push_back({.x = 8000, .y = 0, .width = 600, .height = 300});
    (void)WorkspaceMapPanel::normalizeTiles(tiles, output, 320, 166, 3, true);
    TEST_CHECK(near(tiles.front().frame.width, readableWidth));
    TEST_CHECK(near(tiles.front().frame.height, readableHeight));
    TEST_CHECK(near(readableWidth / tiles.front().width, 160.0 / output.height));

    // No windows: empty state has no fabricated viewport geometry.
    tiles.clear();
    TEST_CHECK(WorkspaceMapPanel::normalizeTiles(tiles, output, 640, 260, 10).width == 0);
  }
  static void checkGeometry() {
    using Tile = WorkspaceMapPanel::Tile;
    // Negative global coordinates, scrolling columns, and unequal window sizes
    // retain their order and relative dimensions under a common scale.
    std::vector<Tile> tiles = {
        {.x = -1000, .y = -200, .width = 800, .height = 600},
        {.x = -100, .y = 100, .width = 400, .height = 300},
    };
    (void)WorkspaceMapPanel::normalizeTiles(tiles, {}, 650, 400, 0);
    TEST_CHECK(tiles[0].frame.x < tiles[1].frame.x);
    TEST_CHECK(tiles[0].frame.y < tiles[1].frame.y);
    TEST_CHECK(tiles[0].frame.width == 2 * tiles[1].frame.width);
    TEST_CHECK(tiles[0].frame.height == 2 * tiles[1].frame.height);
    TEST_CHECK(tiles[0].frame.x == 0);
    TEST_CHECK(tiles[1].frame.x + tiles[1].frame.width == 650);

    // Extreme int IPC coordinates do not overflow while fitting the union.
    // Tiny rectangles keep their true scale instead of moving neighbouring tiles.
    tiles = {
        {.x = std::numeric_limits<int>::min(), .y = -10, .width = 1, .height = 1},
        {.x = std::numeric_limits<int>::max(), .y = 10, .width = 2000, .height = 1000},
    };
    for (const float extent : {400.0F, 10.0F}) {
      (void)WorkspaceMapPanel::normalizeTiles(tiles, {}, extent, extent, 0);
      for (const auto& tile : tiles) {
        const auto& r = tile.frame;
        TEST_CHECK(std::isfinite(r.x) && std::isfinite(r.y));
        TEST_CHECK(r.width > 0 && r.height > 0);
        TEST_CHECK(r.x >= 0 && r.y >= 0);
        TEST_CHECK(r.x + r.width <= extent && r.y + r.height <= extent);
      }
    }
    tiles.clear();
    (void)WorkspaceMapPanel::normalizeTiles(tiles, {}, 400, 300, 0);
    TEST_CHECK(tiles.empty());
  }
};

int main() {
  WorkspaceMapPanelTestAccess::checkGeometry();
  WorkspaceMapPanelTestAccess::checkInactiveWindowInclusion();
  WorkspaceMapPanelTestAccess::checkWorkspaceProjection();
  WorkspaceMapPanelTestAccess::checkPortraitProjection();
  WorkspaceMapPanelTestAccess::checkPortraitWorkspaceDirection();
  WorkspaceMapPanelTestAccess::checkInitialPanelSizes();
  WorkspaceMapPanelTestAccess::checkSceneLayout();
  WorkspaceMapPanelTestAccess::checkRememberedSize();
}
