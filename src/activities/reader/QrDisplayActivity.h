#pragma once
#include <I18n.h>

#include <string>
#include <utility>

#include "activities/Activity.h"

class QrDisplayActivity final : public Activity {
 public:
  explicit QrDisplayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string textPayload,
                             std::string title = std::string())
      : Activity("QrDisplay", renderer, mappedInput), textPayload(std::move(textPayload)), title(std::move(title)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string textPayload;
  std::string title;
  // Becomes true once the release edge of the press that opened this screen
  // has been consumed; dismissal is only armed afterwards.
  bool armed = false;
};
