#include "filemanager/file_manager.h"

#include <exception>
#include <print>

int main(int argc, char** argv) {
  try {
    FileManager manager(argc > 1 ? argv[1] : "");
    return manager.run();
  } catch (const std::exception& error) {
    std::println(stderr, "nythera-file-manager: {}", error.what());
    return 1;
  }
}
