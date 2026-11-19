#ifndef NBT_APP_SETTINGS_HPP
#define NBT_APP_SETTINGS_HPP

#include <stddef.h>

namespace nbt {
namespace app {

enum {
  /* Distinct keys kept (the manager declares at most 12 per application). */
  kSettingsMax = 16,
  /* Key: 1..16 of [a-z0-9_], starting with a letter. */
  kSettingKeyMax = 16,
  /* Value (a choice): 1..12 of [a-z0-9_-]. */
  kSettingValueMax = 12,
  /* A settings file must be shorter than this (the manager's limit). */
  kSettingsFileMax = 372
};

struct Setting {
  char key[kSettingKeyMax + 1];
  char value[kSettingValueMax + 1];
};

bool setting_key_valid(const char* key);
bool setting_value_valid(const char* value);

/* The application's settings as key/value pairs: bounded, allocation-free and
 * copyable. Values are not checked against the application's choices; the
 * application decides which values it accepts. */
class Settings {
 public:
  Settings();

  void clear();
  size_t count() const { return count_; }
  const Setting& at(size_t index) const { return items_[index]; }

  /* The value of `key`, or NULL (or `fallback`) when absent. */
  const char* get(const char* key) const;
  const char* get(const char* key, const char* fallback) const;

  /* Adds or replaces one value. False if the key or value is invalid or the
   * set is full. */
  bool set(const char* key, const char* value);

  /* Copies every value of `other` over this set. */
  void merge(const Settings& other);

  /* Same keys with the same values, in any order. */
  bool equals(const Settings& other) const;

  /* Applies the manager's settings format: "NBTVALUES1\n", then one
   * "<key>=<value>\n" line per setting. All or nothing: one bad line rejects
   * the text and leaves this set unchanged. A repeated key keeps its last value. */
  bool parse_values(const char* text, size_t size);

 private:
  Setting items_[kSettingsMax];
  size_t count_;
};

/* Adds NBT_SETTING_<KEY>=<value> from the environment (key lower-cased),
 * skipping invalid entries. Returns the number added. */
size_t settings_load_env(Settings* out);

/* Reads NBT_SETTINGS_FILE's file the way the manager's own reader does: a
 * regular file (no symlink), owned by this user, not shared with group or
 * others, with one link, read to its end within kSettingsFileMax bytes. On
 * success its values are merged into `out`. On failure `out` is unchanged and
 * `reason` (when given) says why. */
bool settings_read_file(const char* path, Settings* out, const char** reason = NULL);

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_SETTINGS_HPP */
