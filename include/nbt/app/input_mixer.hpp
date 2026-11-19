#ifndef NBT_APP_INPUT_MIXER_HPP
#define NBT_APP_INPUT_MIXER_HPP

#include <stddef.h>

namespace nbt {
namespace app {

/* One input event: a key going down or up, or an application-defined action
 * (types from kAppAction up), with a value. */
struct InputAction {
  enum { kKeyDown = 0, kKeyUp = 1, kAppAction = 16 };
  int type;
  int value;
};

class InputSink {
 public:
  virtual ~InputSink() {}
  virtual void on_action(const InputAction& action) = 0;
};

/* Combines several input sources (the iDrive controller, a gamepad, a
 * keyboard...) into one stream. A key is down while any source holds it, so
 * one source's release never cancels a key another source still holds.
 * Application actions pass through unchanged. */
class InputMixer {
 public:
  enum { kMaxSources = 4, kMaxKeys = 512 };

  InputMixer();
  void set_sink(InputSink* sink) { sink_ = sink; }
  void action(unsigned source, const InputAction& action);
  /* Releases every key `source` holds. */
  void release(unsigned source);
  void release_all();

 private:
  bool held_[kMaxSources][kMaxKeys];
  InputSink* sink_;
};

/* An InputSink that feeds one source of an InputMixer. */
class SourceSink : public InputSink {
 public:
  SourceSink(InputMixer& mixer, unsigned source) : mixer_(mixer), source_(source) {}
  void on_action(const InputAction& action) { mixer_.action(source_, action); }

 private:
  InputMixer& mixer_;
  unsigned source_;
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_INPUT_MIXER_HPP */
