#pragma once

#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "shell/workspace_map/workspace_map_panel.h"
#include "wayland/toplevel_surface.h"

class CompositorPlatform;
class ConfigService;
class RenderContext;
class Box;
struct PointerEvent;
struct KeyboardEvent;

// Native window host for the same schematic component used by the workspace panel.
class WorkspaceMapWindow {
public:
  WorkspaceMapWindow(CompositorPlatform& platform, ConfigService& config, RenderContext& renderContext);
  ~WorkspaceMapWindow();
  void open(const std::string& outputName);
  void close();
  void refresh();
  bool onPointerEvent(const PointerEvent& event);
  bool onKeyboardEvent(const KeyboardEvent& event);

private:
  void destroy();
  void prepareFrame(bool update, bool layout);
  CompositorPlatform& m_platform;
  ConfigService& m_config;
  RenderContext& m_renderContext;
  std::unique_ptr<ToplevelSurface> m_surface;
  AnimationManager m_animations;
  InputDispatcher m_input;
  std::unique_ptr<Node> m_scene;
  std::unique_ptr<WorkspaceMapPanel> m_map;
  Box* m_background = nullptr;
  std::uint64_t m_generation = 0;
  std::string m_outputName;
};
