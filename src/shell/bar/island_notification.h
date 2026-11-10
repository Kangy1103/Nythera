#pragma once
#include "notification/notification.h"

#include <algorithm>
#include <cctype>

enum class IslandNotificationTransition { None, ToCall, ToNotice };

inline IslandNotificationTransition islandNotificationTransition(bool call, bool queued, bool storedCall) {
  if (call && queued) return IslandNotificationTransition::ToCall;
  if (!call && storedCall) return IslandNotificationTransition::ToNotice;
  return IslandNotificationTransition::None;
}

inline bool isIslandCall(const Notification& n) {
  const auto category = n.category.value_or("");
  if (category == "call.incoming" || category == "call.ongoing")
    return true;
  auto app = n.appName + " " + n.desktopEntry.value_or("");
  auto text = n.summary + " " + n.body;
  auto lower = [](std::string& s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
  };
  lower(app);
  lower(text);
  const bool discord = app.find("discord") != std::string::npos || app.find("vesktop") != std::string::npos;
  const bool kde = app.find("kdeconnect") != std::string::npos || app.find("kde connect") != std::string::npos;
  if (kde && category == "callReceived")
    return true;
  return (discord || kde)
      && (text.find("incoming call") != std::string::npos
          || text.find("incoming voice call") != std::string::npos
          || text.find("is calling you") != std::string::npos);
}
