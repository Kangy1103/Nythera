#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace nythera::config::legacy_profile {
  struct Tree {
    std::filesystem::path source, target;
  };
  struct Result {
    std::size_t copied = 0, preserved = 0, failed = 0;
  };
  using Log = std::function<void(const std::string&)>;

  inline void replaceAll(std::string& value, const std::string& from, const std::string& to) {
    if (from.empty())
      return;
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
      value.replace(pos, from.size(), to);
      pos += to.size();
    }
  }

  // Deliberate copy, never fallback: callers select the source profile. Existing
  // destination files and all source files remain untouched, including on retry.
  inline Result migrate(const std::vector<Tree>& trees, const Log& log) {
    namespace fs = std::filesystem;
    Result result;
    for (const auto& tree : trees) {
      std::error_code ec;
      const auto source = fs::weakly_canonical(tree.source, ec);
      if (ec || !fs::exists(source)) {
        log("Source absent; skipped: " + tree.source.string());
        continue;
      }
      const auto target = fs::weakly_canonical(tree.target, ec);
      const auto relative = target.lexically_relative(source);
      if (ec || source == target || (!relative.empty() && *relative.begin() != "..")) {
        ++result.failed;
        log("Refusing overlapping migration paths");
        continue;
      }
      fs::create_directories(target, ec);
      if (ec) {
        ++result.failed;
        log("Cannot create destination: " + target.string());
        continue;
      }
      fs::recursive_directory_iterator it(source, fs::directory_options::none, ec), end;
      for (; !ec && it != end; it.increment(ec)) {
        const auto rel = it->path().lexically_relative(source);
        const auto destination = target / rel;
        const auto status = it->symlink_status(ec);
        if (ec)
          break;
        // Do not traverse or import symlinks into another installation. Report
        // them so a user can deliberately recreate external asset links.
        if (fs::is_symlink(status)) {
          ++result.preserved;
          log("Symlink not imported: " + rel.string());
          continue;
        }
        auto parent = destination.parent_path();
        bool unsafe = false;
        while (parent != target && parent != parent.root_path()) {
          if (fs::is_symlink(fs::symlink_status(parent, ec))) {
            unsafe = true;
            break;
          }
          ec.clear();
          parent = parent.parent_path();
        }
        if (unsafe || fs::is_symlink(fs::symlink_status(destination, ec))) {
          ec.clear();
          ++result.failed;
          log("Refusing destination symlink: " + rel.string());
          if (fs::is_directory(status))
            it.disable_recursion_pending();
          continue;
        }
        ec.clear();
        if (fs::is_directory(status)) {
          fs::create_directories(destination, ec);
          if (ec) {
            ++result.failed;
            log("Cannot create directory: " + rel.string());
            ec.clear();
            it.disable_recursion_pending();
          }
          continue;
        }
        if (!fs::is_regular_file(status))
          continue;
        if (fs::exists(destination, ec)) {
          ++result.preserved;
          continue;
        }
        fs::copy_file(it->path(), destination, fs::copy_options::none, ec);
        if (ec) {
          ++result.failed;
          log("Copy failed: " + rel.string() + ": " + ec.message());
          ec.clear();
          continue;
        }
        ++result.copied;
        // Only copied config/scripts are rewired; never rewrite existing user
        // data, source repositories, binary stores, endpoints or secret values.
        if (destination.extension() == ".toml") {
          std::ifstream in(destination, std::ios::binary);
          std::string text((std::istreambuf_iterator<char>(in)), {});
          for (const auto& mapping : trees)
            replaceAll(text, mapping.source.string(), mapping.target.string());
          replaceAll(text, "noctalia_state", "nythera_state");
          replaceAll(text, "noctalia:", "nythera:");
          replaceAll(text, "noctalia msg", "nythera msg");
          replaceAll(text, "build-debug/noctalia", "build-debug/nythera");
          std::ofstream out(destination, std::ios::binary | std::ios::trunc);
          out << text;
          if (!out) {
            ++result.failed;
            fs::remove(destination, ec);
            ec.clear();
            log("Could not rewrite copied config: " + rel.string());
          }
        }
      }
      if (ec) {
        ++result.failed;
        log("Source traversal failed: " + ec.message());
      }
      log("Migration tree processed: " + source.string() + " -> " + target.string());
    }
    return result;
  }
} // namespace nythera::config::legacy_profile
