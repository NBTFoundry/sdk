// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_CHILD_HPP
#define NBT_APP_CHILD_HPP

#include <stddef.h>
#include <sys/types.h>

namespace nbt {
namespace app {

/* Programs an application starts: the head unit's own tools, or a companion
 * program carried inside the application's executable (nbt/app/payload.h). */

/* The most descriptors spawn_child() maps. */
const unsigned kChildFdsMax = 8;

/* Starts `path`. The child's descriptor i is the caller's fds[i] for
 * i < count (-1 leaves it closed; count is at least 1) and it inherits no
 * other descriptor. It starts with default signal actions, an empty signal
 * mask and, with `own_group`, in a process group of its own, so that
 * stop_child() also reaches the processes it starts. QNX: spawn() (QNX
 * 6.5's fork() fails in a process with threads); elsewhere fork() and
 * execve(). Returns 0 and sets *pid, or returns an errno value (also when the
 * program cannot be executed). */
int spawn_child(const char* path, char* const argv[], char* const envp[], const int* fds,
                unsigned count, bool own_group, pid_t* pid);

/* Fills `out` (at most max_out entries, the last one NULL) with `first`
 * (NULL-terminated, may be NULL), then `src` without LD_PRELOAD (a library
 * iDrive preloads must not reach a child) and without the variables `first`
 * sets. Returns the number of entries, or -1 when they do not fit. */
int child_environment(char* const* src, char* const* first, char* out[], size_t max_out);

/* Whether `pid` has exited, reaping it (*status, when not NULL, gets the
 * wait status). */
bool child_exited(pid_t pid, int* status);

/* Gives `pid` up to `wait_ms` to exit, then sends SIGTERM (to its process
 * group with `group`) and gives it up to `term_ms`, then SIGKILL. Returns its
 * wait status, or -1 when it could not be reaped. */
int stop_child(pid_t pid, bool group, unsigned wait_ms, unsigned term_ms);

/* This executable's absolute path. */
bool own_executable(char* out, size_t size);

/* Copies the payload appended to this executable to <dir>/<name> (mode
 * 0700), writing its path to `out`. 1 when copied; 0 when the executable
 * carries none; -1 on an error, with `why` set. */
int extract_own_payload(const char* dir, const char* name, char* out, size_t size,
                        const char** why);

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_CHILD_HPP */
