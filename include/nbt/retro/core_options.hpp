// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_RETRO_CORE_OPTIONS_HPP
#define NBT_RETRO_CORE_OPTIONS_HPP

#include <stddef.h>

#include "nbt/app/settings.hpp"

struct retro_variable; /* libretro.h */

namespace nbt {
namespace retro {

/* A core variable fed from an NBT Manager setting: when the application's
 * setting `setting` has one of the values listed in `values` (pairs of
 * setting value and core value, then NULL), the core sees that core value;
 * otherwise its own default. */
struct VariableMap {
  const char* setting;
  const char* variable;
  const char* const* values;
};

/* The variables a core declares (RETRO_ENVIRONMENT_SET_VARIABLES, values
 * "Description; first|second|...", the first the default) and the values it
 * reads (GET_VARIABLE), from the settings through the maps. Bounded and
 * allocation-free; a value the core did not list is never given to it. */
class CoreOptions {
 public:
  enum { kMaxVariables = 96, kKeyMax = 63, kValueMax = 63 };

  CoreOptions();
  void set_maps(const VariableMap* maps, size_t count);
  /* The core's declaration (a RETRO_ENVIRONMENT_SET_VARIABLES array, ended by
   * a NULL key). Replaces an earlier one. */
  void declare(const ::retro_variable* variables);
  /* The application's settings now; marks the values changed when they are. */
  void apply(const app::Settings& settings);
  /* The value the core reads, or NULL for a variable it did not declare. */
  const char* get(const char* key) const;
  /* Whether a value changed since the last call (GET_VARIABLE_UPDATE). */
  bool take_update();
  size_t count() const { return count_; }

 private:
  struct Entry {
    char key[kKeyMax + 1];
    char options[256]; /* the listed values, '|' separated */
    char value[kValueMax + 1];
  };
  bool listed(const Entry& e, const char* value) const;
  void choose(Entry& e) const;

  Entry entries_[kMaxVariables];
  size_t count_;
  const VariableMap* maps_;
  size_t map_count_;
  app::Settings settings_;
  bool updated_;
};

}  // namespace retro
}  // namespace nbt

#endif /* NBT_RETRO_CORE_OPTIONS_HPP */
