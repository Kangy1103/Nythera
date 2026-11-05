#pragma once

#include "core/timer_manager.h"
#include "ui/signal.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

// Shared across output surfaces and config reloads. These are session activities, not a wake-up service.
class IslandActivities {
public:
  Signal<> changed;
  std::function<void(bool, bool)> alert;
  void* timerTarget = nullptr;
  void* alarmTarget = nullptr;
  std::int64_t timerEnd = 0, alarmAt = 0;
  int timerDuration = 0;
  bool timerFinished = false, alarmRinging = false;
  unsigned timerRevision = 0, alarmRevision = 0;

  static std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }
  static bool validTime(const std::string& time) {
    return time.size() == 5
        && time[2] == ':'
        && time[0] >= '0'
        && time[0] <= '2'
        && time[1] >= '0'
        && time[1] <= '9'
        && time[3] >= '0'
        && time[3] <= '5'
        && time[4] >= '0'
        && time[4] <= '9'
        && std::stoi(time.substr(0, 2)) < 24;
  }
  void startTimer(int seconds, void* output, std::int64_t at = now()) {
    timerTarget = output;
    timerDuration = seconds;
    timerEnd = at + seconds;
    timerFinished = false;
    ++timerRevision;
    watch();
    changed.emit();
  }
  void cancelTimer() {
    timerEnd = 0;
    timerFinished = false;
    ++timerRevision;
    changed.emit();
  }
  bool armAlarm(const std::string& time, void* output) {
    if (!validTime(time))
      return false;
    auto stamp = static_cast<std::time_t>(now());
    auto date = *std::localtime(&stamp);
    date.tm_hour = std::stoi(time.substr(0, 2));
    date.tm_min = std::stoi(time.substr(3, 2));
    date.tm_sec = 0;
    date.tm_isdst = -1;
    auto scheduled = std::mktime(&date);
    if (scheduled <= stamp) {
      ++date.tm_mday;
      date.tm_isdst = -1;
      scheduled = std::mktime(&date);
    }
    alarmTarget = output;
    alarmAt = scheduled;
    alarmRinging = false;
    ++alarmRevision;
    watch();
    changed.emit();
    return true;
  }
  void dismissAlarm() {
    alarmAt = 0;
    alarmRinging = false;
    ++alarmRevision;
    changed.emit();
  }
  void snooze(int seconds) {
    alarmAt = now() + seconds;
    alarmRinging = false;
    ++alarmRevision;
    watch();
    changed.emit();
  }
  void tick(std::int64_t at = now()) {
    bool timerFired = false, alarmFired = false;
    if (timerEnd && at >= timerEnd) {
      timerFired = true;
      timerEnd = 0;
      timerFinished = true;
      ++timerRevision;
    }
    if (alarmAt && at >= alarmAt) {
      alarmFired = true;
      alarmAt = 0;
      alarmRinging = true;
      ++alarmRevision;
    }
    if ((timerFired || alarmFired) && alert)
      alert(timerFired, alarmFired);
    changed.emit();
    if (!timerEnd && !alarmAt)
      m_tick.stop();
  }
  std::string countdown(std::int64_t at = now()) const {
    auto left = std::max<std::int64_t>(0, timerEnd - at);
    std::ostringstream out;
    out << left / 60 << ':' << std::setfill('0') << std::setw(2) << left % 60;
    return out.str();
  }

private:
  void watch() {
    if (!m_tick.active())
      m_tick.startRepeating(std::chrono::seconds(1), [this] { tick(); });
  }
  Timer m_tick;
};
