#pragma once

#include "app/main_loop.h"
#include "core/files/directory_scanner.h"
#include "config/config_service.h"
#include "config/config_poll_source.h"
#include "filemanager/gio_poll_source.h"
#include "net/http_client.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/input_dispatcher.h"
#include "theme/theme_service.h"
#include "ui/controls/virtual_grid_view.h"
#include "wayland/toplevel_surface.h"
#include "wayland/wayland_connection.h"

#include <cstdint>
#include <filesystem>
#include <gio/gio.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Button;
class Flex;
class Input;
class Label;

class FileManager {
public:
  explicit FileManager(std::string initialPath);
  ~FileManager();
  int run();

private:
  class Adapter;
  struct PendingMonitorChange {
    GFileMonitorEvent event;
    std::filesystem::path path;
    std::filesystem::path otherPath;
  };
  void navigate(const std::filesystem::path& path, bool recordHistory = true);
  void enumerate(const std::filesystem::path& path);
  void fetchNext(GFileEnumerator* enumerator, std::uint64_t generation);
  void sortEntries();
  void restoreSelection(bool chooseFirst = false);
  void applyPendingMonitorChanges();
  void handleMonitorEvent(GFile* file, GFile* otherFile, GFileMonitorEvent event);
  void queryMonitoredFile(
      const std::filesystem::path& path, const std::filesystem::path& replacedPath = std::filesystem::path{}
  );
  void removeMonitoredFile(const std::filesystem::path& path);
  void activateSelected();
  void activatePointerItem(std::size_t index);
  void setStatus(std::string text);
  void updateNavigationControls();
  void rebuildBreadcrumbs();
  void setLocationEditMode(bool editing);
  void setSortField(FileDialogSortField field);
  void toggleSortOrder();
  void prepareFrame(bool needsUpdate, bool needsLayout);
  void handlePointer(const PointerEvent& event);
  void handleKeyboard(const KeyboardEvent& event);
  void monitorDirectory();
  void closeMonitor();

  ConfigService m_configService;
  HttpClient m_httpClient;
  nythera::theme::ThemeService m_themeService{m_configService, m_httpClient};
  ConfigPollSource m_configPollSource{m_configService};
  WaylandConnection m_wayland{WaylandConnection::Purpose::ApplicationClient};
  GlSharedContext m_sharedContext;
  RenderContext m_renderContext;
  GioPollSource m_gioPollSource;
  std::unique_ptr<ToplevelSurface> m_surface;
  InputDispatcher m_dispatcher;
  std::unique_ptr<Flex> m_root;
  Flex* m_breadcrumbBar = nullptr;
  Flex* m_sidebar = nullptr;
  VirtualGridView* m_grid = nullptr;
  Input* m_locationInput = nullptr;
  Label* m_statusLabel = nullptr;
  Button* m_locationEditButton = nullptr;
  Button* m_backButton = nullptr;
  Button* m_forwardButton = nullptr;
  Button* m_upButton = nullptr;
  Button* m_homeButton = nullptr;
  Button* m_refreshButton = nullptr;
  Button* m_viewButton = nullptr;
  Button* m_hiddenButton = nullptr;
  Button* m_sortNameButton = nullptr;
  Button* m_sortSizeButton = nullptr;
  Button* m_sortModifiedButton = nullptr;
  Button* m_sortOrderButton = nullptr;
  std::unique_ptr<Adapter> m_adapter;
  std::filesystem::path m_path;
  std::vector<std::filesystem::path> m_backHistory;
  std::vector<std::filesystem::path> m_forwardHistory;
  std::vector<FileEntry> m_entries;
  std::vector<FileEntry> m_loadingEntries;
  std::vector<PendingMonitorChange> m_pendingMonitorChanges;
  std::vector<std::pair<Button*, std::filesystem::path>> m_placeButtons;
  std::optional<std::filesystem::path> m_selectedPath;
  GFileMonitor* m_monitor = nullptr;
  FileDialogSortField m_sortField = FileDialogSortField::Name;
  FileDialogSortOrder m_sortOrder = FileDialogSortOrder::Ascending;
  bool m_showHidden = false;
  bool m_gridMode = false;
  bool m_editingLocation = false;
  bool m_loading = false;
  bool m_chooseFirstAfterLoad = true;
  std::optional<std::size_t> m_lastPointerIndex;
  std::uint32_t m_lastPointerClickTime = 0;
  std::uint32_t m_pointerClickTime = 0;
  bool m_running = true;
  std::uint64_t m_enumerationGeneration = 0;
};
