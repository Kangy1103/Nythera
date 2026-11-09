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
uniform vec4 u_exclusion;
uniform sampler2D u_backdrop;
uniform vec4 u_backdrop_source;
uniform vec4 u_backdrop_bounds;
uniform int u_has_backdrop;
uniform float u_backdrop_opacity;
uniform int u_edge;
uniform vec2 u_islands[3];
uniform vec4 u_fill;
uniform vec4 u_border;
uniform float u_border_width;
uniform int u_band_count;
uniform vec4 u_band_shape[32]; // edge, outer (-1 attached, -2 attached + chamfer), depth, radius
uniform vec4 u_band_meta[32]; // join, border width, third span
uniform vec4 u_band_spans[32]; // first two spans
uniform vec4 u_band_fill[32];
uniform vec4 u_band_border[32];
uniform vec4 u_band_neck[32]; // neck span, neck depth, compound enabled
uniform vec2 u_band_chamfer[32]; // upper join smoothing, lower radius
varying vec2 v_local;
float roundedDistance(vec2 p, vec2 halfSize, float r) {
  r = min(r, min(halfSize.x, halfSize.y));
  vec2 q = abs(p) - halfSize + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float roundedQuadDistance(vec2 p, vec2 vertices[4], float radius) {
  vec2 inset[4];
  for (int i = 0; i < 4; ++i) {
    int prev = i == 0 ? 3 : i - 1;
    int next = i == 3 ? 0 : i + 1;
    vec2 vertex = vertices[i];
    vec2 edgePrev = vertex - vertices[prev];
    vec2 edgeNext = vertices[next] - vertex;
    float lenPrev = length(edgePrev), lenNext = length(edgeNext);
    vec2 normalPrev = vec2(-edgePrev.y, edgePrev.x) / max(lenPrev, 1e-5);
    vec2 normalNext = vec2(-edgeNext.y, edgeNext.x) / max(lenNext, 1e-5);
    float scale = radius / max(1.0 + dot(normalPrev, normalNext), 1e-4);
    inset[i] = vertex + (normalPrev + normalNext) * scale;
  }
  bool inside = true;
  float nearestSquared = 1e20;
  for (int i = 0; i < 4; ++i) {
    int next = i == 3 ? 0 : i + 1;
    vec2 a = inset[i], edge = inset[next] - a;
    vec2 relative = p - a;
    inside = inside && edge.x * relative.y - edge.y * relative.x >= 0.0;
    float edgeLengthSquared = dot(edge, edge);
    float t = edgeLengthSquared > 0.0 ? clamp(dot(relative, edge) / edgeLengthSquared, 0.0, 1.0) : 0.0;
    vec2 delta = relative - t * edge;
    nearestSquared = min(nearestSquared, dot(delta, delta));
  }
  float distanceToInset = sqrt(nearestSquared);
  return (inside ? -distanceToInset : distanceToInset) - radius;
}
float chamferedBandDistance(vec2 p, vec2 span, float depth, float rail, float radius,
                            bool chamferStart, bool chamferEnd) {
  float outer = -2.0 * depth;
  float extent = depth - outer;
  vec2 vertices[4];
  vertices[0] = vec2(span.x - (chamferStart ? extent : 0.0), outer);
  vertices[1] = vec2(span.y + (chamferEnd ? extent : 0.0), outer);
  vertices[2] = vec2(span.y, depth);
  vertices[3] = vec2(span.x, depth);
  float safeRadius = min(radius, min((depth - rail) * 0.7, (span.y - span.x) * 0.45));
  return roundedQuadDistance(p, vertices, safeRadius);
}
void main() {
  vec2 p = v_local + u_offset;
  if (u_exclusion.z > 0.0 && p.x >= u_exclusion.x && p.y >= u_exclusion.y
      && p.x < u_exclusion.x + u_exclusion.z && p.y < u_exclusion.y + u_exclusion.w) discard;
  float d = u_frame.z < 0.0 ? 1e6 : -roundedDistance(p - u_frame.xy * 0.5, u_frame.xy * 0.5 - u_frame.z, u_frame.w);
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
  vec4 materialFill = u_fill;
  vec4 materialBorder = u_border;
  float materialWidth = u_border_width;
  for (int b = 0; b < 32; ++b) {
    if (b >= u_band_count) break;
    vec4 shape = u_band_shape[b];
    vec4 meta = u_band_meta[b];
    vec2 buv = p;
    bool side = shape.x == 1.0 || shape.x == 3.0;
    if (shape.x == 1.0) buv = vec2(p.y, u_frame.x - p.x);
    else if (shape.x == 2.0) buv.y = u_frame.y - p.y;
    else if (shape.x == 3.0) buv = p.yx;
    float outer = shape.y < 0.0 ? -(side ? u_frame.x : u_frame.y) : shape.y;
    float chamferGap = max(0.0, shape.z - u_frame.z);
    float edgeLength = side ? u_frame.y : u_frame.x;
    for (int j = 0; j < 3; ++j) {
      vec2 span = j == 0 ? u_band_spans[b].xy : j == 1 ? u_band_spans[b].zw : meta.zw;
      if (span.y <= span.x) continue;
      float bd = roundedDistance(buv - vec2((span.x + span.y) * 0.5, (shape.z + outer) * 0.5),
          vec2((span.y - span.x) * 0.5, (shape.z - outer) * 0.5), shape.w);
      bool chamferEnabled = shape.y < -1.5;
      bool chamferStart = chamferEnabled && shape.y < 0.0 && chamferGap > 0.0
          && span.x > chamferGap && span.x < edgeLength;
      bool chamferEnd = chamferEnabled && shape.y < 0.0 && chamferGap > 0.0
          && span.y > 0.0 && span.y + chamferGap < edgeLength;
      bool chamfered = chamferStart || chamferEnd;
      if (chamfered)
        bd = chamferedBandDistance(buv, span, shape.z, u_frame.z, u_band_chamfer[b].y,
                                   chamferStart, chamferEnd);
      vec4 neck = u_band_neck[b];
      if (neck.w > 0.0) {
        float nd = roundedDistance(buv - vec2((neck.x + neck.y) * 0.5, (neck.z + outer) * 0.5),
            vec2((neck.y - neck.x) * 0.5, (neck.z - outer) * 0.5), shape.w);
        float nh = meta.x > 0.0 ? max(meta.x - abs(bd - nd), 0.0) / meta.x : 0.0;
        bd = min(bd, nd) - nh * nh * meta.x * 0.25;
        vec2 bounds = vec2(min(span.x, neck.x), max(span.y, neck.y));
        float depth = max(shape.z, neck.z);
        bd = max(bd, roundedDistance(buv - vec2((bounds.x + bounds.y) * 0.5, (depth + outer) * 0.5),
            vec2((bounds.y - bounds.x) * 0.5, (depth - outer) * 0.5), 0.0));
      }
      float join = chamfered ? min(min(meta.x, 12.0) * u_band_chamfer[b].x / 12.0, chamferGap) : meta.x;
      float blend = join > 0.0 ? clamp(0.5 + (d - bd) / (2.0 * join), 0.0, 1.0) : (bd < d ? 1.0 : 0.0);
      float h = join > 0.0 ? max(join - abs(d - bd), 0.0) / join : 0.0;
      d = min(d, bd) - h * h * join * 0.25;
      materialFill = mix(materialFill, u_band_fill[b], blend);
      materialBorder = mix(materialBorder, u_band_border[b], blend);
      materialWidth = mix(materialWidth, meta.y, blend);
    }
  }
  float aa = max(length(vec2(dFdx(d), dFdy(d))), 1e-6);
  float coverage = clamp(0.5 - d / aa, 0.0, 1.0);
  if (coverage <= 0.0) discard;
  float stroke = materialWidth > 0.0 ? clamp(0.5 + (d + materialWidth) / aa, 0.0, 1.0) : 0.0;
  vec4 fill = vec4(materialFill.rgb * materialFill.a, materialFill.a);
  if (u_has_backdrop != 0 && p.x >= u_backdrop_bounds.x && p.y >= u_backdrop_bounds.y
      && p.x < u_backdrop_bounds.x + u_backdrop_bounds.z && p.y < u_backdrop_bounds.y + u_backdrop_bounds.w) {
    vec2 uv = clamp((p - u_backdrop_source.xy) / u_backdrop_source.zw, vec2(0.0), vec2(1.0));
    vec3 captured = texture2D(u_backdrop, uv).rgb;
    fill = vec4(mix(captured, materialFill.rgb, materialFill.a) * u_backdrop_opacity, u_backdrop_opacity);
  }
  vec4 border = vec4(materialBorder.rgb * materialBorder.a, materialBorder.a);
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
  m_exclusionLocation = glGetUniformLocation(m_program.id(), "u_exclusion");
  m_backdropLocation = glGetUniformLocation(m_program.id(), "u_backdrop");
  m_backdropSourceLocation = glGetUniformLocation(m_program.id(), "u_backdrop_source");
  m_backdropBoundsLocation = glGetUniformLocation(m_program.id(), "u_backdrop_bounds");
  m_hasBackdropLocation = glGetUniformLocation(m_program.id(), "u_has_backdrop");
  m_backdropOpacityLocation = glGetUniformLocation(m_program.id(), "u_backdrop_opacity");
  m_edgeLocation = glGetUniformLocation(m_program.id(), "u_edge");
  m_islandsLocation = glGetUniformLocation(m_program.id(), "u_islands[0]");
  m_bandCountLocation = glGetUniformLocation(m_program.id(), "u_band_count");
  m_bandShapeLocation = glGetUniformLocation(m_program.id(), "u_band_shape[0]");
  m_bandMetaLocation = glGetUniformLocation(m_program.id(), "u_band_meta[0]");
  m_bandSpansLocation = glGetUniformLocation(m_program.id(), "u_band_spans[0]");
  m_bandFillLocation = glGetUniformLocation(m_program.id(), "u_band_fill[0]");
  m_bandBorderLocation = glGetUniformLocation(m_program.id(), "u_band_border[0]");
  m_bandNeckLocation = glGetUniformLocation(m_program.id(), "u_band_neck[0]");
  m_bandChamferLocation = glGetUniformLocation(m_program.id(), "u_band_chamfer[0]");
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
      || m_exclusionLocation < 0
      || m_edgeLocation < 0
      || m_islandsLocation < 0
      || m_bandCountLocation < 0
      || m_bandShapeLocation < 0
      || m_bandMetaLocation < 0
      || m_bandSpansLocation < 0
      || m_bandFillLocation < 0
      || m_bandBorderLocation < 0 || m_bandNeckLocation < 0 || m_bandChamferLocation < 0
      || m_backdropLocation < 0 || m_backdropSourceLocation < 0 || m_backdropBoundsLocation < 0
      || m_hasBackdropLocation < 0 || m_backdropOpacityLocation < 0)
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
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, style.backdrop.texture.value());
  glUniform1i(m_backdropLocation, 0);
  glUniform1i(m_hasBackdropLocation, style.backdrop.texture.valid() ? 1 : 0);
  glUniform1f(m_backdropOpacityLocation, style.backdrop.opacity);
  glUniform4fv(m_backdropSourceLocation, 1, style.backdrop.source.data());
  glUniform4fv(m_backdropBoundsLocation, 1, style.backdrop.bounds.data());
  glUniform2f(m_surfaceSizeLocation, sw, sh);
  glUniform2f(m_sizeLocation, width, height);
  glUniformMatrix3fv(m_transformLocation, 1, GL_FALSE, transform.m.data());
  glUniform4f(m_fillLocation, style.fill.r, style.fill.g, style.fill.b, style.fill.a);
  glUniform4f(m_borderLocation, style.border.r, style.border.g, style.border.b, style.border.a);
  glUniform1f(m_borderWidthLocation, style.borderWidth);
  glUniform4f(m_frameLocation, g.width, g.height, g.rail, g.radius);
  glUniform3f(m_shapeLocation, g.depth, g.islandRadius, g.join);
  glUniform2f(m_offsetLocation, style.offsetX, style.offsetY);
  glUniform4fv(m_exclusionLocation, 1, style.exclusion.data());
  glUniform1i(m_edgeLocation, g.edge);
  glUniform2fv(m_islandsLocation, 3, spans.data());
  if (g.bands.size() > kMaxScreenFrameBands)
    throw std::runtime_error("shared screen frame exceeds 32 bar tracks on one output");
  std::array<GLfloat, kMaxScreenFrameBands * 4> shapes{}, meta{}, bandSpans{}, fills{}, borders{}, necks{};
  std::array<GLfloat, kMaxScreenFrameBands * 2> chamfers{};
  for (std::size_t i = 0; i < g.bands.size(); ++i) {
    const auto& b = g.bands[i];
    const auto o = i * 4;
    chamfers[i * 2] = b.chamferUpper;
    chamfers[i * 2 + 1] = b.chamferLower;
    necks[o] = b.neckSpan.begin;
    necks[o + 1] = b.neckSpan.end;
    necks[o + 2] = b.neckDepth;
    necks[o + 3] = b.neckSpan.end > b.neckSpan.begin ? 1.0F : 0.0F;
    shapes[o] = static_cast<float>(b.edge);
    shapes[o + 1] = b.edgeAttached ? (b.softChamfer ? -2.0F : -1.0F) : b.outer;
    shapes[o + 2] = b.depth;
    shapes[o + 3] = b.radius;
    meta[o] = b.join;
    meta[o + 1] = b.borderWidth;
    meta[o + 2] = b.spans[2].begin;
    meta[o + 3] = b.spans[2].end;
    bandSpans[o] = b.spans[0].begin;
    bandSpans[o + 1] = b.spans[0].end;
    bandSpans[o + 2] = b.spans[1].begin;
    bandSpans[o + 3] = b.spans[1].end;
    fills[o] = b.fill.r;
    fills[o + 1] = b.fill.g;
    fills[o + 2] = b.fill.b;
    fills[o + 3] = b.fill.a;
    borders[o] = b.border.r;
    borders[o + 1] = b.border.g;
    borders[o + 2] = b.border.b;
    borders[o + 3] = b.border.a;
  }
  const auto count = static_cast<GLsizei>(g.bands.size());
  glUniform1i(m_bandCountLocation, count);
  if (count > 0) {
    glUniform2fv(m_bandChamferLocation, count, chamfers.data());
    glUniform4fv(m_bandNeckLocation, count, necks.data());
    glUniform4fv(m_bandShapeLocation, count, shapes.data());
    glUniform4fv(m_bandMetaLocation, count, meta.data());
    glUniform4fv(m_bandSpansLocation, count, bandSpans.data());
    glUniform4fv(m_bandFillLocation, count, fills.data());
    glUniform4fv(m_bandBorderLocation, count, borders.data());
  }
  const auto attr = static_cast<GLuint>(m_positionLocation);
  glVertexAttribPointer(attr, 2, GL_FLOAT, GL_FALSE, 0, vertices.data());
  glEnableVertexAttribArray(attr);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisableVertexAttribArray(attr);
}
