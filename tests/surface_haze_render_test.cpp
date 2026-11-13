#include "render/core/render_styles.h"
#include "render/core/screen_frame_geometry.h"
#include "render/programs/rect_program.h"
#include "render/programs/screen_frame_program.h"
#include "test_check.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

int main() {
  auto display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  EGLint major = 0, minor = 0;
  if (!eglInitialize(display, &major, &minor)) {
    std::puts("No headless EGL display; skipping GPU check");
    return 77;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attributes[] = {
      EGL_SURFACE_TYPE,
      EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_NONE
  };
  EGLConfig config;
  EGLint count = 0;
  TEST_CHECK(eglChooseConfig(display, attributes, &config, 1, &count) && count);
  const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
  const EGLint sa[] = {EGL_WIDTH, 128, EGL_HEIGHT, 96, EGL_NONE};
  auto surface = eglCreatePbufferSurface(display, config, sa);
  TEST_CHECK(eglMakeCurrent(display, surface, surface, context));
  {
    RectProgram rect;
    rect.ensureInitialized();
    ScreenFrameProgram frame;
    frame.ensureInitialized();
    glViewport(0, 0, 128, 96);
    glDisable(GL_BLEND);
    auto capture = [&](const RoundedRectStyle& style) {
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      rect.draw(128, 96, 128, 96, style);
      std::vector<unsigned char> pixels(128 * 96 * 4);
      glReadPixels(0, 0, 128, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      TEST_CHECK(glGetError() == GL_NO_ERROR);
      return pixels;
    };
    RoundedRectStyle style;
    style.fill = rgba(.025F, .028F, .03F, 1);
    style.radius = 18;
    style.border = rgba(0, 1, 0, 1);
    style.borderWidth = 2;
    style.haze = {.enabled = true};
    auto opaque = capture(style);
    TEST_CHECK(opaque[3] == 0 && opaque[(48 * 128 + 64) * 4 + 3] == 255);
    TEST_CHECK(capture(style) == opaque); // deterministic static grain
    style.haze.time = 60;
    TEST_CHECK(capture(style) != opaque);
    style.haze.time = 0;
    style.haze.seed = 31;
    TEST_CHECK(capture(style) != opaque); // independent formations
    style.haze.seed = 0;
    style.haze.transparent = true;
    style.fill.a = .55F;
    auto transparent = capture(style);
    TEST_CHECK(transparent[(48 * 128 + 64) * 4 + 3] < 255);
    TEST_CHECK(transparent[(48 * 128 + 1) * 4 + 1] > 230); // border is untouched
    glClearColor(.2F, .4F, .6F, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    rect.draw(128, 96, 128, 96, style);
    std::vector<unsigned char> composed(transparent.size());
    glReadPixels(0, 0, 128, 96, GL_RGBA, GL_UNSIGNED_BYTE, composed.data());
    for (std::size_t i = 0; i < transparent.size(); i += 4) {
      for (int channel = 0; channel < 3; ++channel) {
        const float expected = transparent[i + channel] + (channel + 1) * .2F * (255 - transparent[i + 3]);
        TEST_CHECK(std::abs(composed[i + channel] - expected) <= 2);
      }
      TEST_CHECK(composed[i + 3] == 255);
    }
    glDisable(GL_BLEND);
    // Native backing opacity/tint and parent opacity are separate from energy.
    // In particular, Solid launchers and 97% notification cards must not acquire
    // an arbitrary 80% cap when transparent haze is selected.
    auto checkMaterial = [](const auto& energy, const auto& pixels, float alpha, float opacity,
                            const std::array<float, 3>& tint, std::size_t i) {
      const float hazeAlpha = energy[i + 3] / 255.0F;
      const float expectedAlpha = (hazeAlpha + alpha * (1 - hazeAlpha)) * opacity * 255;
      TEST_CHECK(std::abs(pixels[i + 3] - expectedAlpha) <= 2);
      for (int c = 0; c < 3; ++c) {
        // Reference energy is rendered over zero backing; its colour must not
        // be multiplied by the backing alpha. Allow 8-bit dither/rounding error.
        const float expected = tint[c] * expectedAlpha + energy[i + c] * opacity;
        TEST_CHECK(std::abs(pixels[i + c] - expected) <= 3);
      }
    };
    {
      RoundedRectStyle material;
      material.haze = {.enabled = true, .transparent = true};
      material.fill = rgba(0, 0, 0, 0);
      const auto energy = capture(material);
      constexpr std::array<float, 3> tint{.12F, .24F, .32F};
      for (float alpha : {1.0F, .97F, .8F, .55F}) {
        for (float opacity : {1.0F, .45F}) {
          material.fill = rgba(tint[0], tint[1], tint[2], alpha * opacity);
          material.haze.opacity = opacity;
          const auto pixels = capture(material);
          for (int y = 16; y < 80; ++y)
            for (int x = 16; x < 112; ++x)
              checkMaterial(energy, pixels, alpha, opacity, tint, (y * 128 + x) * 4);
        }
      }
      // A captured backdrop is already resolved/tinted before haze composition.
      GLuint texture = 0;
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      const unsigned char texel[] = {51, 102, 153, 255};
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      material.fill = rgba(.1F, .2F, .3F, .4F);
      material.haze.opacity = 1;
      material.backdrop.texture = TextureId{texture};
      material.backdrop.source = {0, 0, 128, 96};
      material.backdrop.opacity = .97F;
      const auto pixels = capture(material);
      checkMaterial(energy, pixels, .97F, 1, {.16F, .32F, .48F}, (48 * 128 + 64) * 4);
      glDeleteTextures(1, &texture);
    }
    // Every existing corner shape uses precisely the same coverage as the base material.
    for (auto shape : {CornerShape::Convex, CornerShape::Concave, CornerShape::Chamfered}) {
      style.fill.a = 1;
      style.corners = {shape, shape, shape, shape};
      style.logicalInset = shape == CornerShape::Concave ? RectInsets{8, 8, 8, 8} : RectInsets{};
      style.haze.transparent = false;
      style.haze.enabled = false;
      auto base = capture(style);
      style.haze.enabled = true;
      auto haze = capture(style);
      for (std::size_t i = 3; i < base.size(); i += 4)
        TEST_CHECK(base[i] == haze[i]);
    }
    // A bar-owned tool material may change only inside its explicit panel-body bounds.
    ScreenFrameStyle tool;
    tool.contour = {
        .width = 128,
        .height = 96,
        .rail = 6,
        .radius = 12,
        .depth = 70,
        .islandRadius = 12,
        .join = 8,
        .edge = 0,
        .islands = {ScreenFrameSpan{20, 108}, {}, {}}
    };
    tool.fill = rgba(.1F, .12F, .14F, 1);
    tool.hazeBounds = {20, 20, 88, 50};
    auto captureFrame = [&] {
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      frame.draw(128, 96, 128, 96, tool);
      std::vector<unsigned char> pixels(128 * 96 * 4);
      glReadPixels(0, 0, 128, 96, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
      TEST_CHECK(glGetError() == GL_NO_ERROR);
      return pixels;
    };
    auto plain = captureFrame();
    tool.haze = {.enabled = true};
    auto hazy = captureFrame();
    bool changed = false;
    for (int y = 0; y < 96; ++y)
      for (int x = 0; x < 128; ++x) {
        const auto i = static_cast<std::size_t>((y * 128 + x) * 4);
        if (x < 20 || x >= 108 || (95 - y) < 20 || (95 - y) >= 70)
          for (int c = 0; c < 4; ++c)
            TEST_CHECK(plain[i + c] == hazy[i + c]);
        else if (plain[i] != hazy[i])
          changed = true;
      }
    TEST_CHECK(changed);
    tool.borderWidth = 0;
    tool.haze = {.enabled = true, .transparent = true};
    tool.fill = rgba(0, 0, 0, 0);
    const auto frameEnergy = captureFrame();
    for (float alpha : {1.0F, .97F, .55F}) {
      for (float opacity : {1.0F, .45F}) {
        tool.fill = rgba(.12F, .24F, .32F, alpha * opacity);
        tool.haze.opacity = opacity;
        const auto pixels = captureFrame();
        checkMaterial(frameEnergy, pixels, alpha, opacity, {.12F, .24F, .32F}, (48 * 128 + 64) * 4);
      }
    }
    rect.destroy();
    frame.destroy();
  }
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, surface);
  eglDestroyContext(display, context);
  eglTerminate(display);
}
