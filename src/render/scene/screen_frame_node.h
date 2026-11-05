#pragma once

#include "render/core/screen_frame_geometry.h"
#include "render/scene/node.h"

class ScreenFrameNode : public Node {
public:
  ScreenFrameNode() : Node(NodeType::ScreenFrame) {}
  const ScreenFrameStyle& style() const { return m_style; }
  void setStyle(const ScreenFrameStyle& style) {
    if (m_style == style)
      return;
    m_style = style;
    markPaintDirty();
  }

private:
  ScreenFrameStyle m_style;
};
