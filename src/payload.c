/* SPDX-License-Identifier: MPL-2.0 */
#include "nbt/app/payload.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { kMaxPayload = 64 << 20, kNoteName = 0x0054424e /* "NBT\0" */ };

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(const char** why, const char* text) {
  if (why) *why = text;
  return -1;
}

/* ELF32 little-endian ARM only; every offset is checked against the size. */
int nbt_payload_parse(const uint8_t* d, size_t size, nbt_payload_info* out, const char** why) {
  if (size < 52 || memcmp(d, "\177ELF", 4) != 0) return fail(why, "not an ELF file");
  if (d[4] != 1 || d[5] != 1) return fail(why, "not 32-bit little-endian ELF");
  const uint16_t type = rd16(d + 16), machine = rd16(d + 18);
  if (machine != 40) return fail(why, "not ARM");
  const uint32_t phoff = rd32(d + 28);
  const uint16_t phentsize = rd16(d + 42), phnum = rd16(d + 44);
  if (phentsize < 32 || phnum == 0 || phnum > 64 || phoff > size ||
      (size_t)phnum * phentsize > size - phoff)
    return fail(why, "bad program headers");
  for (uint16_t i = 0; i < phnum; ++i) {
    const uint8_t* ph = d + phoff + (size_t)i * phentsize;
    if (rd32(ph) != 4 /* PT_NOTE */) continue;
    const uint32_t off = rd32(ph + 4), len = rd32(ph + 16);
    if (off > size || len > size - off) return fail(why, "bad note segment");
    uint32_t pos = 0;
    while (len - pos >= 12) {
      const uint8_t* n = d + off + pos;
      const uint32_t namesz = rd32(n), descsz = rd32(n + 4), ntype = rd32(n + 8);
      const uint32_t name_room = (namesz + 3u) & ~3u, desc_room = (descsz + 3u) & ~3u;
      if (namesz > 64 || descsz > 1024 || name_room + desc_room > len - pos - 12) break;
      if (namesz == 4 && rd32(n + 12) == kNoteName && ntype == 1 && descsz >= 12) {
        const uint8_t* desc = n + 12 + name_room;
        out->abi_major = rd32(desc);
        out->abi_minor = rd32(desc + 4);
        out->mode = rd32(desc + 8);
        if (out->mode == NBT_PAYLOAD_MODULE && type != 3 /* ET_DYN */)
          return fail(why, "module is not ET_DYN");
        if (out->mode == NBT_PAYLOAD_GUEST && type != 2 /* ET_EXEC */)
          return fail(why, "guest is not ET_EXEC");
        if (out->mode != NBT_PAYLOAD_MODULE && out->mode != NBT_PAYLOAD_GUEST)
          return fail(why, "unknown mode");
        return 0;
      }
      pos += 12 + name_room + desc_room;
    }
  }
  return fail(why, "no NBT ABI note");
}

int nbt_payload_read(const char* path, nbt_payload_info* out, const char** why) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return fail(why, "cannot open");
  struct stat st;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 52 || st.st_size > kMaxPayload) {
    close(fd);
    return fail(why, "not a regular file of a sane size");
  }
  /* Notes sit near the start: read the headers and the first 64 KiB. */
  size_t want = st.st_size < 65536 ? (size_t)st.st_size : 65536;
  uint8_t* buf = (uint8_t*)malloc(want);
  if (!buf) {
    close(fd);
    return fail(why, "out of memory");
  }
  size_t got = 0;
  while (got < want) {
    ssize_t n = read(fd, buf + got, want - got);
    if (n <= 0) break;
    got += (size_t)n;
  }
  close(fd);
  int rc = nbt_payload_parse(buf, got, out, why);
  free(buf);
  return rc;
}

int nbt_payload_extract(const char* exe, const char* out_path, const char** why) {
  int fd = open(exe, O_RDONLY);
  if (fd < 0) return fail(why, "cannot open the executable");
  struct stat st;
  uint8_t trailer[NBT_PAYLOAD_TRAILER_SIZE];
  if (fstat(fd, &st) != 0 || st.st_size < NBT_PAYLOAD_TRAILER_SIZE ||
      lseek(fd, st.st_size - NBT_PAYLOAD_TRAILER_SIZE, SEEK_SET) < 0 ||
      read(fd, trailer, sizeof(trailer)) != (ssize_t)sizeof(trailer)) {
    close(fd);
    return fail(why, "cannot read the executable");
  }
  if (memcmp(trailer, NBT_PAYLOAD_TRAILER_MAGIC, 8) != 0) {
    close(fd);
    return 0;
  }
  const uint32_t lo = rd32(trailer + 8), hi = rd32(trailer + 12);
  if (hi != 0 || lo == 0 || lo > (uint32_t)kMaxPayload ||
      (off_t)lo > st.st_size - NBT_PAYLOAD_TRAILER_SIZE) {
    close(fd);
    return fail(why, "bad payload trailer");
  }
  const off_t start = st.st_size - NBT_PAYLOAD_TRAILER_SIZE - (off_t)lo;
  /* Written in place (the caller's path is private to this process: /dev/shmem
   * has no rename), then checked by size. */
  unlink(out_path);
  int out = open(out_path, O_WRONLY | O_CREAT | O_EXCL, 0700);
  if (out < 0) {
    close(fd);
    return fail(why, "cannot create the payload file");
  }
  if (lseek(fd, start, SEEK_SET) != start) {
    close(out);
    close(fd);
    unlink(out_path);
    return fail(why, "cannot read the payload");
  }
  uint8_t buf[16384];
  uint32_t left = lo;
  while (left > 0) {
    ssize_t n = read(fd, buf, left < sizeof(buf) ? left : sizeof(buf));
    if (n <= 0 || write(out, buf, (size_t)n) != n) break;
    left -= (uint32_t)n;
  }
  close(fd);
  if (close(out) != 0 || left != 0) {
    unlink(out_path);
    return fail(why, "cannot write the payload");
  }
  return 1;
}
