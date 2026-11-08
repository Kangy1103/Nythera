#include "app/single_instance_lock.h"

#include "core/log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  constexpr Logger kLog("instance");
} // namespace

SingleInstanceLock::~SingleInstanceLock() { release(); }

bool SingleInstanceLock::tryAcquire() {
  m_path = resolveLockPath();

  const int fd = ::open(m_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) {
    // The lock file is unusable (e.g. read-only runtime dir). Degrade to running
    // unguarded rather than refusing to start — losing the single-instance
    // guarantee is better than a shell that won't launch.
    kLog.warn("could not open lock file {}: {}; running without single-instance guard", m_path, std::strerror(errno));
    return true;
  }

  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    if (errno == EWOULDBLOCK) {
      // Another live instance holds the lock.
      ::close(fd);
      return false;
    }
    // Unexpected flock failure — degrade to unguarded, same rationale as above.
    kLog.warn("flock failed on {}: {}; running without single-instance guard", m_path, std::strerror(errno));
    ::close(fd);
    return true;
  }

  m_fd = fd;
  return true;
}

bool SingleInstanceLock::adoptInherited(int fd) {
  if (fd < 0 || m_fd >= 0) {
    return false;
  }

  m_path = resolveLockPath();
  struct stat inheritedStat{};
  struct stat pathStat{};
  if (::fstat(fd, &inheritedStat) != 0) {
    return false;
  }
  const int pathFd = ::open(m_path.c_str(), O_RDWR | O_CLOEXEC);
  if (pathFd < 0) {
    return false;
  }
  const bool sameLock = ::fstat(pathFd, &pathStat) == 0
      && inheritedStat.st_dev == pathStat.st_dev
      && inheritedStat.st_ino == pathStat.st_ino;
  ::close(pathFd);
  if (!sameLock || ::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    return false;
  }

  m_fd = fd;
  return true;
}

bool SingleInstanceLock::prepareForExecRestart() const noexcept {
  if (m_fd < 0) {
    return false;
  }
  const int flags = ::fcntl(m_fd, F_GETFD);
  return flags >= 0 && ::fcntl(m_fd, F_SETFD, flags & ~FD_CLOEXEC) == 0;
}

void SingleInstanceLock::release() noexcept {
  if (m_fd >= 0) {
    // Closing the fd releases the flock. The lock file itself is intentionally
    // left in place: unlinking it would let a racing instance flock a different
    // inode than a third instance later creates, defeating the lock.
    ::close(m_fd);
    m_fd = -1;
  }
}

std::string SingleInstanceLock::resolveLockPath() {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  if (runtime == nullptr || runtime[0] == '\0') {
    runtime = "/tmp";
  }
  const char* display = std::getenv("WAYLAND_DISPLAY");
  if (display == nullptr || display[0] == '\0') {
    display = "wayland-0";
  }
  return std::string(runtime) + "/nythera-" + display + ".lock";
}
