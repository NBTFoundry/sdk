// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_RETRO_HOST_HPP
#define NBT_RETRO_HOST_HPP

#include <stddef.h>

#include "nbt/frontend/pad_profiles.hpp"
#include "nbt/retro/core_options.hpp"

namespace nbt {
namespace retro {

/* An emulator: one libretro core, linked statically, run by the front end.
 * The application's main() fills this and calls run(). */
struct HostConfig {
  const char* tag;    /* log tag ("snes9x") */
  const char* title;  /* desktop window title */
  const char* system; /* shown in the game list ("Super Nintendo") */
  /* The files offered (lower case, no dot, NULL ended); NULL: the core's own
   * list (retro_system_info.valid_extensions). */
  const char* const* extensions;
  /* The controls, by pad family and for the iDrive controller and keyboard;
   * actions are RETRO_DEVICE_ID_JOYPAD_* of port 0. */
  const frontend::Profiles* profiles;
  /* Manager settings feeding core variables. */
  const VariableMap* variables;
  size_t variable_count;
  /* The display aspect (4:3 for most consoles); 0: the core's (its
   * geometry's aspect_ratio, as shown after a SET_ROTATION, else the
   * frame's own). */
  float display_aspect;
  /* The largest game file read into memory. */
  size_t max_file_bytes;
  /* The core's system folder (BIOS files): the files folder when true, so
   * users add them as they add games; the data folder otherwise. */
  bool system_in_files;
  /* For games that are folders (`|folders=` in the package: a CD image's cue
   * and bins): the extensions of the file to open in a game folder, the
   * preferred first (NULL ended); NULL: no game folders. */
  const char* const* folder_mains;
  /* Names the game list's entries, for games whose file names say little
   * (an arcade romset "mslug.zip"): called with the core initialised, for
   * each file or game folder `name`, it writes the entry's label (at most
   * size - 1 bytes) and returns true, or returns false to leave the entry
   * out (a BIOS set). The list is then in label order, and the label is the
   * game's name in the pause menu and the status. NULL: file names. */
  bool (*describe)(const char* name, char* label, size_t size);
};

/* The application: the bridge's lifecycle (a health check only reports
 * ready), the window, the sound, the controls, a game list from the files
 * folder (NBT_FILES_DIR), battery saves and save states in NBT_DATA_DIR, a
 * pause menu (with a disc row for a core with several discs) and the front
 * end's own settings:
 *
 *   aspect    4-3 | pixel | full      the frame's shape on the panel
 *   filter    sharp | smooth | nearest
 *   sound     on | off
 *   frameskip off | auto | 1 | 2  frames not drawn, for a core that asks
 *                                 (GET_AUDIO_VIDEO_ENABLE); auto while late
 *   show_fps  off | on
 *   debug_log off | on
 *
 * "--game NAME" starts that file of the files folder instead of showing the
 * list (tests, measurements). Returns main()'s exit status. */
int run(int argc, char** argv, const HostConfig& config);

}  // namespace retro
}  // namespace nbt

#endif /* NBT_RETRO_HOST_HPP */
