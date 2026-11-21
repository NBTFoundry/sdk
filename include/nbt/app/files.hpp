// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_FILES_HPP
#define NBT_APP_FILES_HPP

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace app {

/* A file of the application's files folder (capability `files`,
 * NBT_FILES_DIR): what the user sent from a phone or imported from a USB
 * stick through NBT Manager. The folder is the manager's; the application
 * only reads it. */
struct FileEntry {
  char name[97]; /* a file's name, or a game folder's */
  uint64_t size; /* the file's, or all of the folder's files' */
  char file[97]; /* in a game folder, the file to open; "" for a file */
};

/* Whether `name` is a name NBT Manager stores: 1-96 printable ASCII bytes
 * without / \ : * ? " < > |, not starting or ending with a dot or a space,
 * with an extension after its last dot. */
bool file_name_valid(const char* name);
/* Whether `name`'s extension, in any case, is one of `extensions` (a NULL
 * terminated list of lower-case extensions without the dot; NULL: any). */
bool file_extension_in(const char* name, const char* const* extensions);

/* The files of a folder, sorted by name without regard to case: regular
 * files only (links and others are left out), with valid names and, when
 * given, one of the extensions. With `folder_mains` (an application whose
 * games may be folders: `|folders=` in its package), each game folder (one
 * level, NBT Manager's) is one entry too: its name, the size of all its
 * files, and its main file, the one whose extension comes first in
 * `folder_mains` (by name among equals); a folder with none is left out.
 * Bounded: at most kMaxFiles. The object is large (about 104 KiB): keep it out
 * of small stacks. */
class FileList {
 public:
  enum { kMaxFiles = 512 };

  FileList();
  /* Lists `dir`; false when it cannot be read (the list is then empty). */
  bool load(const char* dir, const char* const* extensions, const char* const* folder_mains = NULL);
  size_t count() const { return count_; }
  const FileEntry& at(size_t i) const { return entries_[i]; }
  /* The index of `name`, or -1. */
  int find(const char* name) const;
  /* "<dir>/<name>" of entry i ("<dir>/<name>/<file>" for a game folder);
   * false when it does not fit. */
  bool path(size_t i, char* out, size_t size) const;
  /* Entry i's contents in a malloc'd buffer (free() it), read without
   * following a link; NULL when it is gone, changed or larger than `max`. */
  void* read(size_t i, size_t max, size_t* size) const;

 private:
  FileList(const FileList&);
  FileList& operator=(const FileList&);

  char dir_[512];
  size_t count_;
  FileEntry entries_[kMaxFiles];
};

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_FILES_HPP */
