#include "render/programs/screen_frame_program.h"

#include "render/core/screen_frame_geometry.h"

#include <array>
#include <stdexcept>

namespace {
  constexpr char kVertex[] = R"(
precision highp float;
attribute vec2 a_position;
uniform vec2 u_surface_size;
uniform vec2 u_size;
uniform mat3 u_transform;
varying vec2 v_local;
void main() {
  v_local = a_position * u_size;
  vec3 pixel = u_transform * vec3(v_local, 1.0);
  vec2 p = pixel.xy / u_surface_size;
  gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)";
  constexpr char kFragment[] = R"(
#extension GL_OES_standard_derivatives : require
precision highp float;
uniform vec4 u_frame; // width, height, rail, opening radius
uniform vec3 u_shape; // island depth, island radius, junction smoothing
uniform vec2 u_offset;
uniform int u_edge;
uniform vec2 u_islands[3];
uniform vec4 u_fill;
uniform vec4 u_border;
uniform float u_border_width;
varying vec2 v_local;
float roundedDistance(vec2 p, vec2 halfSize, float r) {
  r = min(r, min(halfSize.x, halfSize.y));
  vec2 q = abs(p) - halfSize + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
  vec2 p = v_local + u_offset;
  float d = -roundedDistance(p - u_frame.xy * 0.5, u_frame.xy * 0.5 - u_frame.z, u_frame.w);
  float crossSize = (u_edge == 1 || u_edge == 3) ? u_frame.x : u_frame.y;
  vec2 uv = p;
  if (u_edge == 1) uv = vec2(p.y, u_frame.x - p.x);
  else if (u_edge == 2) uv.y = u_frame.y - p.y;
  else if (u_edge == 3) uv = p.yx;
  for (int i = 0; i < 3; ++i) {
    vec2 span = u_islands[i];
    if (span.y <= span.x) continue;
    float island = roundedDistance(uv - vec2((span.x + span.y) * 0.5, (u_shape.x - crossSize) * 0.5),
        vec2((span.y - span.x) * 0.5, (u_shape.x + crossSize) * 0.5), u_shape.y);
    float h = u_shape.z > 0.0 ? max(u_shape.z - abs(d - island), 0.0) / u_shape.z : 0.0;
    d = min(d, island) - h * h * u_shape.z * 0.25;
  }
  float aa = max(length(vec2(dFdx(d), dFdy(d))), 1e-6);
  float coverage = clamp(0.5 - d / aa, 0.0, 1.0);
  if (coverage <= 0.0) discard;
  float stroke = u_border_width > 0.0 ? clamp(0.5 + (d + u_border_width) / aa, 0.0, 1.0) : 0.0;
  vec4 fill = vec4(u_fill.rgb * u_fill.a, u_fill.a);
  vec4 border = vec4(u_border.rgb * u_border.a, u_border.a);
  gl_FragColor = mix(fill, border, stroke) * coverage;
}
)";
} // namespace

void ScreenFrameProgram::ensureInitialized() {
  if (m_program.isValid())
    return;
  m_program.create(kVertex, kFragment);
  m_positionLocation = glGetAttribLocation(m_program.id(), "a_position");
  m_surfaceSizeLocation = glGetUniformLocation(m_program.id(), "u_surface_size");
  m_sizeLocation = glGetUniformLocation(m_program.id(), "u_size");
  m_transformLocation = glGetUniformLocation(m_program.id(), "u_transform");
  m_fillLocation = glGetUniformLocation(m_program.id(), "u_fill");
  m_borderLocation = glGetUniformLocation(m_program.id(), "u_border");
  m_borderWidthLocation = glGetUniformLocation(m_program.id(), "u_border_width");
  m_frameLocation = glGetUniformLocation(m_program.id(), "u_frame");
  m_shapeLocation = glGetUniformLocation(m_program.id(), "u_shape");
  m_offsetLocation = glGetUniformLocation(m_program.id(), "u_offset");
  m_edgeLocation = glGetUniformLocation(m_program.id(), "u_edge");
  m_islandsLocation = glGetUniformLocation(m_program.id(), "u_islands[0]");
  if (m_positionLocation < 0
      || m_surfaceSizeLocation < 0
      || m_sizeLocation < 0
      || m_transformLocation < 0
      || m_fillLocation < 0
      || m_borderLocation < 0
      || m_borderWidthLocation < 0
      || m_frameLocation < 0
      || m_shapeLocation < 0
      || m_offsetLocation < 0
      || m_edgeLocation < 0
      || m_islandsLocation < 0)
    throw std::runtime_error("failed to query screen-frame shader locations");
}

void ScreenFrameProgram::destroy() { m_program.destroy(); }
void ScreenFrameProgram::abandon() noexcept { m_program.abandon(); }

void ScreenFrameProgram::draw(
    float sw, float sh, float width, float height, const ScreenFrameStyle& style, const Mat3& transform
) const {
  if (!m_program.isValid() || width <= 0 || height <= 0)
    return;
  constexpr std::array<GLfloat, 12> vertices{0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
  const auto& g = style.contour;
  std::array<GLfloat, 6> spans{};
  for (std::size_t i = 0; i < g.islands.size(); ++i) {
    spans[i * 2] = g.islands[i].begin;
    spans[i * 2 + 1] = g.islands[i].end;
  }
  glUseProgram(m_program.id());
  glUniform2f(m_surfaceSizeLocation, sw, sh);
  glUniform2f(m_sizeLocation, width, height);
  glUniformMatrix3fv(m_transformLocation, 1, GL_FALSE, transform.m.data());
  glUniform4f(m_fillLocation, style.fill.r, style.fill.g, style.fill.b, style.fill.a);
  glUniform4f(m_borderLocation, style.border.r, style.border.g, style.border.b, style.border.a);
  glUniform1f(m_borderWidthLocation, style.borderWidth);
  glUniform4f(m_frameLocation, g.width, g.height, g.rail, g.radius);
  glUniform3f(m_shapeLocation, g.depth, g.islandRadius, g.join);
  glUniform2f(m_offsetLocation, style.offsetX, style.offsetY);
  glUniform1i(m_edgeLocation, g.edge);
  glUniform2fv(m_islandsLocation, 3, spans.data());
  const auto attr = static_cast<GLuint>(m_positionLocation);
  glVertexAttribPointer(attr, 2, GL_FLOAT, GL_FALSE, 0, vertices.data());
  glEnableVertexAttribArray(attr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(attr);
}
