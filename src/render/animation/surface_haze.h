#pragma once

#include "core/timer_manager.h"
#include "render/core/surface_haze.h"

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct HazeSurfaceDescriptor {
  std::string_view id;
  std::string_view label;
};
inline constexpr HazeSurfaceDescriptor kHazeSurfaces[] = {
    {"workspace-map-window", "settings.haze.workspace-map-window"},
    {"workspace-map", "settings.haze.workspace-map"},
    {"control-center", "settings.haze.control-center"},
    {"window-inspector", "settings.haze.window-inspector"},
    {"launcher", "settings.haze.launcher"},
    {"settings", "settings.haze.settings"},
    {"clipboard", "settings.haze.clipboard"},
    {"polkit", "settings.haze.polkit"},
    {"session", "settings.haze.session"},
    {"wallpaper", "settings.haze.wallpaper"},
    {"setup-wizard", "settings.haze.setup-wizard"},
    {"notifications", "settings.haze.notifications"},
    {"osd", "settings.haze.osd"},
    {"window-switcher", "settings.haze.window-switcher"},
    {"dock", "settings.haze.dock"},
    {"tooltips", "settings.haze.tooltips"},
    {"dock-context-menu", "settings.haze.dock-context-menu"},
    {"context-menu", "settings.haze.context-menu"},
    {"select-menu", "settings.haze.select-menu"},
    {"tray-menu", "settings.haze.tray-menu"},
    {"tray-drawer", "settings.haze.tray-drawer"},
    {"file-dialog", "settings.haze.file-dialog"},
    {"color-picker", "settings.haze.color-picker"},
    {"glyph-picker", "settings.haze.glyph-picker"},
    {"search-picker", "settings.haze.search-picker"},
    {"desktop-widget-editor", "settings.haze.desktop-widget-editor"},
    {"lock-screen", "settings.haze.lock-screen"},
    {"annotation", "settings.haze.annotation"},
    {"screenshot", "settings.haze.screenshot"},
    {"windows", "settings.haze.windows"},
    {"popups", "settings.haze.popups"},
};

// Uses the existing TimerManager. One timer is armed only while a participant is
// ready to present: waiting on an occluded Wayland frame callback costs no ticks.
class SurfaceHaze {
public:
  static SurfaceHaze& instance();
  void configure(bool enabled, bool animated, bool transparent, std::map<std::string, bool> surfaces);
  void registerProfile(std::string_view id);
  void attach(const void* owner, std::string id, std::function<void(float)> tick, std::function<void()> changed);
  void detach(const void* owner);
  void setReady(const void* owner, bool ready);
  [[nodiscard]] SurfaceHazeStyle style(std::string_view id, float time, float seed) const;
  [[nodiscard]] bool animated(std::string_view id) const;
  [[nodiscard]] std::vector<std::string> profiles() const;
  [[nodiscard]] bool timerActive() const { return m_timer.active(); }

private:
  struct Participant {
    std::string id;
    std::function<void(float)> tick;
    std::function<void()> changed;
    bool ready = false;
  };
  void schedule();
  void tick();
  std::map<const void*, Participant> m_participants;
  std::map<std::string, bool> m_surfaces;
  std::map<std::string, bool> m_profiles;
  std::vector<const void*> m_tickOwners;
  Timer m_timer;
  std::chrono::steady_clock::time_point m_lastTick{};
  bool m_enabled = false, m_animated = true, m_transparent = false;
};
