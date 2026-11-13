#include "render/animation/surface_haze.h"

#include <algorithm>
#include <utility>

SurfaceHaze& SurfaceHaze::instance() {
  static SurfaceHaze service;
  return service;
}
void SurfaceHaze::registerProfile(std::string_view id) {
  if (!id.empty())
    m_profiles.try_emplace(std::string(id), true);
}
void SurfaceHaze::configure(bool enabled, bool animated, bool transparent, std::map<std::string, bool> surfaces) {
  if (m_enabled == enabled && m_animated == animated && m_transparent == transparent && m_surfaces == surfaces)
    return;
  m_enabled = enabled;
  m_animated = animated;
  m_transparent = transparent;
  m_surfaces = std::move(surfaces);
  // Work on owner keys: callbacks may attach or detach other native surfaces.
  std::vector<const void*> owners;
  for (const auto& [owner, participant] : m_participants) {
    (void)participant;
    owners.push_back(owner);
  }
  for (const auto* owner : owners) {
    if (const auto it = m_participants.find(owner); it != m_participants.end()) {
      const auto changed = it->second.changed;
      changed();
    }
  }
  schedule();
}
SurfaceHazeStyle SurfaceHaze::style(std::string_view id, float time, float seed) const {
  const auto it = m_surfaces.find(std::string(id));
  return {
      .enabled = m_enabled && !id.empty() && (it == m_surfaces.end() || it->second),
      .transparent = m_transparent,
      .time = time,
      .seed = seed
  };
}
bool SurfaceHaze::animated(std::string_view id) const { return m_animated && style(id, 0, 0).enabled; }
void SurfaceHaze::attach(
    const void* owner, std::string id, std::function<void(float)> tickCallback, std::function<void()> changed
) {
  registerProfile(id);
  m_participants.insert_or_assign(
      owner, Participant{std::move(id), std::move(tickCallback), std::move(changed), false}
  );
  schedule();
}
void SurfaceHaze::detach(const void* owner) {
  m_participants.erase(owner);
  schedule();
}
void SurfaceHaze::setReady(const void* owner, bool ready) {
  if (const auto it = m_participants.find(owner); it != m_participants.end())
    it->second.ready = ready;
  schedule();
}
void SurfaceHaze::schedule() {
  const bool ready = std::ranges::any_of(m_participants, [this](const auto& entry) {
    return entry.second.ready && animated(entry.second.id);
  });
  if (!ready) {
    m_timer.stop();
    return;
  }
  if (m_timer.active())
    return;
  const auto elapsed = std::chrono::steady_clock::now() - m_lastTick;
  const auto delay = std::max(
      std::chrono::milliseconds(1),
      std::chrono::milliseconds(33) - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
  );
  m_timer.start(delay, [this] { tick(); });
}
void SurfaceHaze::tick() {
  m_timer.stop();
  const auto now = std::chrono::steady_clock::now();
  const float seconds = std::clamp(std::chrono::duration<float>(now - m_lastTick).count(), 0.0F, 0.05F);
  m_lastTick = now;
  m_tickOwners.clear();
  for (const auto& [owner, participant] : m_participants)
    if (participant.ready && animated(participant.id))
      m_tickOwners.push_back(owner);
  for (const auto* owner : m_tickOwners) {
    const auto it = m_participants.find(owner);
    if (it == m_participants.end() || !it->second.ready)
      continue;
    it->second.ready = false;
    const auto callback = it->second.tick;
    callback(seconds);
  }
  schedule();
}
std::vector<std::string> SurfaceHaze::profiles() const {
  std::vector<std::string> result;
  for (const auto& [id, enabled] : m_profiles) {
    (void)enabled;
    result.push_back(id);
  }
  return result;
}
