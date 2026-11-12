#include "shell/window_inspector/window_inspector_target.h"
#include "test_check.h"

int main() {
  ToplevelInfo target{.title = "Shared title", .appId = "app", .identifier = "window-1", .exactIdentity = true};
  ToplevelInfo other = target;
  other.identifier = "window-2";
  TEST_CHECK(!window_inspector::sameTarget(target, other));
  other = target;
  other.title = "A changed title";
  TEST_CHECK(window_inspector::sameTarget(target, other));
  other.exactIdentity = false;
  TEST_CHECK(!window_inspector::sameTarget(target, other));
  target.identifier.clear();
  TEST_CHECK(!window_inspector::sameTarget(target, target));

  int first = 0;
  int second = 0;
  target = ToplevelInfo{.handle = reinterpret_cast<zwlr_foreign_toplevel_handle_v1*>(&first)};
  other = target;
  other.title = "New title";
  TEST_CHECK(window_inspector::sameTarget(target, other));
  other.handle = reinterpret_cast<zwlr_foreign_toplevel_handle_v1*>(&second);
  TEST_CHECK(!window_inspector::sameTarget(target, other));
  target.handle = nullptr;
  other.handle = nullptr;
  TEST_CHECK(!window_inspector::sameTarget(target, other));
}
