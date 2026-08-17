#pragma once

#include <memory>
#include <string>

#include "KeyboardEntryActivity.h"
#include "MappedInputManager.h"
#include "T4EntryActivity.h"

// Factory for text-entry activities.
//
// The keyboard is chosen by input style, not by call site: touch devices
// (Seeed Sticky, X4 Pro, …) get the FreeInkUI touch keyboard
// (KeyboardEntryActivity); button-only devices (Xteink X3/X4) get the
// button-driven T4 keyboard (T4EntryActivity). Both record their result as
// a KeyboardResult, so every call site handles the returned activity the
// same way regardless of the actual type.
namespace textentry {

inline std::unique_ptr<Activity> makeEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   std::string title, std::string initialText = "",
                                                   size_t maxLength = 0, InputType inputType = InputType::Text) {
  if (mappedInput.hasTouch()) {
    return std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::move(title), std::move(initialText),
                                                   maxLength, inputType);
  }
  return std::make_unique<T4EntryActivity>(renderer, mappedInput, std::move(title), std::move(initialText), maxLength,
                                           inputType);
}

}  // namespace textentry
