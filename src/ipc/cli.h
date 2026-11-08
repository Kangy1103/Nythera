#pragma once

namespace nythera::ipc {

  // Entry point for `nythera msg <command> [args...]`. Returns a process exit
  // code. Forwards the command to the running instance over the IPC socket.
  int runCli(int argc, char* argv[]);

  // Entry point for the top-level `nythera restart` command.
  int runRestartCli(int argc, char* argv[]);

  // Entry point for the top-level `nythera stop` command.
  int runStopCli(int argc, char* argv[]);

} // namespace nythera::ipc
