#include "shell/bar/widgets/workspaces_widget.h"
#include "tests/test_check.h"

#include <string>
#include <utility>

class WorkspacesWidgetTestAccess {
public:
  static std::string tooltip(std::string name, const std::string& label, bool showLabel) {
    return WorkspacesWidget::workspaceTooltipText(Workspace{.name = std::move(name)}, label, showLabel);
  }
  static bool iconMatches(wl_output* focused, wl_output* workspace) {
    return WorkspacesWidget::activeWindowMatchesOutput(focused, workspace);
  }
};

int main() {
  int first = 0, second = 0;
  auto* firstOutput = reinterpret_cast<wl_output*>(&first);
  auto* secondOutput = reinterpret_cast<wl_output*>(&second);
  TEST_CHECK(WorkspacesWidgetTestAccess::iconMatches(firstOutput, firstOutput));
  TEST_CHECK(!WorkspacesWidgetTestAccess::iconMatches(firstOutput, secondOutput));
  TEST_CHECK(WorkspacesWidgetTestAccess::iconMatches(nullptr, firstOutput));
  // A name cut short by max_label_chars is recoverable on hover.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("client portal", "cli", true) == "client portal");
  // A pill that already spells out the whole name has nothing to add.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("web", "web", true).empty());
  // label_source = "id" labels a named workspace by number, so the name is only reachable on hover.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("client portal", "3", true) == "client portal");
  // A hidden label shows nothing, even when its text would have matched the name.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("web", "web", false) == "web");
  // Unnamed workspaces have no tooltip, never an empty one.
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("", "3", true).empty());
  TEST_CHECK(WorkspacesWidgetTestAccess::tooltip("", "", false).empty());

  return 0;
}
