#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>

// Bounded FIFO with per-id replacement. Only time actually displayed is consumed;
// interruptions and native-panel interaction do not eat the next notice's dwell.
template <class Notice> class IslandNoticeQueue {
public:
  struct Entry { Notice notice; std::int64_t remaining; };
  static constexpr std::size_t capacity = 8;
  bool push(const Notice& notice, std::int64_t dwell) {
    auto it = std::ranges::find_if(m_items, [&](const auto& e) { return e.notice.id == notice.id; });
    if (it != m_items.end()) {
      if (it == m_items.begin()) m_ticking = false;
      *it = {notice, dwell};
      return true;
    }
    if (m_items.size() >= capacity) return false;
    m_items.push_back({notice, dwell});
    return true;
  }
  std::optional<Notice> advance(std::int64_t now, bool displayed, bool held) {
    const auto elapsed = m_ticking ? std::max<std::int64_t>(0, now - m_lastTick) : 0;
    m_lastTick = now;
    m_ticking = displayed && !held && !m_items.empty();
    if (!m_items.empty() && displayed && !held) {
      m_items.front().remaining -= elapsed;
      if (m_items.front().remaining <= 0) {
        auto notice = m_items.front().notice;
        m_items.pop_front();
        m_ticking = displayed && !held && !m_items.empty();
        return notice;
      }
    }
    return std::nullopt;
  }
  void erase(std::uint32_t id) {
    if (!m_items.empty() && m_items.front().notice.id == id) m_ticking = false;
    std::erase_if(m_items, [&](const auto& e) { return e.notice.id == id; });
  }
  const Entry* front() const { return m_items.empty() ? nullptr : &m_items.front(); }
  bool contains(std::uint32_t id) const {
    return std::ranges::any_of(m_items, [&](const auto& e) { return e.notice.id == id; });
  }
  std::size_t size() const { return m_items.size(); }
private:
  std::deque<Entry> m_items;
  std::int64_t m_lastTick = 0;
  bool m_ticking = false;
};
