#include "filemanager/file_manager.h"

#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/ui_phase.h"
#include "i18n/i18n_service.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"
#include "ui/dialogs/file_entry_row.h"
#include "ui/dialogs/file_entry_tile.h"
#include "ui/style.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <chrono>
#include <clocale>
#include <linux/input-event-codes.h>
#include <tuple>
#include <utility>
#include <xkbcommon/xkbcommon-keysyms.h>

class FileManager::Adapter final : public VirtualGridAdapter {
public:
  explicit Adapter(FileManager& owner) : m_owner(owner) {}
  std::size_t itemCount() const override { return m_owner.m_entries.size(); }
  std::unique_ptr<Node> createTile() override {
    if (m_owner.m_gridMode) {
      auto tile = std::make_unique<FileEntryTile>(1.0F, nullptr);
      tile->setSelectionRoles(ColorRole::PrimaryContainer, ColorRole::OnPrimaryContainer);
      return tile;
    }
    auto row = std::make_unique<FileEntryRow>(1.0F);
    row->setSelectionRoles(ColorRole::PrimaryContainer, ColorRole::OnPrimaryContainer);
    return row;
  }
  void bindTile(Node& tile, std::size_t index, bool selected, bool hovered) override {
    if (index >= m_owner.m_entries.size() || m_renderer == nullptr)
      return;
    if (m_owner.m_gridMode) {
      static_cast<FileEntryTile&>(tile).bind(
          *m_renderer, m_owner.m_entries[index], index, tile.width(), tile.height(), selected, hovered, false
      );
    } else {
      static_cast<FileEntryRow&>(tile).bind(
          *m_renderer, m_owner.m_entries[index], index, tile.width(), selected, hovered, false
      );
    }
  }
  void onActivate(std::size_t index) override {
    if (index < m_owner.m_entries.size())
      m_owner.activatePointerItem(index);
  }
  void setRenderer(Renderer* renderer) { m_renderer = renderer; }

private:
  FileManager& m_owner;
  Renderer* m_renderer = nullptr;
};

namespace {
  struct MonitoredFileQuery {
    FileManager* manager;
    std::uint64_t generation;
    std::filesystem::path path;
    std::filesystem::path replacedPath;
  };

  constexpr float kToolbarHeight = 32.0F;
  constexpr float kListRowHeight = 38.0F;
  constexpr float kGridCellWidth = 132.0F;
  constexpr float kGridCellHeight = 118.0F;
  constexpr float kSidebarWidth = 188.0F;

  void applyNytheraBevel(Flex& panel, float radius) {
    panel.setRadius(radius);
    panel.setCornerShapes(CornerShapes{
        .tl = CornerShape::Chamfered,
        .tr = CornerShape::Chamfered,
        .br = CornerShape::Chamfered,
        .bl = CornerShape::Chamfered,
    });
  }

  void useSubtleSelection(Button& button) {
    auto palette = Button::defaultPalette(button.variant());
    palette.selected = Button::ButtonStateColors{
        .bg = colorSpecFromRole(ColorRole::PrimaryContainer, 0.42F),
        .border = clearColorSpec(),
        .label = colorSpecFromRole(ColorRole::OnPrimaryContainer),
    };
    button.setCustomPalette(std::move(palette));
  }

  FileEntry makeFileEntry(GFileInfo* info, const std::filesystem::path& path) {
    FileEntry entry;
    entry.name = g_file_info_get_name(info);
    entry.absPath = path;
    entry.isDir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    entry.size = g_file_info_get_size(info);
    entry.mtime = std::filesystem::file_time_type::clock::from_sys(
        std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED))
        )
    );
    return entry;
  }

  bool sameEntry(const FileEntry& lhs, const FileEntry& rhs) {
    return lhs.name == rhs.name
        && lhs.absPath == rhs.absPath
        && lhs.isDir == rhs.isDir
        && lhs.size == rhs.size
        && lhs.mtime == rhs.mtime;
  }

  std::unique_ptr<Button> makeButton(std::string_view text, std::function<void()> action, Button** out = nullptr) {
    auto button = std::make_unique<Button>();
    button->setText(text);
    button->setVariant(ButtonVariant::Ghost);
    button->setControlHeight(kToolbarHeight);
    button->setPadding(5.0F);
    button->setOnClick(std::move(action));
    if (out != nullptr)
      *out = button.get();
    return button;
  }
} // namespace

FileManager::FileManager(std::string initialPath)
    : m_adapter(std::make_unique<Adapter>(*this)),
      m_path(initialPath.empty() ? std::filesystem::current_path() : std::filesystem::path(initialPath)) {}

FileManager::~FileManager() {
  closeMonitor();
  m_dispatcher.setSceneRoot(nullptr);
  if (m_surface)
    m_surface->setSceneRoot(nullptr);
  m_renderContext.cleanup();
  m_sharedContext.cleanup();
}

int FileManager::run() {
  std::setlocale(LC_ALL, "");
  std::setlocale(LC_NUMERIC, "C");
  i18n::Service::instance().init();
  Style::setRtl(i18n::Service::instance().rtl());
  m_configService.addReloadCallback([this] { m_themeService.apply(); }, "file-manager-theme");
  m_themeService.apply();
  m_wayland.connect();
  if (!m_wayland.hasXdgShell() || m_wayland.compositor() == nullptr || m_wayland.shm() == nullptr)
    throw std::runtime_error("Wayland compositor, shm, and xdg-shell are required");
  m_sharedContext.initialize(m_wayland.display());
  m_renderContext.initialize(m_sharedContext);
  m_renderContext.setTextBaseDirection(i18n::Service::instance().rtl());
  m_surface = std::make_unique<ToplevelSurface>(m_wayland);
  m_surface->setRenderContext(&m_renderContext);
  m_surface->setClosedCallback([this] { m_running = false; });
  m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface->requestLayout(); });
  m_surface->setPrepareFrameCallback([this](bool update, bool layout) { prepareFrame(update, layout); });
  ToplevelSurfaceConfig config{
      .width = 1120,
      .height = 760,
      .minWidth = 560,
      .minHeight = 380,
      .title = "Nythera File Manager",
      .appId = "dev.nythera.FileManager"
  };
  wl_output* output = m_wayland.outputs().empty() ? nullptr : m_wayland.outputs().front().output;
  if (!m_surface->initialize(output, config))
    throw std::runtime_error("could not create xdg_toplevel");

  m_wayland.setPointerEventCallback([this](const PointerEvent& event) { handlePointer(event); });
  m_wayland.setKeyboardEventCallback([this](const KeyboardEvent& event) { handleKeyboard(event); });
  m_dispatcher.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
    m_wayland.setCursorShape(serial, shape);
  });
  navigate(m_path, false);
  MainLoop loop(
      m_wayland, [this] { return std::vector<PollSource*>{&m_gioPollSource, &m_configPollSource}; },
      [this] { return !m_running; },
      [this] {
        m_dispatcher.setSceneRoot(nullptr);
        if (m_surface) {
          m_surface->setSceneRoot(nullptr);
          m_surface.reset();
        }
      }
  );
  loop.run();
  return 0;
}

void FileManager::navigate(const std::filesystem::path& requestedPath, bool recordHistory) {
  std::error_code ec;
  auto path = std::filesystem::absolute(requestedPath, ec).lexically_normal();
  if (ec)
    path = requestedPath.lexically_normal();
  if (recordHistory && path != m_path) {
    m_backHistory.push_back(m_path);
    m_forwardHistory.clear();
  }
  if (path != m_path) {
    m_selectedPath.reset();
    m_chooseFirstAfterLoad = true;
    m_entries.clear();
    if (m_grid != nullptr) {
      m_grid->setSelectedIndex(std::nullopt, false);
      m_grid->notifyDataChanged();
    }
  }
  m_lastPointerIndex.reset();
  m_path = std::move(path);
  if (m_locationInput != nullptr)
    m_locationInput->setValue(m_path.string());
  rebuildBreadcrumbs();
  updateNavigationControls();
  enumerate(m_path);
}

void FileManager::enumerate(const std::filesystem::path& path) {
  closeMonitor();
  m_path = path;
  const auto generation = ++m_enumerationGeneration;
  m_loading = true;
  m_loadingEntries.clear();
  m_pendingMonitorChanges.clear();
  setStatus("Loading…");
  rebuildBreadcrumbs();
  if (m_locationInput != nullptr)
    m_locationInput->setValue(m_path.string());
  if (m_surface)
    m_surface->requestLayout();
  monitorDirectory();

  GFile* file = g_file_new_for_path(path.c_str());
  g_file_enumerate_children_async(
      file, "standard::name,standard::type,standard::size,time::modified", G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
      nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<std::pair<FileManager*, std::uint64_t>> request(
            static_cast<std::pair<FileManager*, std::uint64_t>*>(data)
        );
        auto* self = request->first;
        const auto requestGeneration = request->second;
        GError* error = nullptr;
        GFileEnumerator* enumerator = g_file_enumerate_children_finish(G_FILE(source), result, &error);
        if (requestGeneration != self->m_enumerationGeneration) {
          if (enumerator != nullptr)
            g_object_unref(enumerator);
          g_clear_error(&error);
          return;
        }
        if (enumerator == nullptr) {
          self->m_loading = false;
          self->m_loadingEntries.clear();
          self->setStatus(error == nullptr ? "Unable to open this folder" : error->message);
          g_clear_error(&error);
          if (self->m_surface)
            self->m_surface->requestLayout();
          self->applyPendingMonitorChanges();
          return;
        }
        g_clear_error(&error);
        self->fetchNext(enumerator, requestGeneration);
        g_object_unref(enumerator);
      },
      new std::pair<FileManager*, std::uint64_t>{this, generation}
  );
  g_object_unref(file);
}

void FileManager::fetchNext(GFileEnumerator* enumerator, std::uint64_t generation) {
  g_file_enumerator_next_files_async(
      enumerator, 128, G_PRIORITY_DEFAULT, nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<std::tuple<FileManager*, GFileEnumerator*, std::uint64_t>> state(
            static_cast<std::tuple<FileManager*, GFileEnumerator*, std::uint64_t>*>(data)
        );
        auto* self = std::get<0>(*state);
        auto* activeEnumerator = std::get<1>(*state);
        const auto requestGeneration = std::get<2>(*state);
        GError* error = nullptr;
        GList* infos = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), result, &error);
        if (requestGeneration != self->m_enumerationGeneration) {
          g_list_free_full(infos, g_object_unref);
          g_clear_error(&error);
          g_object_unref(activeEnumerator);
          return;
        }
        if (error != nullptr) {
          self->m_loading = false;
          self->m_loadingEntries.clear();
          self->setStatus(error->message);
          g_clear_error(&error);
          g_list_free_full(infos, g_object_unref);
          g_object_unref(activeEnumerator);
          if (self->m_surface)
            self->m_surface->requestLayout();
          self->applyPendingMonitorChanges();
          return;
        }
        for (GList* node = infos; node != nullptr; node = node->next) {
          auto* info = G_FILE_INFO(node->data);
          const char* name = g_file_info_get_name(info);
          if (name == nullptr || (!self->m_showHidden && name[0] == '.'))
            continue;
          self->m_loadingEntries.push_back(makeFileEntry(info, self->m_path / name));
        }
        g_list_free_full(infos, g_object_unref);
        if (infos == nullptr) {
          self->m_loading = false;
          self->m_entries = std::move(self->m_loadingEntries);
          self->sortEntries();
          self->setStatus(self->m_entries.empty() ? "This folder is empty" : "");
          if (self->m_grid != nullptr) {
            self->m_grid->notifyDataChanged();
          }
          self->restoreSelection(self->m_chooseFirstAfterLoad);
          self->m_chooseFirstAfterLoad = false;
          if (self->m_surface)
            self->m_surface->requestLayout();
          g_object_unref(activeEnumerator);
          self->applyPendingMonitorChanges();
          return;
        }
        self->fetchNext(activeEnumerator, requestGeneration);
        g_object_unref(activeEnumerator);
      },
      new std::tuple<FileManager*, GFileEnumerator*, std::uint64_t>{
          this, G_FILE_ENUMERATOR(g_object_ref(enumerator)), generation
      }
  );
}

void FileManager::sortEntries() {
  std::ranges::sort(m_entries, [this](const FileEntry& lhs, const FileEntry& rhs) {
    if (lhs.isDir != rhs.isDir)
      return lhs.isDir;
    int comparison = 0;
    switch (m_sortField) {
    case FileDialogSortField::Name:
      comparison = g_utf8_collate(lhs.name.c_str(), rhs.name.c_str());
      break;
    case FileDialogSortField::Size:
      comparison = lhs.size < rhs.size ? -1 : (lhs.size > rhs.size ? 1 : 0);
      break;
    case FileDialogSortField::Modified:
      comparison = lhs.mtime < rhs.mtime ? -1 : (lhs.mtime > rhs.mtime ? 1 : 0);
      break;
    }
    if (comparison == 0)
      comparison = g_utf8_collate(lhs.name.c_str(), rhs.name.c_str());
    return m_sortOrder == FileDialogSortOrder::Ascending ? comparison < 0 : comparison > 0;
  });
}

void FileManager::restoreSelection(bool chooseFirst) {
  if (m_grid == nullptr)
    return;
  if (m_selectedPath.has_value()) {
    const auto selected = std::ranges::find(m_entries, *m_selectedPath, &FileEntry::absPath);
    if (selected != m_entries.end()) {
      m_grid->setSelectedIndex(static_cast<std::size_t>(selected - m_entries.begin()), false);
      return;
    }
    if (m_loading)
      return;
    m_selectedPath.reset();
  }
  if (chooseFirst && !m_entries.empty()) {
    m_grid->setSelectedIndex(0, false);
    return;
  }
  m_grid->setSelectedIndex(std::nullopt, false);
}

void FileManager::activateSelected() {
  if (m_grid == nullptr)
    return;
  const auto selected = m_grid->selectedIndex();
  if (!selected || *selected >= m_entries.size())
    return;
  const auto entry = m_entries[*selected];
  if (entry.isDir) {
    navigate(entry.absPath);
    return;
  }
  GFile* file = g_file_new_for_path(entry.absPath.c_str());
  char* uri = g_file_get_uri(file);
  g_app_info_launch_default_for_uri_async(
      uri, nullptr, nullptr,
      [](GObject*, GAsyncResult* result, gpointer data) {
        auto* self = static_cast<FileManager*>(data);
        GError* error = nullptr;
        if (!g_app_info_launch_default_for_uri_finish(result, &error))
          self->setStatus(error == nullptr ? "Could not open this file" : error->message);
        else
          self->setStatus("");
        g_clear_error(&error);
      },
      this
  );
  g_free(uri);
  g_object_unref(file);
}

void FileManager::activatePointerItem(std::size_t index) {
  if (!m_grid || index >= m_entries.size())
    return;
  m_grid->setSelectedIndex(index, false);
  const std::uint32_t elapsed = m_pointerClickTime - m_lastPointerClickTime;
  if (m_lastPointerIndex == index && elapsed <= 500U) {
    m_lastPointerIndex.reset();
    activateSelected();
    return;
  }
  m_lastPointerIndex = index;
  m_lastPointerClickTime = m_pointerClickTime;
}

void FileManager::setStatus(std::string text) {
  if (m_statusLabel != nullptr)
    m_statusLabel->setText(text);
}

void FileManager::updateNavigationControls() {
  if (m_backButton != nullptr)
    m_backButton->setEnabled(!m_backHistory.empty());
  if (m_forwardButton != nullptr)
    m_forwardButton->setEnabled(!m_forwardHistory.empty());
  if (m_upButton != nullptr)
    m_upButton->setEnabled(m_path.has_parent_path() && m_path.parent_path() != m_path);
  for (const auto& [button, path] : m_placeButtons)
    button->setSelected(path == m_path);
  if (m_sortNameButton != nullptr)
    m_sortNameButton->setSelected(m_sortField == FileDialogSortField::Name);
  if (m_sortSizeButton != nullptr)
    m_sortSizeButton->setSelected(m_sortField == FileDialogSortField::Size);
  if (m_sortModifiedButton != nullptr)
    m_sortModifiedButton->setSelected(m_sortField == FileDialogSortField::Modified);
  if (m_sortOrderButton != nullptr)
    m_sortOrderButton->setText(m_sortOrder == FileDialogSortOrder::Ascending ? "↑" : "↓");
}

void FileManager::rebuildBreadcrumbs() {
  if (m_breadcrumbBar == nullptr)
    return;
  while (!m_breadcrumbBar->children().empty())
    m_breadcrumbBar->removeChild(m_breadcrumbBar->children().front().get());
  auto current = m_path.root_path();
  auto addCrumb = [this](std::string label, std::filesystem::path path, bool last) {
    if (!m_breadcrumbBar->children().empty()) {
      auto separator = std::make_unique<Label>();
      separator->setText("›");
      separator->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
      separator->setFontSize(Style::fontSizeCaption);
      m_breadcrumbBar->addChild(std::move(separator));
    }
    auto button = std::make_unique<Button>();
    button->setText(label);
    button->setVariant(ButtonVariant::Ghost);
    useSubtleSelection(*button);
    button->setSelected(last);
    button->setControlHeight(kToolbarHeight);
    button->setPadding(5.0F);
    button->setOnClick([this, path = std::move(path)] { navigate(path); });
    m_breadcrumbBar->addChild(std::move(button));
  };
  if (!current.empty())
    addCrumb("Filesystem", current, m_path == current);
  for (const auto& component : m_path.relative_path()) {
    current /= component;
    addCrumb(component.string(), current, current == m_path);
  }
}

void FileManager::setLocationEditMode(bool editing) {
  m_editingLocation = editing;
  if (m_locationInput != nullptr) {
    m_locationInput->setVisible(editing);
    m_locationInput->setParticipatesInLayout(editing);
    if (!editing)
      m_locationInput->setValue(m_path.string());
  }
  if (m_breadcrumbBar != nullptr) {
    m_breadcrumbBar->setVisible(!editing);
    m_breadcrumbBar->setParticipatesInLayout(!editing);
  }
  if (m_locationEditButton != nullptr)
    m_locationEditButton->setText(editing ? "Done" : "✎");
  if (editing && m_locationInput != nullptr) {
    m_locationInput->setValue(m_path.string());
    m_dispatcher.setFocus(m_locationInput->inputArea());
    m_locationInput->selectAll();
  } else if (m_grid != nullptr) {
    m_dispatcher.setFocus(m_grid->focusArea());
  }
  if (m_surface != nullptr)
    m_surface->requestLayout();
}

void FileManager::setSortField(FileDialogSortField field) {
  if (m_sortField == field)
    return;
  m_sortField = field;
  sortEntries();
  if (m_grid != nullptr) {
    m_grid->notifyDataChanged();
    restoreSelection();
  }
  updateNavigationControls();
}

void FileManager::toggleSortOrder() {
  m_sortOrder =
      m_sortOrder == FileDialogSortOrder::Ascending ? FileDialogSortOrder::Descending : FileDialogSortOrder::Ascending;
  sortEntries();
  if (m_grid != nullptr) {
    m_grid->notifyDataChanged();
    restoreSelection();
  }
  updateNavigationControls();
}

void FileManager::monitorDirectory() {
  closeMonitor();
  GFile* file = g_file_new_for_path(m_path.c_str());
  GError* error = nullptr;
  m_monitor = g_file_monitor_directory(file, G_FILE_MONITOR_WATCH_MOVES, nullptr, &error);
  g_object_unref(file);
  if (m_monitor != nullptr) {
    g_signal_connect(
        m_monitor, "changed",
        G_CALLBACK(+[](GFileMonitor* monitor, GFile* changedFile, GFile* otherFile, GFileMonitorEvent event,
                       gpointer data) {
          auto* self = static_cast<FileManager*>(data);
          if (monitor == self->m_monitor)
            self->handleMonitorEvent(changedFile, otherFile, event);
        }),
        this
    );
  }
  g_clear_error(&error);
}

void FileManager::handleMonitorEvent(GFile* file, GFile* otherFile, GFileMonitorEvent event) {
  if (file == nullptr)
    return;
  char* rawPath = g_file_get_path(file);
  char* rawOtherPath = otherFile == nullptr ? nullptr : g_file_get_path(otherFile);
  if (rawPath == nullptr) {
    g_free(rawOtherPath);
    return;
  }
  PendingMonitorChange change{
      .event = event,
      .path = std::filesystem::path(rawPath).lexically_normal(),
      .otherPath =
          rawOtherPath == nullptr ? std::filesystem::path{} : std::filesystem::path(rawOtherPath).lexically_normal(),
  };
  g_free(rawPath);
  g_free(rawOtherPath);
  if (change.path == m_path || change.path.parent_path() != m_path)
    return;
  if (m_loading) {
    m_pendingMonitorChanges.push_back(std::move(change));
    return;
  }

  switch (change.event) {
  case G_FILE_MONITOR_EVENT_DELETED:
  case G_FILE_MONITOR_EVENT_MOVED_OUT:
    removeMonitoredFile(change.path);
    break;
  case G_FILE_MONITOR_EVENT_RENAMED:
  case G_FILE_MONITOR_EVENT_MOVED:
    if (!change.otherPath.empty() && change.otherPath.parent_path() == m_path)
      queryMonitoredFile(change.otherPath, change.path);
    else
      removeMonitoredFile(change.path);
    break;
  case G_FILE_MONITOR_EVENT_CREATED:
  case G_FILE_MONITOR_EVENT_MOVED_IN:
  case G_FILE_MONITOR_EVENT_CHANGED:
  case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
  case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
    queryMonitoredFile(change.path);
    break;
  case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
  case G_FILE_MONITOR_EVENT_UNMOUNTED:
    setStatus("This folder is no longer available");
    break;
  }
}

void FileManager::applyPendingMonitorChanges() {
  auto changes = std::move(m_pendingMonitorChanges);
  m_pendingMonitorChanges.clear();
  for (const auto& change : changes) {
    GFile* file = g_file_new_for_path(change.path.c_str());
    GFile* otherFile = change.otherPath.empty() ? nullptr : g_file_new_for_path(change.otherPath.c_str());
    handleMonitorEvent(file, otherFile, change.event);
    g_object_unref(file);
    if (otherFile != nullptr)
      g_object_unref(otherFile);
  }
}

void FileManager::queryMonitoredFile(const std::filesystem::path& path, const std::filesystem::path& replacedPath) {
  GFile* file = g_file_new_for_path(path.c_str());
  g_file_query_info_async(
      file, "standard::name,standard::type,standard::size,time::modified", G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT,
      nullptr,
      [](GObject* source, GAsyncResult* result, gpointer data) {
        std::unique_ptr<MonitoredFileQuery> query(static_cast<MonitoredFileQuery*>(data));
        auto* self = query->manager;
        GError* error = nullptr;
        GFileInfo* info = g_file_query_info_finish(G_FILE(source), result, &error);
        if (query->generation != self->m_enumerationGeneration || query->path.parent_path() != self->m_path) {
          if (info != nullptr)
            g_object_unref(info);
          g_clear_error(&error);
          return;
        }
        if (info == nullptr) {
          g_clear_error(&error);
          self->removeMonitoredFile(query->replacedPath.empty() ? query->path : query->replacedPath);
          return;
        }
        g_clear_error(&error);
        const char* name = g_file_info_get_name(info);
        if (name == nullptr || (!self->m_showHidden && name[0] == '.')) {
          g_object_unref(info);
          self->removeMonitoredFile(query->replacedPath.empty() ? query->path : query->replacedPath);
          return;
        }

        FileEntry entry = makeFileEntry(info, query->path);
        g_object_unref(info);
        auto oldEntry = std::ranges::find(self->m_entries, query->path, &FileEntry::absPath);
        const auto replacedEntry = query->replacedPath.empty()
            ? self->m_entries.end()
            : std::ranges::find(self->m_entries, query->replacedPath, &FileEntry::absPath);
        if (query->replacedPath.empty() && oldEntry != self->m_entries.end() && sameEntry(*oldEntry, entry))
          return;
        if (!query->replacedPath.empty() && self->m_selectedPath == query->replacedPath)
          self->m_selectedPath = query->path;
        if (replacedEntry != self->m_entries.end())
          self->m_entries.erase(replacedEntry);
        oldEntry = std::ranges::find(self->m_entries, query->path, &FileEntry::absPath);
        if (oldEntry != self->m_entries.end())
          *oldEntry = std::move(entry);
        else
          self->m_entries.push_back(std::move(entry));

        self->sortEntries();
        if (self->m_grid != nullptr) {
          self->m_grid->notifyDataChanged();
          self->restoreSelection();
        }
        self->setStatus("");
        if (self->m_surface)
          self->m_surface->requestLayout();
      },
      new MonitoredFileQuery{this, m_enumerationGeneration, path, replacedPath}
  );
  g_object_unref(file);
}

void FileManager::removeMonitoredFile(const std::filesystem::path& path) {
  if (path.empty())
    return;
  const auto oldSize = m_entries.size();
  std::erase_if(m_entries, [&path](const FileEntry& entry) { return entry.absPath == path; });
  if (oldSize == m_entries.size())
    return;
  if (m_selectedPath == path)
    m_selectedPath.reset();
  if (m_grid != nullptr) {
    m_grid->notifyDataChanged();
    restoreSelection();
  }
  setStatus(m_entries.empty() ? "This folder is empty" : "");
  if (m_surface)
    m_surface->requestLayout();
}

void FileManager::closeMonitor() {
  if (m_monitor != nullptr) {
    g_file_monitor_cancel(m_monitor);
    g_object_unref(m_monitor);
    m_monitor = nullptr;
  }
}

void FileManager::prepareFrame(bool needsUpdate, bool needsLayout) {
  (void)needsUpdate;
  if (!m_surface || m_surface->width() == 0 || m_surface->height() == 0)
    return;
  m_renderContext.makeCurrent(m_surface->renderTarget());
  Renderer& renderer = m_surface->renderTarget().renderer();
  m_adapter->setRenderer(&renderer);
  if (!m_root) {
    constexpr float pad = 8.0F;
    m_root = std::make_unique<Flex>();
    m_root->setDirection(FlexDirection::Vertical);
    m_root->setAlign(FlexAlign::Stretch);
    m_root->setGap(3.0F);
    m_root->setPadding(pad);
    m_root->setFill(colorSpecFromRole(ColorRole::SurfaceContainerLow, 0.88F));
    m_root->setBorder(colorSpecFromRole(ColorRole::OutlineVariant, 0.72F), Style::borderWidth);
    applyNytheraBevel(*m_root, Style::scaledRadiusLg(1.0F));

    auto appBar = std::make_unique<Flex>();
    appBar->setAlign(FlexAlign::Center);
    appBar->setGap(3.0F);
    appBar->setMinHeight(34.0F);
    appBar->setPadding(Style::spaceSm, Style::spaceMd);
    appBar->setFill(colorSpecFromRole(ColorRole::SurfaceContainerHigh, 0.72F));
    appBar->setBorder(colorSpecFromRole(ColorRole::Primary, 0.38F), Style::borderWidth);
    applyNytheraBevel(*appBar, Style::scaledRadiusMd(1.0F));
    auto appName = std::make_unique<Label>();
    appName->setText("NYTHERA  //  FILES");
    appName->setFontSize(Style::fontSizeBody);
    appName->setFontWeight(FontWeight::SemiBold);
    appName->setColor(colorSpecFromRole(ColorRole::Primary));
    appBar->addChild(std::move(appName));
    auto appSpacer = std::make_unique<Flex>();
    appSpacer->setFlexGrow(1.0F);
    appBar->addChild(std::move(appSpacer));

    auto sortName = makeButton("Name", [this] { setSortField(FileDialogSortField::Name); }, &m_sortNameButton);
    sortName->setVariant(ButtonVariant::Outline);
    useSubtleSelection(*sortName);
    appBar->addChild(std::move(sortName));
    auto sortSize = makeButton("Size", [this] { setSortField(FileDialogSortField::Size); }, &m_sortSizeButton);
    sortSize->setVariant(ButtonVariant::Outline);
    useSubtleSelection(*sortSize);
    appBar->addChild(std::move(sortSize));
    auto sortModified =
        makeButton("Modified", [this] { setSortField(FileDialogSortField::Modified); }, &m_sortModifiedButton);
    sortModified->setVariant(ButtonVariant::Outline);
    useSubtleSelection(*sortModified);
    appBar->addChild(std::move(sortModified));
    appBar->addChild(makeButton("↑", [this] { toggleSortOrder(); }, &m_sortOrderButton));
    appBar->addChild(makeButton(
        "List",
        [this] {
          m_gridMode = !m_gridMode;
          m_viewButton->setText(m_gridMode ? "Grid" : "List");
          if (m_grid) {
            m_grid->setAdapter(nullptr);
            m_grid->setAdapter(m_adapter.get());
            m_grid->setColumns(m_gridMode ? 0 : 1);
            m_grid->setMinCellWidth(kGridCellWidth);
            m_grid->setCellHeight(m_gridMode ? kGridCellHeight : kListRowHeight);
            m_grid->setSquareCells(false);
            m_grid->notifyDataChanged();
          }
        },
        &m_viewButton
    ));
    appBar->addChild(makeButton(
        "Hidden",
        [this] {
          m_showHidden = !m_showHidden;
          m_hiddenButton->setText(m_showHidden ? "Hidden ✓" : "Hidden");
          enumerate(m_path);
        },
        &m_hiddenButton
    ));
    m_root->addChild(std::move(appBar));

    auto content = std::make_unique<Flex>();
    content->setAlign(FlexAlign::Stretch);
    content->setGap(7.0F);
    content->setFlexGrow(1.0F);

    auto sidebar = std::make_unique<Flex>();
    sidebar->setDirection(FlexDirection::Vertical);
    sidebar->setAlign(FlexAlign::Stretch);
    sidebar->setGap(3.0F);
    sidebar->setPadding(7.0F);
    sidebar->setMinWidth(kSidebarWidth);
    sidebar->setMaxWidth(kSidebarWidth);
    sidebar->setFill(colorSpecFromRole(ColorRole::SurfaceContainer, 0.62F));
    sidebar->setBorder(colorSpecFromRole(ColorRole::OutlineVariant, 0.42F), Style::borderWidth);
    applyNytheraBevel(*sidebar, Style::scaledRadiusMd(1.0F));
    auto placesTitle = std::make_unique<Label>();
    placesTitle->setText("Places");
    placesTitle->setFontSize(Style::fontSizeCaption);
    placesTitle->setFontWeight(FontWeight::Medium);
    placesTitle->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    sidebar->addChild(std::move(placesTitle));

    const char* homeValue = std::getenv("HOME");
    const std::filesystem::path home = homeValue == nullptr ? std::filesystem::path("/") : homeValue;
    const std::vector<std::pair<std::string, std::filesystem::path>> places = {
        {"Home", home},
        {"Desktop", home / "Desktop"},
        {"Documents", home / "Documents"},
        {"Downloads", home / "Downloads"},
        {"Music", home / "Music"},
        {"Pictures", home / "Pictures"},
        {"Videos", home / "Videos"},
    };
    m_placeButtons.clear();
    for (const auto& [label, path] : places) {
      auto placeButton = makeButton(label, [this, path] { navigate(path); });
      useSubtleSelection(*placeButton);
      placeButton->setContentAlign(ButtonContentAlign::Start);
      m_placeButtons.emplace_back(placeButton.get(), path);
      sidebar->addChild(std::move(placeButton));
    }
    m_sidebar = static_cast<Flex*>(content->addChild(std::move(sidebar)));

    auto mainPane = std::make_unique<Flex>();
    mainPane->setDirection(FlexDirection::Vertical);
    mainPane->setAlign(FlexAlign::Stretch);
    mainPane->setGap(3.0F);
    mainPane->setFlexGrow(1.0F);

    auto navigation = std::make_unique<Flex>();
    navigation->setAlign(FlexAlign::Center);
    navigation->setGap(2.0F);
    navigation->addChild(makeButton(
        "‹",
        [this] {
          if (m_backHistory.empty())
            return;
          auto path = m_backHistory.back();
          m_backHistory.pop_back();
          m_forwardHistory.push_back(m_path);
          navigate(path, false);
        },
        &m_backButton
    ));
    navigation->addChild(makeButton(
        "›",
        [this] {
          if (m_forwardHistory.empty())
            return;
          auto path = m_forwardHistory.back();
          m_forwardHistory.pop_back();
          m_backHistory.push_back(m_path);
          navigate(path, false);
        },
        &m_forwardButton
    ));
    navigation->addChild(makeButton(
        "↑",
        [this] {
          if (m_path.has_parent_path())
            navigate(m_path.parent_path());
        },
        &m_upButton
    ));
    navigation->addChild(makeButton(
        "Home",
        [this] {
          const char* homeDirectory = std::getenv("HOME");
          navigate(std::filesystem::path(homeDirectory == nullptr ? "/" : homeDirectory));
        },
        &m_homeButton
    ));

    auto locationBar = std::make_unique<Flex>();
    locationBar->setAlign(FlexAlign::Center);
    locationBar->setGap(2.0F);
    locationBar->setPadding(2.0F, 4.0F);
    locationBar->setFlexGrow(1.0F);
    locationBar->setFill(colorSpecFromRole(ColorRole::SurfaceContainerLow, 0.78F));
    locationBar->setBorder(colorSpecFromRole(ColorRole::Primary, 0.28F), Style::borderWidth);
    applyNytheraBevel(*locationBar, Style::scaledRadiusSm(1.0F));
    auto location = std::make_unique<Input>();
    m_locationInput = location.get();
    m_locationInput->setValue(m_path.string());
    m_locationInput->setPlaceholder("Enter a folder path");
    m_locationInput->setFlexGrow(1.0F);
    m_locationInput->setVisible(false);
    m_locationInput->setParticipatesInLayout(false);
    m_locationInput->setOnSubmit([this](const std::string& value) {
      navigate(value);
      setLocationEditMode(false);
    });
    locationBar->addChild(std::move(location));
    auto crumb = std::make_unique<Flex>();
    crumb->setAlign(FlexAlign::Center);
    crumb->setGap(1.0F);
    crumb->setFlexGrow(1.0F);
    crumb->setClipChildren(true);
    crumb->setMinHeight(kToolbarHeight);
    m_breadcrumbBar = static_cast<Flex*>(locationBar->addChild(std::move(crumb)));
    locationBar->addChild(makeButton(
        "✎",
        [this] {
          if (m_editingLocation && m_locationInput != nullptr) {
            navigate(m_locationInput->value());
            setLocationEditMode(false);
          } else {
            setLocationEditMode(true);
          }
        },
        &m_locationEditButton
    ));
    navigation->addChild(std::move(locationBar));
    navigation->addChild(makeButton("↻", [this] { enumerate(m_path); }, &m_refreshButton));
    mainPane->addChild(std::move(navigation));

    auto fileHeader = std::make_unique<Flex>();
    fileHeader->setAlign(FlexAlign::Center);
    fileHeader->setGap(Style::spaceSm);
    fileHeader->setPadding(5.0F, Style::spaceSm);
    auto nameHeader = std::make_unique<Label>();
    nameHeader->setText("Name");
    nameHeader->setFontSize(Style::fontSizeCaption);
    nameHeader->setFontWeight(FontWeight::Medium);
    nameHeader->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    nameHeader->setFlexGrow(1.0F);
    fileHeader->addChild(std::move(nameHeader));
    auto sizeHeader = std::make_unique<Label>();
    sizeHeader->setText("Size");
    sizeHeader->setFontSize(Style::fontSizeCaption);
    sizeHeader->setTextAlign(TextAlign::End);
    sizeHeader->setMinWidth(96.0F);
    sizeHeader->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    fileHeader->addChild(std::move(sizeHeader));
    auto modifiedHeader = std::make_unique<Label>();
    modifiedHeader->setText("Modified");
    modifiedHeader->setFontSize(Style::fontSizeCaption);
    modifiedHeader->setTextAlign(TextAlign::End);
    modifiedHeader->setMinWidth(152.0F);
    modifiedHeader->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    fileHeader->addChild(std::move(modifiedHeader));
    mainPane->addChild(std::move(fileHeader));
    mainPane->addChild(
        ui::separator({
            .color = colorSpecFromRole(ColorRole::OutlineVariant, 0.45F),
            .gradientEdges = false,
        })
    );

    auto grid = std::make_unique<VirtualGridView>();
    m_grid = grid.get();
    m_grid->setFlexGrow(1.0F);
    m_grid->setFill(colorSpecFromRole(ColorRole::Surface, 0.38F));
    m_grid->setBorder(colorSpecFromRole(ColorRole::OutlineVariant, 0.26F), Style::borderWidth);
    applyNytheraBevel(*m_grid, Style::scaledRadiusMd(1.0F));
    m_grid->setAdapter(m_adapter.get());
    m_grid->setColumns(1);
    m_grid->setSquareCells(false);
    m_grid->setCellHeight(kListRowHeight);
    m_grid->setOnSelectionChanged([this](std::optional<std::size_t> index) {
      if (index.has_value() && *index < m_entries.size())
        m_selectedPath = m_entries[*index].absPath;
    });
    mainPane->addChild(std::move(grid));

    auto status = std::make_unique<Label>();
    m_statusLabel = status.get();
    m_statusLabel->setText("Loading…");
    m_statusLabel->setFontSize(Style::fontSizeCaption);
    mainPane->addChild(std::move(status));
    content->addChild(std::move(mainPane));
    m_root->addChild(std::move(content));
    m_dispatcher.setSceneRoot(m_root.get());
    m_dispatcher.setFocus(m_grid->focusArea());
    m_surface->setSceneRoot(m_root.get());
    rebuildBreadcrumbs();
    updateNavigationControls();
    setLocationEditMode(false);
    needsLayout = true;
  }
  if (needsLayout) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    const bool showSidebar = m_surface->width() >= 760;
    m_sidebar->setVisible(showSidebar);
    m_sidebar->setParticipatesInLayout(showSidebar);
    m_root->setSize(static_cast<float>(m_surface->width()), static_cast<float>(m_surface->height()));
    m_root->layout(renderer);
  }
}

void FileManager::handlePointer(const PointerEvent& event) {
  if (!m_surface || event.surface != m_surface->wlSurface())
    return;
  const float x = static_cast<float>(event.sx), y = static_cast<float>(event.sy);
  switch (event.type) {
  case PointerEvent::Type::Enter:
    m_dispatcher.pointerEnter(x, y, event.serial);
    break;
  case PointerEvent::Type::Leave:
    m_dispatcher.pointerLeave();
    break;
  case PointerEvent::Type::Motion:
    m_dispatcher.pointerMotion(x, y, event.serial);
    break;
  case PointerEvent::Type::Button:
    if (event.button == BTN_LEFT) {
      if (event.pressed)
        m_pointerClickTime = event.time;
      m_dispatcher.pointerButton(x, y, event.button, event.pressed, event.serial, event.time, false);
    }
    break;
  case PointerEvent::Type::Axis:
    m_dispatcher.pointerAxis(
        x, y, event.axis, event.axisSource, event.axisValue, event.axisDiscrete, event.axisValue120, event.axisLines,
        event.axisGestureSerial
    );
    break;
  }
}

void FileManager::handleKeyboard(const KeyboardEvent& event) {
  if (!event.pressed || event.preedit || !m_surface || m_wayland.lastKeyboardSurface() != m_surface->wlSurface())
    return;
  if (event.sym == XKB_KEY_Escape) {
    if (m_editingLocation)
      setLocationEditMode(false);
    // An application window stays open when there is nothing to cancel.
    m_dispatcher.cancelPointerCapture();
    return;
  }
  if ((event.modifiers & KeyMod::Ctrl) != 0U && event.sym == XKB_KEY_l) {
    setLocationEditMode(true);
    return;
  }
  if (m_locationInput != nullptr && m_dispatcher.focusedArea() == m_locationInput->inputArea()) {
    if (event.sym == XKB_KEY_Return || event.sym == XKB_KEY_KP_Enter) {
      navigate(m_locationInput->value());
      setLocationEditMode(false);
    } else {
      m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    }
    return;
  }
  if (event.sym == XKB_KEY_BackSpace) {
    if (m_path.has_parent_path() && m_path.parent_path() != m_path)
      navigate(m_path.parent_path());
    return;
  }
  if (event.sym == XKB_KEY_r && (event.modifiers & KeyMod::Ctrl) != 0U) {
    enumerate(m_path);
    return;
  }
  if (m_grid == nullptr)
    return;
  if (event.sym == XKB_KEY_Return || event.sym == XKB_KEY_KP_Enter) {
    activateSelected();
    return;
  }
  const auto current = m_grid->selectedIndex();
  const auto selected = current.value_or(0);
  if (event.sym == XKB_KEY_Down && !current && !m_entries.empty())
    m_grid->setSelectedIndex(0);
  else if (event.sym == XKB_KEY_Down && selected + 1 < m_entries.size())
    m_grid->setSelectedIndex(selected + 1);
  else if (event.sym == XKB_KEY_Up && selected > 0)
    m_grid->setSelectedIndex(selected - 1);
  else
    m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
}
