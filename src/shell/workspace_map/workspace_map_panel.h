#pragma once

#include "shell/panel/panel.h"
#include "system/icon_resolver.h"

#include <string>
#include <unordered_map>
#include <vector>

class CompositorPlatform;
class ConfigService;
class Label;
class Box;
class Flex;
class ScrollView;
class Image;
class Glyph;
class Scrollbar;
struct InspectorWindowDetails;

// Stage 1: a geometry snapshot, with no capture or window-management controls.
class WorkspaceMapPanel final : public Panel {
public:
  explicit WorkspaceMapPanel(CompositorPlatform& platform, ConfigService* config = nullptr);
  void create() override;
  void onOpen(std::string_view context) override;
  [[nodiscard]] float preferredWidth() const override;
  [[nodiscard]] float preferredHeight() const override;
  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override;
  void onResizeFinished(float width, float height) override;

private:
  friend class WorkspaceMapPanelTestAccess;
  void rebuildMap();
  void syncHorizontalScroll(float offset);
  [[nodiscard]] std::pair<float, float> preferredOutputSize() const;
  [[nodiscard]] static std::string outputStateKey(std::string_view output, std::string_view dimension);
  [[nodiscard]] static float monitorMapHeight(const LayoutRect& output, float scale);
  [[nodiscard]] static std::pair<float, float>
  initialPanelSize(float outputWidth, float outputHeight, float contentHeight);
  void doLayout(Renderer& renderer, float width, float height) override;
  struct Tile {
    std::string id;
    std::string title;
    std::string appName;
    std::string iconPath;
    double x, y, width, height;
    bool focused;
    LayoutRect frame;
    InputArea* area = nullptr;
    Box* box = nullptr;
    Box* titleSurface = nullptr;
    Label* label = nullptr;
    Image* icon = nullptr;
    Glyph* fallbackIcon = nullptr;
    bool iconAttempted = false;
  };
  struct WorkspaceMap {
    std::string id;
    std::string name;
    bool visible = false;
    std::vector<Tile> tiles;
    Node* scene = nullptr;
    Label* heading = nullptr;
    Label* empty = nullptr;
    Box* outputFill = nullptr;
    Box* outputOutline = nullptr;
  };
  [[nodiscard]] static bool includesWindow(const InspectorWindowDetails& details, std::string_view output);
  WorkspaceMap& ensureWorkspace(std::string_view id, std::string_view name = {});
  [[nodiscard]] static LayoutRect normalizeTiles(
      std::vector<Tile>& tiles, const LayoutRect& outputBounds, float width, float height, float padding,
      bool scrollCanvas = false
  );
  CompositorPlatform& m_platform;
  ConfigService* m_config = nullptr;
  std::unordered_map<std::string, std::pair<float, float>> m_outputSizes;
  InputArea* m_resizeGrip = nullptr;
  std::string m_output;
  std::vector<WorkspaceMap> m_workspaces;
  Label* m_heading = nullptr;
  Flex* m_header = nullptr;
  Label* m_legend = nullptr;
  ScrollView* m_scroll = nullptr;
  ScrollView* m_horizontal = nullptr;
  Scrollbar* m_horizontalBar = nullptr;
  float m_canvasWidth = 0;
  bool m_centerPending = true;
  LayoutRect m_outputBounds;
  IconResolver m_iconResolver;
  bool m_rebuildPending = true;
};
