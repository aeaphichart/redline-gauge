#pragma once
// Pure ELM327 reply parsing — no hardware, unit-tested in test/test_native.
// Replies are expected compacted: upper-case, spaces/CR/LF removed ("410C1AF8").
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ObdResult : uint8_t { OBD_OK, OBD_NODATA, OBD_BAD };

inline int obdHexByte(const char *p) {
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (!p[0] || !p[1]) return -1;
    int a = h(p[0]), b = h(p[1]);
    return (a < 0 || b < 0) ? -1 : a * 16 + b;
}

// Mode 01 PIDs used by the gauge: 0C rpm, 0D speed, 05 coolant, 0F intake.
// Several ECUs may answer; the first "41<pid>" wins.
inline ObdResult obdParsePid(const char *resp, uint8_t pid, float &out) {
    if (strstr(resp, "NODATA")) return OBD_NODATA;
    char tag[5];
    snprintf(tag, sizeof tag, "41%02X", pid);
    const char *p = strstr(resp, tag);
    if (!p) return OBD_BAD;
    int A = obdHexByte(p + 4);
    if (A < 0) return OBD_BAD;
    switch (pid) {
        case 0x0C: {
            int B = obdHexByte(p + 6);
            if (B < 0) return OBD_BAD;
            out = (A * 256 + B) / 4.0f;
            return OBD_OK;
        }
        case 0x0D: out = (float)A; return OBD_OK;
        case 0x05:
        case 0x0F: out = A - 40.0f; return OBD_OK;
        default:   return OBD_BAD;
    }
}

// ATRV reply, e.g. "13.9V". Accepts 5..20 V.
inline ObdResult obdParseVolt(const char *resp, float &out) {
    float v = (float)atof(resp);
    if (v <= 5 || v >= 20) return OBD_BAD;
    out = v;
    return OBD_OK;
}
