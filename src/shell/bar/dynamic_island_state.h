#pragma once

#include <array>
#include <cstdint>

enum class IslandEvent : std::uint8_t {
  Resting,
  Media,
  Volume,
  Brightness,
  Privacy,
  Device,
  Notification,
  Timer,
  TimerFinished,
  Alarm,
  Call,
  Count
};

// Separate event arbitration from rendering. Equal-priority feedback is latest-first;
// lower-priority events can resume if they are still alive. Hover holds the active event.
class DynamicIslandState {
public:
  void push(IslandEvent kind, std::int64_t now, std::int64_t dwell) {
    if (kind == IslandEvent::Resting)
      return;
    m_entries[static_cast<unsigned>(kind)] = {now + dwell, ++m_serial};
  }
  void hold(IslandEvent kind) { m_entries[static_cast<unsigned>(kind)] = {0, ++m_serial, true}; }
  IslandEvent resolve(std::int64_t now, IslandEvent hold = IslandEvent::Resting) const {
    IslandEvent best = IslandEvent::Resting;
    std::uint64_t serial = 0;
    for (unsigned i = 1; i < m_entries.size(); ++i) {
      const auto kind = static_cast<IslandEvent>(i);
      const auto& e = m_entries[i];
      if (!e.serial || (e.expires <= now && !e.persistent && kind != hold))
        continue;
      if (priority(kind) > priority(best) || (priority(kind) == priority(best) && e.serial > serial)) {
        best = kind;
        serial = e.serial;
      }
    }
    return best;
  }
  void clear(IslandEvent kind) { m_entries[static_cast<unsigned>(kind)] = {}; }
  std::int64_t remaining(IslandEvent kind, std::int64_t now) const {
    const auto& entry = m_entries[static_cast<unsigned>(kind)];
    return entry.persistent ? -1 : entry.expires - now;
  }

private:
  struct Entry {
    std::int64_t expires = 0;
    std::uint64_t serial = 0;
    bool persistent = false;
  };
  static int priority(IslandEvent kind) {
    switch (kind) {
    case IslandEvent::Resting:
      return 0;
    case IslandEvent::Timer:
      return 1;
    case IslandEvent::Media:
      return 2;
    case IslandEvent::Volume:
    case IslandEvent::Brightness:
    case IslandEvent::Privacy:
    case IslandEvent::Device:
      return 3;
    case IslandEvent::Notification:
      return 4;
    case IslandEvent::Call:
      return 5;
    case IslandEvent::TimerFinished:
    case IslandEvent::Alarm:
      return 6;
    case IslandEvent::Count:
      return 0;
    }
    return 0;
  }
  std::array<Entry, static_cast<unsigned>(IslandEvent::Count)> m_entries{};
  std::uint64_t m_serial = 0;
};
