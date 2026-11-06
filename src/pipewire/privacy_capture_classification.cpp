#include "pipewire/privacy_capture_classification.h"

namespace nythera::pipewire {

  bool isPrivacyAudioCaptureConsumer(const PrivacyAudioConsumerProperties& properties) {
    return properties.mediaClass == "Stream/Input/Audio"
        && properties.linkGroup.empty()
        && !properties.nodePassive
        && !properties.streamCaptureSink;
  }

} // namespace nythera::pipewire
