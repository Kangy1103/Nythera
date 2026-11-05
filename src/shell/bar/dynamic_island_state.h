#pragma once

#include <array>
#include <cstdint>

enum class IslandEvent : std::uint8_t { Resting, Media, Volume, Brightness };

// Separate event arbitration from rendering. Equal-priority feedback is latest-first;
// lower-priority events can resume if they are still alive. Hover holds the active event.
class DynamicIslandState {
public:
  void push(IslandEvent kind, std::int64_t now, std::int64_t dwell) {
    if (kind == IslandEvent::Resting)
      return;
    m_entries[static_cast<unsigned>(kind)] = {now + dwell, ++m_serial};
  }
  IslandEvent resolve(std::int64_t now, IslandEvent hold = IslandEvent::Resting) const {
    IslandEvent best = IslandEvent::Resting;
    std::uint64_t serial = 0;
    for (unsigned i = 1; i < m_entries.size(); ++i) {
      const auto kind = static_cast<IslandEvent>(i);
      const auto& e = m_entries[i];
      if (!e.serial || (e.expires <= now && kind != hold))
        continue;
      if (priority(kind) > priority(best) || (priority(kind) == priority(best) && e.serial > serial)) {
        best = kind;
        serial = e.serial;
      }
    }
    return best;
  }
  std::int64_t remaining(IslandEvent kind, std::int64_t now) const {
    return m_entries[static_cast<unsigned>(kind)].expires - now;
  }

private:
  struct Entry {
    std::int64_t expires = 0;
    std::uint64_t serial = 0;
  };
  static int priority(IslandEvent kind) {
    return kind == IslandEvent::Resting ? 0 : kind == IslandEvent::Media ? 1 : 2;
  }
  std::array<Entry, 4> m_entries{};
  std::uint64_t m_serial = 0;
};
