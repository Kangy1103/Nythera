#pragma once

#include "capture/toplevel_thumbnail_capture.h"
#include "shell/panel/panel.h"
#include "system/icon_resolver.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class CompositorPlatform;
class ConfigService;
class Button;
class Label;
class Box;
class Flex;
class ScrollView;
class Image;
class Glyph;
class Scrollbar;
struct InspectorWindowDetails;

// A non-live geometry snapshot with exact-window focus and close controls.
class WorkspaceMapPanel final : public Panel {
public:
  explicit WorkspaceMapPanel(CompositorPlatform& platform, ConfigService* config = nullptr, bool expanded = false);
  void create() override;
  void onClose() override;
  void onOpen(std::string_view context) override;
  [[nodiscard]] float preferredWidth() const override;
  [[nodiscard]] float preferredHeight() const override;
  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override;
  void onResizeFinished(float width, float height) override;
  void setPreviewSuspended(bool suspended);
  void setExpandCallback(std::function<void(const std::string&)> callback) { m_expand = std::move(callback); }
  void setWindowCallbacks(
      std::function<void()> close, std::function<void()> refresh, std::function<void(std::uint32_t)> resize
  ) {
    m_closeWindow = std::move(close);
    m_refreshWindow = std::move(refresh);
    m_resizeWindow = std::move(resize);
  }

private:
  friend class WorkspaceMapPanelTestAccess;
  void rebuildMap();
  void captureNextSnapshot();
  void toggleLivePreviews();
  bool storeSnapshot(const std::string& id, ScreencopyImage image);
  [[nodiscard]] static std::chrono::milliseconds previewDelay(std::chrono::milliseconds elapsed, bool initialFill);
  static constexpr std::size_t kSnapshotBudget = 8U * 1024U * 1024U;
  void schedulePreview(std::chrono::milliseconds delay);
  [[nodiscard]] bool previewReady(std::chrono::steady_clock::time_point now) const;
  [[nodiscard]] bool hasVisibleSnapshotGap() const;
  void dismiss();
  void requestRefresh();
  std::function<void(const std::string&)> m_expand;
  std::function<void()> m_closeWindow, m_refreshWindow;
  std::function<void(std::uint32_t)> m_resizeWindow;
  void bindPan(InputArea* area);
  void closeWindow(const std::string& id);
  void doUpdate(Renderer& renderer) override;
  struct PanGesture {
    bool active = false;
    bool moved = false;
    float x = 0, y = 0, horizontal = 0, vertical = 0;
  } m_pan;
  std::vector<std::string> m_pendingCloses;
  bool m_preservePan = false;
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
    InputArea* close = nullptr;
    Glyph* closeGlyph = nullptr;
    Box* box = nullptr;
    Label* label = nullptr;
    Image* preview = nullptr;
    bool previewUploadAttempted = false;
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
    Box* outputFill = nullptr;
    Box* outputOutline = nullptr;
  };
  [[nodiscard]] bool snapshotVisible(const Tile& tile) const;
  [[nodiscard]] static bool includesWindow(const InspectorWindowDetails& details, std::string_view output);
  WorkspaceMap& ensureWorkspace(std::string_view id, std::string_view name = {});
  [[nodiscard]] static LayoutRect normalizeTiles(
      std::vector<Tile>& tiles, const LayoutRect& outputBounds, float width, float height, float padding,
      bool scrollCanvas = false
  );
  CompositorPlatform& m_platform;
  ConfigService* m_config = nullptr;
  bool m_expanded = false;
  ToplevelThumbnailCapture m_capture;
  struct Snapshot {
    std::optional<ScreencopyImage> image;
    std::chrono::steady_clock::time_point lastCaptured{}, retryAt{};
  };
  std::unordered_map<std::string, Snapshot> m_snapshots;
  std::size_t m_snapshotBytes = 0;
  std::uint64_t m_snapshotGeneration = 0;
  bool m_snapshotOpen = false;
  bool m_snapshotLayout = false;
  bool m_livePreviews = true;
  bool m_previewSuspended = false;
  Timer m_previewTimer;
  std::chrono::steady_clock::time_point m_nextPreviewCapture{};
  Button* m_previewToggle = nullptr;

  std::unordered_map<std::string, std::pair<float, float>> m_outputSizes;
  InputArea* m_resizeGrip = nullptr;
  std::string m_output;
  std::vector<WorkspaceMap> m_workspaces;
  Label* m_heading = nullptr;
  Flex* m_header = nullptr;
  ScrollView* m_scroll = nullptr;
  ScrollView* m_horizontal = nullptr;
  Scrollbar* m_horizontalBar = nullptr;
  float m_canvasWidth = 0;
  float m_verticalPanPadding = 0;
  bool m_centerPending = true;
  LayoutRect m_outputBounds;
  IconResolver m_iconResolver;
  bool m_rebuildPending = true;
};
