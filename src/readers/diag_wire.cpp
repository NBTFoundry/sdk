// SPDX-License-Identifier: MPL-2.0
// Copied verbatim into NBTFoundry/sdk (make check-readers there).
#include "nbt/diag_wire.h"

#include <string.h>

int nbt_diag_request_encode(uint8_t ecu, uint16_t id, uint16_t timeout_ms, const uint8_t *uds,
                            size_t size, uint8_t *out, size_t capacity) {
    if (!uds || !out || !size || size > NBT_DIAG_WIRE_UDS_MAX ||
        timeout_ms > NBT_DIAG_WIRE_TIMEOUT_MAX || capacity < NBT_DIAG_WIRE_HEADER + size) {
        return -1;
    }
    out[0] = 0x01;
    out[1] = ecu;
    out[2] = static_cast<uint8_t>(id);
    out[3] = static_cast<uint8_t>(id >> 8);
    out[4] = static_cast<uint8_t>(timeout_ms);
    out[5] = static_cast<uint8_t>(timeout_ms >> 8);
    memcpy(out + NBT_DIAG_WIRE_HEADER, uds, size);
    return static_cast<int>(NBT_DIAG_WIRE_HEADER + size);
}

int nbt_diag_request_decode(const uint8_t *data, size_t size, nbt_diag_request *out) {
    if (!data || !out || size <= NBT_DIAG_WIRE_HEADER ||
        size > NBT_DIAG_WIRE_HEADER + NBT_DIAG_WIRE_UDS_MAX || data[0] != 0x01) {
        return -1;
    }
    const uint16_t timeout = static_cast<uint16_t>(data[4] | data[5] << 8);
    if (timeout > NBT_DIAG_WIRE_TIMEOUT_MAX) {
        return -1;
    }
    out->ecu = data[1];
    out->id = static_cast<uint16_t>(data[2] | data[3] << 8);
    out->timeout_ms = timeout ? timeout : static_cast<uint16_t>(NBT_DIAG_WIRE_TIMEOUT_DEFAULT);
    out->uds = data + NBT_DIAG_WIRE_HEADER;
    out->size = size - NBT_DIAG_WIRE_HEADER;
    return 0;
}

int nbt_diag_reply_encode(uint8_t ecu, uint16_t id, uint8_t status, uint8_t transport,
                          const uint8_t *uds, size_t size, uint8_t *out, size_t capacity) {
    if (!out || status > NBT_DIAG_MALFORMED || transport > NBT_DIAG_VIA_MOST ||
        (status == NBT_DIAG_OK ? !uds || !size || size > NBT_DIAG_WIRE_REPLY_MAX : size != 0) ||
        capacity < NBT_DIAG_WIRE_HEADER + size) {
        return -1;
    }
    out[0] = 0x81;
    out[1] = ecu;
    out[2] = static_cast<uint8_t>(id);
    out[3] = static_cast<uint8_t>(id >> 8);
    out[4] = status;
    out[5] = transport;
    if (size) {
        memcpy(out + NBT_DIAG_WIRE_HEADER, uds, size);
    }
    return static_cast<int>(NBT_DIAG_WIRE_HEADER + size);
}

int nbt_diag_reply_decode(const uint8_t *data, size_t size, nbt_diag_reply *out) {
    if (!data || !out || size < NBT_DIAG_WIRE_HEADER || size > NBT_DIAG_WIRE_MAX ||
        data[0] != 0x81 || data[4] > NBT_DIAG_MALFORMED || data[5] > NBT_DIAG_VIA_MOST ||
        (data[4] == NBT_DIAG_OK) != (size > NBT_DIAG_WIRE_HEADER)) {
        return -1;
    }
    out->ecu = data[1];
    out->id = static_cast<uint16_t>(data[2] | data[3] << 8);
    out->status = data[4];
    out->transport = data[5];
    out->uds = size > NBT_DIAG_WIRE_HEADER ? data + NBT_DIAG_WIRE_HEADER : NULL;
    out->size = size - NBT_DIAG_WIRE_HEADER;
    return 0;
}
