#pragma once

#include <cstdint>
#include <cstdio>

/**
 * Lightweight ephemeral status-message overlay for header subtitles.
 *
 * Designed for embedded use: no heap, no std::function, no exceptions,
 * no framework dependencies. The clock is injected via a function
 * pointer so the class works on both real hardware (Arduino millis)
 * and the desktop simulator without header-ordering issues.
 *
 * Typical usage in an activity:
 *
 *   // --- Member ---
 *   StatusMessageOverlay _toast;
 *
 *   // --- onEnter ---
 *   _toast.setClock(&millis);
 *   _toast.setSubtitleProvider([](void* ctx, char* buf, size_t n) {
 *     formatMeshCoreSubtitle(*static_cast<MeshCoreClient*>(ctx), buf, n);
 *   }, &client);
 *
 *   // --- loop ---
 *   if (_toast.poll()) requestUpdate();
 *
 *   // --- render ---
 *   char sub[64];
 *   _toast.getSubtitle(sub, sizeof(sub));
 *   GUI.drawHeader(…, sub);
 *
 *   // --- anywhere ---
 *   _toast.show("Contact saved!", 3000);
 *   requestUpdate();
 *
 * When show() is called, getSubtitle() returns the message. After the
 * timeout (or after clear()), getSubtitle() falls back to the standard
 * subtitle provider automatically.
 */
class StatusMessageOverlay {
 public:
  /** Signature of the function that generates the standard subtitle.
   *  @param ctx  Opaque pointer set via setSubtitleProvider().
   *  @param buf  Output buffer (caller-owned, at least bufSize bytes).
   *  @param bufSize Size of the output buffer. */
  using SubtitleFn = void (*)(const void* ctx, char* buf, size_t bufSize);

  /** Monotonic millisecond clock — typically &millis.
   *  Returns unsigned long to match Arduino's millis() signature
   *  on all platforms (32-bit on ESP32, 64-bit on macOS). */
  using ClockFn = unsigned long (*)();

  /** Wire up the monotonic clock. Call once during setup.
   *  Must be called before show() or poll(). */
  void setClock(ClockFn fn) { _clockFn = fn; }

  /** Wire up the standard subtitle generator. Call once during setup.
   *  @param fn  Function that fills buf with the default subtitle.
   *  @param ctx Opaque pointer passed through to fn. */
  void setSubtitleProvider(SubtitleFn fn, const void* ctx) {
    _subtitleFn = fn;
    _subtitleCtx = ctx;
  }

  /** Show a temporary status message (a "toast").
   *  @param msg       Null-terminated text to display (copied).
   *  @param timeoutMs Auto-clear timeout in milliseconds.
   *                   Use 0 for a persistent message (stays until
   *                   clear() or another show() call). */
  void show(const char* msg, uint32_t timeoutMs = 5000) {
    snprintf(_message, sizeof(_message), "%s", msg);
    _until = timeoutMs > 0 ? static_cast<uint32_t>(_clockFn() + timeoutMs) : 0;
  }

  /** Fill buf with the active subtitle text.
   *  If a status message is active, returns that.
   *  Otherwise delegates to the standard subtitle provider.
   *  If no provider is set, returns an empty string. */
  void getSubtitle(char* buf, size_t bufSize) const {
    if (_message[0] != '\0') {
      snprintf(buf, bufSize, "%s", _message);
    } else if (_subtitleFn) {
      _subtitleFn(_subtitleCtx, buf, bufSize);
    } else {
      buf[0] = '\0';
    }
  }

  /** Must be called every frame (in loop()).
   *  @return true if the timeout expired and the message was
   *          automatically cleared — caller should requestUpdate(). */
  bool poll() {
    if (_until > 0 && _clockFn() > _until) {
      _message[0] = '\0';
      _until = 0;
      return true;
    }
    return false;
  }

  /** Immediately clear any active status message. */
  void clear() {
    _message[0] = '\0';
    _until = 0;
  }

  /** @return true if a status message is currently being displayed. */
  bool isActive() const { return _message[0] != '\0'; }

 private:
  char _message[64] = {};
  uint32_t _until = 0;
  SubtitleFn _subtitleFn = nullptr;
  const void* _subtitleCtx = nullptr;
  ClockFn _clockFn = nullptr;
};
