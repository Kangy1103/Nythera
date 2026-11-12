#include "compositors/umbriel/umbriel_runtime.h"
#include "compositors/umbriel/umbriel_window_inspector.h"
#include "compositors/umbriel/umbriel_workspace_backend.h"
#include "test_check.h"

using namespace compositors::umbriel;

int main() {
  const nlohmann::json entry = {
      {"id", "window-1"},
      {"app_id", "app"},
      {"workspace", "DP-1:1"},
      {"output", "DP-1"},
      {"pid", 42},
      {"x", -100},
      {"y", 20},
      {"w", 800},
      {"h", 600},
      {"active", true},
      {"floating", false},
      {"pinned", false},
      {"fullscreen", false},
      {"maximized", false},
      {"xwayland", false},
      {"minimized", false},
      {"minimizable", true}
  };
  const auto details = parseInspectorDetails(entry);
  TEST_CHECK(details.pid == 42 && details.x == -100 && details.height == 600);
  TEST_CHECK(details.floating == false && !details.tabbed.has_value());
  auto malformed = entry;
  malformed["floating"] = "false";
  malformed["pid"] = -1;
  malformed["w"] = 18446744073709551615ULL;
  malformed["h"] = 1.5;
  const auto unknown = parseInspectorDetails(malformed);
  TEST_CHECK(!unknown.floating && !unknown.pid && !unknown.width && !unknown.height);
  std::vector<InspectorWorkspace> workspaces = {
      {.id = "DP-1:1", .name = "1", .output = "DP-1", .layout = "scrolling", .index = 1, .named = false},
      {.id = "DP-2:8", .name = "2", .output = "DP-2", .layout = "stacking", .index = 3, .named = true},
  };
  TEST_CHECK(inspectorCapabilities(details, workspaces).floating);
  auto stacking = details;
  stacking.workspaceId = "DP-2:8";
  TEST_CHECK(!inspectorCapabilities(stacking, workspaces).floating);
  for (const auto action :
       {InspectorWindowAction::Pinned, InspectorWindowAction::Fullscreen, InspectorWindowAction::Maximized,
        InspectorWindowAction::MoveToWorkspace}) {
    TEST_CHECK(!inspectorActionFields("window-1", details, workspaces, action, "DP-2:8", "window-2"));
    TEST_CHECK(!inspectorActionFields("window-1", details, workspaces, action, "DP-2:8", ""));
    TEST_CHECK(inspectorActionFields("window-1", details, workspaces, action, "DP-2:8", "window-1"));
  }
  const auto floating =
      inspectorActionFields("window-1", details, workspaces, InspectorWindowAction::Floating, "", "other");
  TEST_CHECK(floating && (*floating)["window"] == "window-1");
  const auto numbered = inspectorActionFields(
      "window-1", details, workspaces, InspectorWindowAction::MoveToWorkspace, "DP-1:1", "window-1"
  );
  TEST_CHECK(numbered && (*numbered)["workspace"].is_number_integer() && (*numbered)["workspace"] == 1);
  const auto named = inspectorActionFields(
      "window-1", details, workspaces, InspectorWindowAction::MoveToWorkspace, "DP-2:8", "window-1"
  );
  TEST_CHECK(
      named && (*named)["workspace"].is_string() && (*named)["workspace"] == "2" && (*named)["output"] == "DP-2"
  );
  TEST_CHECK(!inspectorActionFields(
      "window-1", details, workspaces, InspectorWindowAction::MoveToWorkspace, "vanished", "window-1"
  ));
  auto noPid = details;
  noPid.pid.reset();
  TEST_CHECK(!inspectorActionFields("window-1", noPid, workspaces, InspectorWindowAction::Kill, "", ""));
  noPid.xwayland = true;
  const auto kill = inspectorActionFields("window-1", noPid, workspaces, InspectorWindowAction::Kill, "", "");
  TEST_CHECK(kill && (*kill)["window"] == "window-1");

  UmbrielRuntime runtime;
  UmbrielWorkspaceBackend backend(runtime);
  backend.handleEvent("windows", nlohmann::json::array({entry}));
  TEST_CHECK(backend.inspectorWindowDetails("window-1") == details);
  TEST_CHECK(!backend.inspectorWindowDetails("window-2"));
  backend.handleEvent(
      "workspaces",
      nlohmann::json::array(
          {{{"id", "DP-1:1"},
            {"name", "1"},
            {"output", "DP-1"},
            {"layout", "scrolling"},
            {"index", 1},
            {"named", false}}}
      )
  );
  TEST_CHECK(backend.inspectorWorkspaces().size() == 1);
  backend.handleEvent("windows", nlohmann::json::object());
  TEST_CHECK(backend.inspectorWindowDetails("window-1") == InspectorWindowDetails{});
  backend.handleStreamReset();
  TEST_CHECK(!backend.inspectorWindowDetails("window-1"));
  TEST_CHECK(backend.inspectorWorkspaces().empty());
}
