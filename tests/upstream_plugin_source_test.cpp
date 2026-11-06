#include "scripting/upstream_plugin_source.h"

#include <cassert>
int main() {
  using nythera::plugins::importUpstreamSource;
  assert(importUpstreamSource("noctalia.getConfig('x')") == "nythera.getConfig('x')");
  assert(importUpstreamSource("-- noctalia\nnoctalia.notify('noctalia')") == "-- noctalia\nnythera.notify('nythera')");
  assert(
      importUpstreamSource("local a = [=[noctalia]=]; noctalia.nowMs()") == "local a = [=[noctalia]=]; nythera.nowMs()"
  );
  assert(
      importUpstreamSource("--[[noctalia]]\nlocal noctaliaExtra = 'https://noctalia.dev'")
      == "--[[noctalia]]\nlocal noctaliaExtra = 'https://noctalia.dev'"
  );
  assert(
      importUpstreamSource("noctalia.runAsync({'noctalia', 'msg', 'status'})")
      == "nythera.runAsync({'nythera', 'msg', 'status'})"
  );
  assert(importUpstreamSource("noctalia.runAsync('noctalia msg status')") == "nythera.runAsync('nythera msg status')");
  assert(importUpstreamSource("'dev.noctalia.Debug'") == "'dev.nythera.Debug'");
  assert(importUpstreamSource("'noctalia/wallpaper_depth'") == "'noctalia/wallpaper_depth'");
}
