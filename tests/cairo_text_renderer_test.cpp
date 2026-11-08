#include "core/files/resource_paths.h"
#include "render/text/cairo_text_renderer.h"
#include "render/text/font_registry.h"

#include <array>
#include <cmath>
#include <print>
#include <string_view>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fontconfig.h>

namespace {

  bool sameMetrics(const CairoTextRenderer::TextMetrics& lhs, const CairoTextRenderer::TextMetrics& rhs) {
    constexpr float epsilon = 0.0001F;
    return std::abs(lhs.width - rhs.width) < epsilon
        && std::abs(lhs.top - rhs.top) < epsilon
        && std::abs(lhs.bottom - rhs.bottom) < epsilon
        && lhs.lineCount == rhs.lineCount;
  }

  // Wrapping-boundary cache correctness: a cached measurement at one width must
  // not reuse metrics computed for a different width.
  int wrappingBoundaryCase(CairoTextRenderer& renderer) {
    constexpr std::string_view text =
        "Error: Error while parsing table header: expected a comment or whitespace, saw 't'";
    constexpr float fontSize = 14.0F;
    const float naturalWidth = renderer.measure(1.0F, text, fontSize).width;
    constexpr std::array deltas{0.0005F, 0.001F, 0.002F, 0.003F, 0.004F, 0.006F};

    for (const float delta : deltas) {
      renderer.notifyFontConfigChanged();
      const auto above = renderer.measure(1.0F, text, fontSize, {FontWeight::Normal}, naturalWidth + delta, 3);
      renderer.notifyFontConfigChanged();
      const auto expectedBelow = renderer.measure(1.0F, text, fontSize, {FontWeight::Normal}, naturalWidth - delta, 3);
      if (above.lineCount == expectedBelow.lineCount) {
        continue;
      }

      renderer.notifyFontConfigChanged();
      [[maybe_unused]] const auto cachedAbove =
          renderer.measure(1.0F, text, fontSize, {FontWeight::Normal}, naturalWidth + delta, 3);
      const auto cachedBelow = renderer.measure(1.0F, text, fontSize, {FontWeight::Normal}, naturalWidth - delta, 3);
      if (cachedBelow.lineCount != expectedBelow.lineCount) {
        std::println(
            stderr,
            "cairo_text_renderer_test: FAIL: cached measurement at width {} reused metrics from width {} "
            "(expected {} lines, got {})",
            naturalWidth - delta, naturalWidth + delta, expectedBelow.lineCount, cachedBelow.lineCount
        );
        return 1;
      }
      return 0;
    }

    std::println(stderr, "cairo_text_renderer_test: FAIL: could not establish a wrapping boundary");
    return 1;
  }

  // The metrics cache is keyed by content scale: interleaving 1.0x and 1.25x
  // measurements of the same text must return per-scale-repeatable results, and
  // an intervening call at the other scale must not corrupt the cached result.
  int scaleKeyedCacheCase(CairoTextRenderer& renderer) {
    constexpr std::string_view text = "The quick brown fox";
    constexpr float fontSize = 16.0F;

    const auto oneA = renderer.measure(1.0F, text, fontSize);
    const auto hiA = renderer.measure(1.25F, text, fontSize);
    const auto oneB = renderer.measure(1.0F, text, fontSize); // cache hit at 1.0x
    const auto hiB = renderer.measure(1.25F, text, fontSize); // cache hit at 1.25x

    if (!sameMetrics(oneA, oneB)) {
      std::println(
          stderr,
          "cairo_text_renderer_test: FAIL: 1.0x measurement not repeatable across a 1.25x call "
          "(width {} vs {}, lines {} vs {})",
          oneA.width, oneB.width, oneA.lineCount, oneB.lineCount
      );
      return 1;
    }
    if (!sameMetrics(hiA, hiB)) {
      std::println(
          stderr,
          "cairo_text_renderer_test: FAIL: 1.25x measurement not repeatable across a 1.0x call "
          "(width {} vs {}, lines {} vs {})",
          hiA.width, hiB.width, hiA.lineCount, hiB.lineCount
      );
      return 1;
    }
    return 0;
  }

} // namespace

int main() {
  const std::string wordmarkFamily = text::registerFontFile(paths::assetPath("fonts/NytheraDisplay-Regular.ttf"));
  CairoTextRenderer renderer;
  renderer.initialize(nullptr, nullptr);
  renderer.notifyFontConfigChanged();
  const auto wordmark = renderer.measure(1.0F, "NYTHERA", 25.0F, TextFont{.family = wordmarkFamily});
  FcPattern* query = FcPatternCreate();
  FcPatternAddString(query, FC_FAMILY, reinterpret_cast<const FcChar8*>(wordmarkFamily.c_str()));
  FcResult matchResult = FcResultNoMatch;
  FcPattern* match = FcFontMatch(FcConfigGetCurrent(), query, &matchResult);
  FcChar8* matchedFile = nullptr;
  if (match != nullptr) {
    FcPatternGetString(match, FC_FILE, 0, &matchedFile);
  }
  std::println("Fontconfig matched file '{}'", matchedFile ? reinterpret_cast<const char*>(matchedFile) : "(none)");
  if (match != nullptr) {
    FcPatternDestroy(match);
  }
  FcPatternDestroy(query);
  PangoFontMap* map = pango_cairo_font_map_new();
  pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(map), FcConfigGetCurrent());
  pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(map));
  FcConfig* const pangoConfig = pango_fc_font_map_get_config(PANGO_FC_FONT_MAP(map));
  FcPattern* pangoQuery = FcPatternCreate();
  FcPatternAddString(pangoQuery, FC_FAMILY, reinterpret_cast<const FcChar8*>(wordmarkFamily.c_str()));
  FcResult pangoMatchResult = FcResultNoMatch;
  FcPattern* pangoMatch = FcFontMatch(pangoConfig, pangoQuery, &pangoMatchResult);
  FcChar8* pangoMatchedFile = nullptr;
  if (pangoMatch != nullptr) FcPatternGetString(pangoMatch, FC_FILE, 0, &pangoMatchedFile);
  std::println("Pango map Fontconfig file '{}'", pangoMatchedFile ? reinterpret_cast<const char*>(pangoMatchedFile) : "(none)");
  if (pangoMatch != nullptr) FcPatternDestroy(pangoMatch);
  FcPatternDestroy(pangoQuery);
  PangoContext* context = pango_font_map_create_context(map);
  PangoLayout* layout = pango_layout_new(context);
  PangoFontDescription* description = pango_font_description_new();
  pango_font_description_set_family(description, wordmarkFamily.c_str());
  pango_font_description_set_absolute_size(description, 25.0 * PANGO_SCALE);
  pango_layout_set_font_description(layout, description);
  pango_layout_set_text(layout, "NYTHERA", -1);
  const auto* line = static_cast<PangoLayoutLine*>(pango_layout_get_lines_readonly(layout)->data);
  const auto* run = static_cast<PangoGlyphItem*>(line->runs->data);
  PangoFontDescription* resolved = pango_font_describe(run->item->analysis.font);
  const std::string resolvedFamily = pango_font_description_get_family(resolved);
  std::println("Pango requested '{}' resolved '{}'", wordmarkFamily, resolvedFamily);
  pango_font_description_free(resolved);
  pango_font_description_free(description);
  g_object_unref(layout);
  g_object_unref(context);
  g_object_unref(map);
  if (wordmarkFamily.empty() || resolvedFamily != wordmarkFamily || wordmark.width < 135.0F) {
    std::println(stderr, "cairo_text_renderer_test: FAIL: registered Nythera font fell back (requested '{}', resolved '{}', width {})",
                 wordmarkFamily, resolvedFamily, wordmark.width);
    return 1;
  }

  if (const int rc = scaleKeyedCacheCase(renderer); rc != 0) {
    return rc;
  }
  return wrappingBoundaryCase(renderer);
}
