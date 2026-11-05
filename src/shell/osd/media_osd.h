#pragma once

#include "core/timer_manager.h"
#include "shell/osd/osd_overlay.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

class MprisService;

// Seed discovery silently; announce actual transitions into Playing once metadata is ready.
struct MediaPlaybackStartState {
  std::string previous;
  bool pending = false;

  bool observe(std::string_view status, bool titled, bool active) {
    if (!previous.empty() && previous != "Playing" && status == "Playing")
      pending = true;
    previous = status;
    if (status != "Playing" || !active)
      pending = false;
    if (!pending || !titled)
      return false;
    pending = false;
    return true;
  }
};

class MediaOsd {
public:
  void bindOverlay(OsdOverlay& overlay);
  void onMprisChanged(const MprisService& service);
  // Island-only feedback: preserve the normal track OSD's existing behaviour.
  void setPlaybackStartedCallback(std::function<void(const OsdContent&)> callback) {
    m_playbackStarted = std::move(callback);
  }

private:
  struct PlayerState {
    // Track signature this player has already been accounted for. Empty means no titled snapshot
    // has been seen yet; a real signature is never empty.
    std::string trackSignature;
    double volume = 1.0;
    MediaPlaybackStartState playback;
  };

  struct PendingTrack {
    std::string busName;
    std::string signature;
    OsdContent content;
  };

  void scheduleTrackOsd(PendingTrack pending);
  void cancelTrackOsd(const std::string& busName);
  void showPendingTrackOsd();

  OsdOverlay* m_overlay = nullptr;
  std::function<void(const OsdContent&)> m_playbackStarted;
  std::unordered_map<std::string, PlayerState> m_players;
  std::optional<PendingTrack> m_pendingTrack;
  Timer m_trackSettleTimer;
};
