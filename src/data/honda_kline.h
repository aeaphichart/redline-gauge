#pragma once
// Honda motorcycle K-line diagnostic protocol ("HDS tables"), pure helpers - no hardware,
// unit-tested in test/test_native. Used by HondaKSource.
//
// 10400 baud 8N1 on one wire (every sent byte is echoed back). Frame:
//   [addr][len incl. checksum][service][data...][checksum]
//   request addr 0x72 (ping 0xFE), reply addr 0x02 (ping reply 0x0E)
//   checksum: all bytes of the frame sum to 0 (mod 256)
// Session: wake pulse (K low 70 ms, high 120 ms) -> ping FE 04 72 8C (-> 0E 04 72 7C)
//          -> init 72 05 00 F0 99 (-> 02 04 00 FA) -> table reads 72 05 71 <t> <cs>.
// Sources: HondaECU / eculib (honda.py, frames/data.py), andreibaw/Honda_K-Line_KWP2000,
// sophienyaa/Honda-Motorcycle-ECU-Tools (CRF250L logs), gonzos.net CTX700 project.
#include <stdint.h>
#include <stddef.h>

static const uint8_t HK_PING[] = { 0xFE, 0x04, 0x72, 0x8C };
static const uint8_t HK_INIT[] = { 0x72, 0x05, 0x00, 0xF0, 0x99 };

inline uint8_t hkChecksum(const uint8_t *b, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s += b[i];
    return (uint8_t)(0x100 - s);
}

// 72 05 71 <table> <cs>
inline void hkTableRequest(uint8_t table, uint8_t out[5]) {
    out[0] = 0x72; out[1] = 0x05; out[2] = 0x71; out[3] = table;
    out[4] = hkChecksum(out, 4);
}

// A complete, well-formed reply frame: len byte matches, checksum sums to 0.
inline bool hkFrameOk(const uint8_t *f, size_t n) {
    if (n < 4 || f[1] != n) return false;
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s += f[i];
    return s == 0;
}

// Reply to a table read: 02 <len> 71 <table> <payload...> <cs>
inline bool hkIsTableReply(const uint8_t *f, size_t n, uint8_t table) {
    return hkFrameOk(f, n) && n >= 5 && f[0] == 0x02 && f[2] == 0x71 && f[3] == table;
}

struct HondaData {
    float rpm, tps, ect, iat, map, batt, speed;
};

// Main engine table. 0x10 / 0x11 (2008+ Keihin ECUs, the 500 twins included):
//   [0-1] rpm  [2] TPS V  [3] TPS %*1.6  [4] ECT V  [5] ECT+40  [6] IAT V  [7] IAT+40
//   [8] MAP V  [9] MAP kPa  [10-11] (FF FF)  [12] battery V*10  [13] speed km/h ...
// 0x13 / 0x17 are the same without the FF FF pair (battery at [10], speed at [11]).
inline bool hkDecodeMain(const uint8_t *f, size_t n, uint8_t table, HondaData &d) {
    if (!hkIsTableReply(f, n, table)) return false;
    const uint8_t *p = f + 4;
    size_t pn = n - 5;
    bool shortLayout = table == 0x13 || table == 0x17;
    size_t iBatt = shortLayout ? 10 : 12, iSpeed = iBatt + 1;
    if (pn <= iSpeed) return false;
    d.rpm = (float)(p[0] << 8 | p[1]);
    d.tps = p[3] / 1.6f;
    if (d.tps > 100) d.tps = 100;
    d.ect = p[5] - 40.0f;
    d.iat = p[7] - 40.0f;
    d.map = p[9];
    d.batt = p[iBatt] / 10.0f;
    d.speed = p[iSpeed];
    return true;
}

// Table 0xD1: payload[0] = 0x01 neutral (or clutch pulled), 0x00 in gear, 0x03 side stand down.
inline bool hkDecodeNeutral(const uint8_t *f, size_t n, bool &neutral) {
    if (!hkIsTableReply(f, n, 0xD1) || n < 6) return false;
    neutral = f[4] & 0x01;
    return true;
}
