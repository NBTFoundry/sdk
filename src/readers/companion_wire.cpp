// SPDX-License-Identifier: MPL-2.0
#include "nbt/companion_wire.h"

#include <stdio.h>
#include <string.h>

namespace {

/* A canonical uint64 at text[*at], ending at `end` (not consumed). */
bool number(const char *text, size_t length, size_t *at, char end, uint64_t *value) {
    size_t digits = 0;
    uint64_t v = 0;
    while (*at < length && text[*at] >= '0' && text[*at] <= '9') {
        const unsigned d = static_cast<unsigned>(text[*at] - '0');
        if (digits == 20 || v > (UINT64_MAX - d) / 10u) {
            return false;
        }
        v = v * 10u + d;
        ++*at;
        ++digits;
    }
    if (!digits || (digits > 1 && text[*at - digits] == '0') || *at >= length || text[*at] != end) {
        return false;
    }
    *value = v;
    return true;
}

bool control(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7f;
}

} // namespace

int nbt_companion_page_check(const char *page, size_t length, uint64_t *sequence) {
    size_t at = 0;
    uint64_t value = 0;
    if (!page || !sequence || length < 2 || length > NBT_COMPANION_PAGE_MAX ||
        page[length - 1] != '\n' || !number(page, length, &at, '\n', &value)) {
        return -1;
    }
    unsigned widgets = 0;
    for (size_t i = at + 1; i < length; ++i) {
        if (page[i] == '\n') {
            // No empty line, and at most NBT_COMPANION_WIDGETS_MAX.
            if (page[i - 1] == '\n' || ++widgets > NBT_COMPANION_WIDGETS_MAX) {
                return -1;
            }
        } else if (page[i] != '\t' && control(page[i])) {
            return -1;
        }
    }
    *sequence = value;
    return 0;
}

int nbt_companion_id_valid(const char *id, size_t length) {
    if (!id || !length || length > NBT_COMPANION_ID_MAX) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

int nbt_companion_value_valid(const char *value, size_t length) {
    if (!value || length > NBT_COMPANION_VALUE_MAX) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (control(value[i]) || value[i] == ',') {
            return 0;
        }
    }
    return 1;
}

int nbt_companion_event_format(uint64_t sequence, const char *id, const char *value, char *out,
                               size_t size) {
    if (!id || !value || !out || !nbt_companion_id_valid(id, strlen(id)) ||
        !nbt_companion_value_valid(value, strlen(value))) {
        return -1;
    }
    const int n =
        snprintf(out, size, "%llu\t%s\t%s\n", static_cast<unsigned long long>(sequence), id, value);
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}

int nbt_companion_event_parse(const char *line, size_t length, uint64_t *sequence,
                              char id[NBT_COMPANION_ID_MAX + 1],
                              char value[NBT_COMPANION_VALUE_MAX + 1]) {
    size_t at = 0;
    uint64_t v = 0;
    if (!line || !sequence || !id || !value || length < 2 ||
        length > NBT_COMPANION_EVENT_LINE_MAX || line[length - 1] != '\n' ||
        !number(line, length, &at, '\t', &v)) {
        return -1;
    }
    const size_t idAt = at + 1;
    const char *tab = static_cast<const char *>(memchr(line + idAt, '\t', length - idAt));
    if (!tab) {
        return -1;
    }
    const size_t idLength = static_cast<size_t>(tab - (line + idAt));
    const size_t valueAt = idAt + idLength + 1;
    const size_t valueLength = length - 1 - valueAt;
    if (!nbt_companion_id_valid(line + idAt, idLength) ||
        !nbt_companion_value_valid(line + valueAt, valueLength)) {
        return -1;
    }
    memcpy(id, line + idAt, idLength);
    id[idLength] = 0;
    memcpy(value, line + valueAt, valueLength);
    value[valueLength] = 0;
    *sequence = v;
    return 0;
}
