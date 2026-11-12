#pragma once

#include "capture/toplevel_thumbnail_capture.h"
#include "compositors/window_inspector.h"
#include "shell/panel/panel.h"
#include "wayland/wayland_toplevels.h"

#include <optional>
#include <vector>

class CompositorPlatform;
class Flex;
class Image;
class Label;
class Button;
class Select;
class ScrollView;

class WindowInspectorPanel final : public Panel {
public:
  explicit WindowInspectorPanel(CompositorPlatform& platform);
  void create() override;
  void onOpen(std::string_view context) override;
  void onClose() override;
  [[nodiscard]] float preferredWidth() const override { return scaled(880.0F); }
  [[nodiscard]] float preferredHeight() const override { return scaled(680.0F); }
  [[nodiscard]] bool fitsContentHeight() const noexcept override { return true; }
  [[nodiscard]] float measureContentHeight(Renderer& renderer, float contentWidth) override;
  [[nodiscard]] bool dismissTransientUi() override;
  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override { return PanelPlacement::Attached; }

private:
  void doLayout(Renderer& renderer, float width, float height) override;
  void doUpdate(Renderer& renderer) override;
  void onPanelCardOpacityChanged(float opacity) override;
  [[nodiscard]] std::optional<ToplevelInfo> resolveTarget() const;
  void refreshSnapshot(bool oneShot = false);
  void prepareContentLayout(Renderer& renderer, float width);
  void act(bool minimize, bool close);
  void runAction(InspectorWindowAction action);
  void toggleLive();
  void syncWorkspaceChoices(const std::optional<InspectorWindowDetails>& details);

  CompositorPlatform& m_platform;
  ToplevelThumbnailCapture m_capture;
  Timer m_refreshTimer;
  std::optional<ToplevelInfo> m_target;
  std::optional<ScreencopyImage> m_pendingImage;
  bool m_open = false;
  bool m_gone = false;
  bool m_imageDirty = false;
  bool m_hasPreview = false;
  int m_previewSourceWidth = 0;
  int m_previewSourceHeight = 0;
  bool m_live = true;
  bool m_killArmed = false;
  std::string m_actionMessage;
  std::vector<InspectorWorkspace> m_workspaces;
  std::string m_destinationId;
  Flex* m_layout = nullptr;
  Flex* m_header = nullptr;
  ScrollView* m_viewport = nullptr;
  Flex* m_content = nullptr;
  Flex* m_main = nullptr;
  Flex* m_previewColumn = nullptr;
  Flex* m_previewStage = nullptr;
  Flex* m_footer = nullptr;
  Flex* m_primaryActions = nullptr;
  Flex* m_stateActions = nullptr;
  Flex* m_console = nullptr;
  Flex* m_workspaceRow = nullptr;
  Label* m_application = nullptr;
  Label* m_workspaceValue = nullptr;
  Label* m_outputValue = nullptr;
  Label* m_sizeValue = nullptr;
  std::vector<Label*> m_identityKeys;
  Button* m_snapshotRefresh = nullptr;
  std::string m_applicationId;
  std::string m_applicationName;
  std::uint64_t m_desktopEntriesVersion = 0;
  Flex* m_killConfirmation = nullptr;
  Select* m_workspaceSelect = nullptr;
  Label* m_actionStatus = nullptr;
  Button* m_float = nullptr;
  Button* m_pin = nullptr;
  Button* m_fullscreen = nullptr;
  Button* m_maximize = nullptr;
  Button* m_move = nullptr;
  Button* m_kill = nullptr;
  Button* m_confirmKill = nullptr;
  Image* m_preview = nullptr;
  Label* m_title = nullptr;
  Label* m_details = nullptr;
  Label* m_previewStatus = nullptr;
  Button* m_focus = nullptr;
  Button* m_minimize = nullptr;
  Button* m_close = nullptr;
  Button* m_refresh = nullptr;
  std::vector<Button*> m_buttons;
};
