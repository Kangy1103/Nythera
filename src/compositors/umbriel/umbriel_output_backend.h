#pragma once

#include "compositors/output_backend.h"
#include "compositors/umbriel/umbriel_event_handler.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace compositors::umbriel {
  class UmbrielRuntime;
} // namespace compositors::umbriel

// Caches focused output and power changes from the persistent event stream.
// Power decisions use a fresh snapshot to include commands not yet dispatched in the stream.
// The focused workspace names the output even when that workspace is empty.
class UmbrielOutputBackend final : public compositors::umbriel::UmbrielEventHandler {
public:
  explicit UmbrielOutputBackend(compositors::umbriel::UmbrielRuntime& runtime);

  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
  [[nodiscard]] std::unordered_map<std::string, bool> outputPowerStates() const;
  void setOutputPowerChangeCallback(std::function<void()> callback);

  void handleEvent(std::string_view event, const nlohmann::json& data) override;
  void handleStreamReset() override;

private:
  std::optional<std::string> m_focusedOutput;
  std::unordered_map<std::string, bool> m_outputPowerStates;
  std::function<void()> m_outputPowerChangeCallback;
};

namespace compositors::umbriel {

  [[nodiscard]] bool setOutputPower(UmbrielRuntime& runtime, bool on);
  [[nodiscard]] std::optional<OutputWorkArea> parseOutputWorkArea(const nlohmann::json& data, std::string_view output);
  [[nodiscard]] std::optional<OutputWorkArea> outputWorkArea(const UmbrielRuntime& runtime, std::string_view output);

} // namespace compositors::umbriel
