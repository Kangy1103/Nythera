#pragma once

#include "render/core/mat3.h"
#include "render/core/shader_program.h"

#include <GLES2/gl2.h>

struct ScreenFrameStyle;

class ScreenFrameProgram {
public:
  ScreenFrameProgram() = default;
  ~ScreenFrameProgram() = default;

  ScreenFrameProgram(const ScreenFrameProgram&) = delete;
  ScreenFrameProgram& operator=(const ScreenFrameProgram&) = delete;

  void ensureInitialized();
  void destroy();
  void abandon() noexcept;

  void draw(
      float surfaceWidth, float surfaceHeight, float width, float height, const ScreenFrameStyle& style,
      const Mat3& transform = Mat3::identity()
  ) const;

private:
  ShaderProgram m_program;
  GLint m_hazeLocation = -1, m_hazeBoundsLocation = -1, m_hazeSizeLocation = -1;
  GLint m_positionLocation = -1;
  GLint m_surfaceSizeLocation = -1;
  GLint m_sizeLocation = -1;
  GLint m_fillLocation = -1, m_borderLocation = -1, m_borderWidthLocation = -1;
  GLint m_frameLocation = -1, m_shapeLocation = -1, m_offsetLocation = -1;
  GLint m_edgeLocation = -1, m_islandsLocation = -1;
  GLint m_exclusionLocation = -1;
  GLint m_transformLocation = -1;
  GLint m_bandCountLocation = -1, m_bandShapeLocation = -1, m_bandMetaLocation = -1;
  GLint m_bandSpansLocation = -1, m_bandFillLocation = -1, m_bandBorderLocation = -1;
  GLint m_backdropLocation = -1, m_backdropSourceLocation = -1, m_backdropBoundsLocation = -1;
  GLint m_hasBackdropLocation = -1;
  GLint m_backdropOpacityLocation = -1;
  GLint m_bandNeckLocation = -1;
  GLint m_bandChamferLocation = -1;
};
