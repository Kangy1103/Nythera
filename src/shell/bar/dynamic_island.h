#pragma once

#include "config/config_types.h"
#include "core/timer_manager.h"
#include "notification/notification_manager.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "shell/bar/bar_services.h"
#include "shell/bar/dynamic_island_state.h"
#include "shell/osd/osd_overlay.h"
#include "shell/panel/attached_panel_context.h"
#include "ui/signal.h"

#include <memory>
#include <unordered_set>

class Box;
class Label;
class Glyph;
class Image;
class Button;
class ProgressBar;
class InputArea;
class ScreenWrap;
class ScreenFrameNode;
class Renderer;
struct PointerEvent;

// Compact event presentation. Rich interaction belongs to the registered native panel.
class DynamicIsland {
public:
  DynamicIsland(
      const BarServices&, const BarConfig&, LayerSurface&, AnimationManager&, ScreenWrap*, wl_output*, int, int
  );
  ~DynamicIsland();
  void prepare(Renderer&, bool update);
  bool onPointerEvent(const PointerEvent&);
  bool feedback(const OsdContent&);
  bool notification(const Notification&, NotificationEvent);
  void syncActivities();
  bool activityCommand(const std::string&, const std::string&);
  void toggleExpanded();
  void setVisible(bool);
  void setNativePanelGeometry(std::optional<AttachedPanelGeometry>);
  Node* root() const { return m_root.get(); }
  static LayerSurfaceConfig surfaceConfig(const BarConfig&, int, int);
  static std::string_view nativeToolContext(IslandEvent);

private:
  void build();
  void resolve();
  void layout(Renderer&);
  void geometry();
  void syncClockTimer();
  void changeContent(IslandEvent);
  void openNativeTool();
  void watchNativeLeave();
  void dismissEvent();
  float compactExtent(IslandEvent) const;

  BarServices m_services;
  BarConfig m_config;
  LayerSurface& m_surface;
  AnimationManager& m_animations;
  ScreenWrap* m_wrap;
  wl_output* m_output;
  int m_outputWidth, m_outputHeight;
  bool m_vertical, m_reverse;
  float m_originX = 0, m_originY = 0;
  float m_width = 0, m_height = 0, m_targetWidth = 0, m_targetHeight = 0;
  bool m_initialized = false, m_visible = true, m_hovered = false, m_pinned = false, m_hoverBlocked = false;
  bool m_nativeOwned = false;
  float m_nativeProgress = 0, m_nativeExtent = 0;
  IslandEvent m_event = IslandEvent::Resting, m_displayEvent = IslandEvent::Resting;
  AnimationManager::Id m_resizeAnimation = 0;
  DynamicIslandState m_state;
  std::array<OsdContent, static_cast<unsigned>(IslandEvent::Count)> m_feedback;
  std::array<Notification, 2> m_notices{};
  unsigned m_timerRevision = 0, m_alarmRevision = 0;
  Signal<>::ScopedConnection m_activityConnection, m_paletteConnection;
  Timer m_expiryTimer, m_hoverTimer, m_clockTimer;
  std::unique_ptr<Node> m_root;
  InputDispatcher m_dispatcher;
  InputArea* m_body = nullptr;
  Node* m_content = nullptr;
  Node* m_frameClip = nullptr;
  ScreenFrameNode* m_frame = nullptr;
  Box* m_background = nullptr;
  Glyph* m_glyph = nullptr;
  Label* m_title = nullptr;
  Image* m_art = nullptr;
  ProgressBar* m_progress = nullptr;
  std::array<Button*, 3> m_actions{};
  std::string m_artPath;
  std::unordered_set<std::string> m_pendingArt;
  std::shared_ptr<void> m_alive = std::make_shared<int>(0);
};
