#pragma once
// Speed-triggered performance timer: 0-100, 100-120, 120-160 and 0-200 km/h from one run
// (160-200 is kept for the log too). No drawing and no hardware here: it is fed the raw
// speed samples (value + the time they were published) and runs identically on the host.
//
//   wait  -> car stopped for 1 s           -> STAGED
//   STAGED -> speed leaves 0               -> RUN (start time back-estimated from the
//                                             first two moving samples)
//   RUN   -> 200 km/h                      -> FINISH
//   RUN   -> speed drops 10 below its max  -> SAVED (if 0-100 was done) / NO RESULT
//   result -> stopped for 3 s, or again()  -> STAGED for the next run
// Each threshold crossing is interpolated between the two samples around it, so the
// times are better than the sample rate (OBD speed is integer km/h at ~5-10 Hz).
#include <stdint.h>

enum Seg : uint8_t { SEG_0_100, SEG_100_120, SEG_120_160, SEG_0_200, SEG_160_200, SEG_COUNT };
static const uint16_t RUN_NONE = 0xFFFF;

struct RunRecord {
    uint16_t seq;                 // run number, 1..
    uint16_t cs[SEG_COUNT];       // segment times in 1/100 s, RUN_NONE = not reached
    uint16_t maxKmh;
};

// Newest-first ring of saved runs. Plain data: main.cpp stores it as one NVS blob.
struct RunLog {
    static const int kMax = 20;
    uint8_t   version = 1;
    uint8_t   count = 0;
    uint16_t  nextSeq = 1;
    RunRecord runs[kMax];

    void     add(RunRecord r);    // assigns seq
    void     clear();
    uint16_t best(int seg) const; // fastest cs of a segment, RUN_NONE if none
};

enum TimerState : uint8_t { TS_NO_SPEED, TS_MOVING, TS_STAGED, TS_RUN, TS_FINISH, TS_SAVED, TS_NO_RESULT };
enum TimerEvent : uint8_t { TE_NONE, TE_ARMED, TE_START, TE_SPLIT, TE_FINISH, TE_SAVED, TE_DISCARD };

class DragTimer {
public:
    static const int kThresholds = 4;
    static constexpr float kTh[kThresholds] = { 100, 120, 160, 200 };

    void again();                               // drop any result, wait for a stop
    // Call every frame with the newest speed sample (stamp 0 = no data yet).
    TimerEvent update(float kmh, uint32_t stamp, uint32_t now, const RunLog &log);

    TimerState state() const { return state_; }
    bool  running() const { return state_ == TS_RUN; }
    bool  hasResult() const { return state_ == TS_FINISH || state_ == TS_SAVED; }
    float elapsed(uint32_t now) const;          // s since launch (live while running)
    float segTime(int seg, uint32_t now) const; // s; live for the active segment; <0 = not started
    bool  segDone(int seg) const;
    bool  segActive(int seg) const;             // being timed right now
    float segProgress(int seg) const;           // 0..1 by speed, for the panel bars
    bool  newBest(int seg) const { return newBest_[seg]; }
    float speed() const { return lastV_; }
    float maxKmh() const { return maxV_; }
    const RunRecord &record() const { return rec_; }

private:
    TimerState state_ = TS_NO_SPEED;
    uint32_t lastStamp_ = 0;
    double   lastT_ = 0;                        // ms of the previous sample
    float    lastV_ = 0;
    double   stillSince_ = -1;                  // ms the car has been stopped since, -1 = moving
    double   zeroT_ = 0;                        // last sample at standstill before launch
    double   start_ = 0;                        // launch time (ms)
    double   firstT_ = 0; float firstV_ = 0;    // first moving sample (start refinement)
    bool     refined_ = false;
    double   cross_[kThresholds] = {};
    bool     crossed_[kThresholds] = {};
    float    maxV_ = 0;
    bool     newBest_[SEG_COUNT] = {};
    RunRecord rec_ = {};

    void       begin(double t);
    TimerEvent end(const RunLog &log);
    double     segBegin(int seg) const;         // ms, <0 = not yet
    double     segEnd(int seg) const;
};
