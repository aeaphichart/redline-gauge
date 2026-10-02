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

// ---- adapter selection (Bluetooth scan) ---------------------------------------------
// "AA:BB:CC:DD:EE:FF" (either case).
inline bool obdValidMac(const char *m) {
    if (!m || strlen(m) != 17) return false;
    for (int i = 0; i < 17; i++) {
        char c = m[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (i % 3 == 2 ? c != ':' : !hex) return false;
    }
    return true;
}

// Does a Bluetooth name look like an ELM327-type adapter? `preferred` (OBD_BT_NAME)
// matches exactly; otherwise a known brand/keyword anywhere in the name, case-insensitive.
// Kept specific on purpose: a bare "ELM" would also match "HELMET" intercoms, which
// riders very likely have nearby.
inline bool obdNameLooksLikeAdapter(const char *name, const char *preferred) {
    if (!name || !*name) return false;
    char up[64];
    size_t n = 0;
    for (; name[n] && n < sizeof(up) - 1; n++) {
        char c = name[n];
        up[n] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[n] = 0;
    if (preferred && *preferred && !strcasecmp(name, preferred)) return true;
    static const char *const kHints[] = {
        "OBD", "ELM327", "ELM 327", "V-LINK", "VLINK", "VEEPEAK", "KONNWEI", "VGATE",
        "ICAR", "CARISTA", "KIWI",
    };
    for (const char *h : kHints) if (strstr(up, h)) return true;
    return false;
}

// "4100BE3EB811" -> 0xBE3EB811: which PIDs 01..20 the ECU supports (bit 31 = PID 01).
// Several ECUs may answer; their masks are OR-ed. 0 = no valid reply.
inline uint32_t obdSupportedPids(const char *resp) {
    uint32_t mask = 0;
    for (const char *p = strstr(resp, "4100"); p; p = strstr(p + 4, "4100")) {
        uint32_t m = 0;
        int i = 0;
        for (; i < 4; i++) {
            int b = obdHexByte(p + 4 + i * 2);
            if (b < 0) break;
            m = (m << 8) | (uint32_t)b;
        }
        if (i == 4) mask |= m;
    }
    return mask;
}
inline bool obdPidSupported(uint32_t mask, uint8_t pid) {
    return pid >= 1 && pid <= 0x20 && ((mask >> (32 - pid)) & 1);
}
