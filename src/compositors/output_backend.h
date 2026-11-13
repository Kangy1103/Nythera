#pragma once

struct wl_output;

struct OutputWorkArea {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

class OutputLifecycleObserver {
public:
  virtual ~OutputLifecycleObserver() = default;
  virtual void onOutputAdded(wl_output* output) = 0;
  virtual void onOutputRemoved(wl_output* output) = 0;
};
