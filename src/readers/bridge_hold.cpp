// SPDX-License-Identifier: MPL-2.0
#include "nbt/bridge_hold.h"

#include <stdio.h>
#include <string.h>

namespace {

const char *const kWords[] = {"", "hold", "peek", "release"};
const char *const kStates[] = {"", "held", "aside"};

/* A canonical uint64 at line[*at], then one space. */
bool number(const char *line, size_t length, size_t *at, uint64_t *value) {
    size_t digits = 0;
    uint64_t v = 0;
    while (*at < length && line[*at] >= '0' && line[*at] <= '9') {
        const unsigned d = static_cast<unsigned>(line[*at] - '0');
        if (digits == 20 || v > (UINT64_MAX - d) / 10u) {
            return false;
        }
        v = v * 10u + d;
        ++*at;
        ++digits;
    }
    if (!digits || (digits > 1 && line[*at - digits] == '0') || *at >= length || line[*at] != ' ') {
        return false;
    }
    ++*at;
    *value = v;
    return true;
}

/* The rest of the line is exactly one of `names` (index 1..count-1) and '\n'. */
int word(const char *line, size_t length, size_t at, const char *const *names, unsigned count) {
    for (unsigned i = 1; i < count; ++i) {
        const size_t n = strlen(names[i]);
        if (length - at == n + 1 && !memcmp(line + at, names[i], n) && line[length - 1] == '\n') {
            return static_cast<int>(i);
        }
    }
    return 0;
}

bool appValid(const char *app, size_t n) {
    if (!n || n > NBT_HOLD_APP_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = app[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

int fit(int n, size_t size) {
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}

} // namespace

int nbt_hold_request_parse(const char *line, size_t length, uint64_t *sequence,
                           nbt_hold_word *out) {
    size_t at = 0;
    uint64_t value = 0;
    if (!line || !sequence || !out || length > NBT_HOLD_LINE_MAX ||
        !number(line, length, &at, &value)) {
        return -1;
    }
    const int w = word(line, length, at, kWords, 4);
    if (!w) {
        return -1;
    }
    *sequence = value;
    *out = static_cast<nbt_hold_word>(w);
    return 0;
}

int nbt_hold_request_format(uint64_t sequence, nbt_hold_word w, char *out, size_t size) {
    if (!out || w < NBT_HOLD_HOLD || w > NBT_HOLD_RELEASE) {
        return -1;
    }
    return fit(
        snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(sequence), kWords[w]),
        size);
}

int nbt_hold_relay_parse(const char *line, size_t length, uint64_t *ms, uint64_t *sequence,
                         char app[NBT_HOLD_APP_MAX + 1], nbt_hold_word *out) {
    size_t at = 0;
    uint64_t value = 0, seq = 0;
    if (!line || !ms || !sequence || !app || !out || length > NBT_HOLD_LINE_MAX ||
        !number(line, length, &at, &value) || !number(line, length, &at, &seq)) {
        return -1;
    }
    const char *space = static_cast<const char *>(memchr(line + at, ' ', length - at));
    const size_t n = space ? static_cast<size_t>(space - (line + at)) : 0;
    if (!space || !appValid(line + at, n)) {
        return -1;
    }
    const int w = word(line, length, at + n + 1, kWords, 4);
    if (!w) {
        return -1;
    }
    memcpy(app, line + at, n);
    app[n] = 0;
    *ms = value;
    *sequence = seq;
    *out = static_cast<nbt_hold_word>(w);
    return 0;
}

int nbt_hold_relay_format(uint64_t ms, uint64_t sequence, const char *app, nbt_hold_word w,
                          char *out, size_t size) {
    if (!out || !app || !appValid(app, strlen(app)) || w < NBT_HOLD_HOLD || w > NBT_HOLD_RELEASE) {
        return -1;
    }
    return fit(snprintf(out, size, "%llu %llu %s %s\n", static_cast<unsigned long long>(ms),
                        static_cast<unsigned long long>(sequence), app, kWords[w]),
               size);
}

int nbt_hold_state_parse(const char *line, size_t length, uint64_t *sequence, nbt_hold_state *out) {
    size_t at = 0;
    uint64_t value = 0;
    if (!line || !sequence || !out || length > NBT_HOLD_LINE_MAX ||
        !number(line, length, &at, &value)) {
        return -1;
    }
    const int s = word(line, length, at, kStates, 3);
    if (!s) {
        return -1;
    }
    *sequence = value;
    *out = static_cast<nbt_hold_state>(s);
    return 0;
}

int nbt_hold_state_format(uint64_t sequence, nbt_hold_state s, char *out, size_t size) {
    if (!out || (s != NBT_HOLD_HELD && s != NBT_HOLD_ASIDE)) {
        return -1;
    }
    return fit(
        snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(sequence), kStates[s]),
        size);
}

int nbt_cds_value_parse(const char *line, size_t length, uint64_t *sequence,
                        char value[NBT_CDS_VALUE_MAX + 1]) {
    size_t at = 0;
    uint64_t seq = 0;
    if (!line || !sequence || !value || length > NBT_CDS_LINE_MAX || length < 2 ||
        line[length - 1] != '\n' || !number(line, length, &at, &seq)) {
        return -1;
    }
    const size_t n = length - 1 - at;
    if (n > NBT_CDS_VALUE_MAX) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (line[at + i] < 32 || line[at + i] > 126) {
            return -1;
        }
    }
    memcpy(value, line + at, n);
    value[n] = 0;
    *sequence = seq;
    return 0;
}

int nbt_cds_value_format(uint64_t sequence, const char *value, char *out, size_t size) {
    if (!value || !out) {
        return -1;
    }
    const size_t n = strlen(value);
    if (n > NBT_CDS_VALUE_MAX) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        if (value[i] < 32 || value[i] > 126) {
            return -1;
        }
    }
    return fit(snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(sequence), value),
               size);
}
