#pragma once

namespace nythera::config {

  // Entry point for `nythera config <command> [options]`. Returns a process
  // exit code. Pure CLI helper; does not start Application or mutate live config.
  int runCli(int argc, char* argv[]);

} // namespace nythera::config
