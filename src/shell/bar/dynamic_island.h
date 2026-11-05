#pragma once

#include "config/config_types.h"
#include "core/timer_manager.h"
#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "shell/bar/bar_services.h"
#include "shell/bar/dynamic_island_state.h"
#include "shell/osd/osd_overlay.h"
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

// One independent presentation, not a widget appended to the normal bar lanes.
class DynamicIsland {
public:
  DynamicIsland(
      const BarServices& services, const BarConfig& config, LayerSurface& surface, AnimationManager& animations,
      ScreenWrap* wrap, wl_output* output, int outputWidth, int outputHeight
  );
  ~DynamicIsland();
  void prepare(Renderer& renderer, bool update);
  bool onPointerEvent(const PointerEvent& event);
  bool feedback(const OsdContent& content);
  void toggleExpanded();
  void setVisible(bool visible);
  Node* root() const { return m_root.get(); }
  static LayerSurfaceConfig surfaceConfig(const BarConfig& config, int outputWidth, int outputHeight);

private:
  void build();
  void resolve();
  void layout(Renderer& renderer);
  void geometry();
  void syncClockTimer();
  void changeContent(IslandEvent kind, bool expanded);
  void setLevel(float fraction);
  void updateProgress();

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
  float m_fromWidth = 0, m_fromHeight = 0;
  bool m_initialized = false, m_visible = true, m_hovered = false, m_pinned = false, m_hoverBlocked = false;
  bool m_expanded = false, m_displayExpanded = false;
  IslandEvent m_event = IslandEvent::Resting, m_displayEvent = IslandEvent::Resting;
  AnimationManager::Id m_resizeAnimation = 0;
  DynamicIslandState m_state;
  std::array<OsdContent, 4> m_feedback;
  Timer m_expiryTimer, m_hoverTimer, m_clockTimer, m_progressTimer;
  Signal<>::ScopedConnection m_paletteConnection;
  std::unique_ptr<Node> m_root;
  InputDispatcher m_dispatcher;
  InputArea* m_body = nullptr;
  Node* m_content = nullptr;
  Node* m_frameClip = nullptr;
  ScreenFrameNode* m_frame = nullptr;
  Box* m_background = nullptr;
  Glyph* m_glyph = nullptr;
  Label *m_title = nullptr, *m_detail = nullptr;
  Image* m_art = nullptr;
  Button *m_previous = nullptr, *m_play = nullptr, *m_next = nullptr, *m_close = nullptr;
  InputArea* m_levelArea = nullptr;
  ProgressBar* m_progress = nullptr;
  std::string m_artPath;
  std::unordered_set<std::string> m_pendingArt;
  std::shared_ptr<void> m_alive = std::make_shared<int>(0);
};
