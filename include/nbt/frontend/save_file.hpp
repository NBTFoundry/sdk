// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FRONTEND_SAVE_FILE_HPP
#define NBT_FRONTEND_SAVE_FILE_HPP

#include <stddef.h>

namespace nbt {
namespace frontend {

/* Writes `path` whole or not at all: "<path>.next", synced, then renamed over
 * it (a power cut leaves the previous file). Private (0600). */
bool save_file_write(const char* path, const void* data, size_t size);
/* `path`'s contents in a malloc'd buffer (free() it); NULL when it is absent,
 * not a regular file, empty or larger than `max`. Links are not followed. */
void* save_file_read(const char* path, size_t max, size_t* size);
/* Reads exactly `size` bytes of `path` into `data`; false otherwise (data is
 * then unchanged). */
bool save_file_read_into(const char* path, void* data, size_t size);

}  // namespace frontend
}  // namespace nbt

#endif /* NBT_FRONTEND_SAVE_FILE_HPP */
