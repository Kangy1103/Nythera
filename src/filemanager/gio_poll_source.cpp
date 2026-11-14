#include "filemanager/gio_poll_source.h"

#include <algorithm>

GioPollSource::GioPollSource(GMainContext* context)
    : m_context(context != nullptr ? g_main_context_ref(context) : g_main_context_ref(g_main_context_default())) {}

GioPollSource::~GioPollSource() { g_main_context_unref(m_context); }

void GioPollSource::doAddPollFds(std::vector<pollfd>& fds) {
  m_fds.clear();
  m_priority = 0;
  const gboolean ready = g_main_context_prepare(m_context, &m_priority);
  m_preparedReady = ready != FALSE;
  const gint count = g_main_context_query(m_context, m_priority, &m_timeoutMs, nullptr, 0);
  if (count > 0) {
    m_fds.resize(static_cast<std::size_t>(count));
    g_main_context_query(m_context, m_priority, &m_timeoutMs, m_fds.data(), count);
  }
  if (ready) {
    m_timeoutMs = 0;
  }
  for (const GPollFD& fd : m_fds) {
    fds.push_back({.fd = fd.fd, .events = static_cast<short>(fd.events), .revents = 0});
  }
}

void GioPollSource::dispatch(const std::vector<pollfd>& fds, std::size_t startIdx) {
  const std::size_t available = std::min(m_fds.size(), fds.size() > startIdx ? fds.size() - startIdx : 0);
  for (std::size_t i = 0; i < available; ++i) {
    m_fds[i].revents = static_cast<gushort>(fds[startIdx + i].revents);
  }
  const gboolean checked = g_main_context_check(m_context, m_priority, m_fds.data(), static_cast<gint>(m_fds.size()));
  if (m_preparedReady || checked) {
    g_main_context_dispatch(m_context);
  } else {
    g_main_context_release(m_context);
  }
  m_preparedReady = false;
}
