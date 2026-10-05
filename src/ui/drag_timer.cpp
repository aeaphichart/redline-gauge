#include "ui/drag_timer.h"
#include "config.h"
#include <math.h>
#include <string.h>

constexpr float DragTimer::kTh[];

// segment -> (begin threshold, end threshold); -1 = the launch
static const int8_t kSegFrom[SEG_COUNT] = { -1, 0, 1, -1, 2 };
static const int8_t kSegTo[SEG_COUNT]   = {  0, 1, 2,  3, 3 };

static const float kStillKmh   = 1.0f;     // below this the car counts as stopped
static const float kArmMs      = 1000;     // stopped this long -> STAGED
static const float kRearmMs    = 3000;     // after a result: stopped this long -> next run
static const float kLiftKmh    = 10;       // speed this far under the run's max ends it

// ---- log -------------------------------------------------------------------------------
void RunLog::add(RunRecord r) {
    r.seq = nextSeq++;
    if (count < kMax) count++;
    memmove(&runs[1], &runs[0], sizeof(RunRecord) * (count - 1));
    runs[0] = r;
}

void RunLog::clear() {
    count = 0;
    nextSeq = 1;
    memset(runs, 0, sizeof runs);
}

uint16_t RunLog::best(int seg) const {
    uint16_t b = RUN_NONE;
    for (int i = 0; i < count; i++)
        if (runs[i].cs[seg] < b) b = runs[i].cs[seg];
    return b;
}

// ---- timer -----------------------------------------------------------------------------
void DragTimer::again() {
    state_ = lastStamp_ ? TS_MOVING : TS_NO_SPEED;
    stillSince_ = -1;
    memset(crossed_, 0, sizeof crossed_);
    memset(newBest_, 0, sizeof newBest_);
    maxV_ = 0;
}

void DragTimer::begin(double t) {
    state_ = TS_RUN;
    start_ = zeroT_;                       // provisional: the last standstill sample
    firstT_ = t;
    firstV_ = 0;
    refined_ = false;
    memset(crossed_, 0, sizeof crossed_);
    memset(newBest_, 0, sizeof newBest_);
    maxV_ = 0;
}

double DragTimer::segBegin(int seg) const {
    int i = kSegFrom[seg];
    if (i < 0) return state_ == TS_RUN || hasResult() ? start_ : -1;
    return crossed_[i] ? cross_[i] : -1;
}

double DragTimer::segEnd(int seg) const {
    int i = kSegTo[seg];
    return crossed_[i] ? cross_[i] : -1;
}

bool DragTimer::segDone(int seg) const { return segBegin(seg) >= 0 && segEnd(seg) >= 0; }

bool DragTimer::segActive(int seg) const {
    return state_ == TS_RUN && segBegin(seg) >= 0 && segEnd(seg) < 0;
}

float DragTimer::segTime(int seg, uint32_t now) const {
    double b = segBegin(seg), e = segEnd(seg);
    if (b < 0) return -1;
    if (e >= 0) return (float)((e - b) / 1000.0);
    if (state_ == TS_RUN) return (float)((now - b) / 1000.0);
    return -1;
}

float DragTimer::segProgress(int seg) const {
    if (segDone(seg)) return 1;
    if (!segActive(seg)) return 0;
    float lo = kSegFrom[seg] < 0 ? 0 : kTh[kSegFrom[seg]], hi = kTh[kSegTo[seg]];
    float p = (lastV_ - lo) / (hi - lo);
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

float DragTimer::elapsed(uint32_t now) const {
    if (state_ == TS_RUN) return (float)((now - start_) / 1000.0);
    if (state_ == TS_FINISH) return (float)((cross_[3] - start_) / 1000.0);
    return 0;
}

TimerEvent DragTimer::end(const RunLog &log) {
    if (!crossed_[0]) { state_ = TS_NO_RESULT; stillSince_ = -1; return TE_DISCARD; }
    rec_ = {};
    for (int s = 0; s < SEG_COUNT; s++) {
        rec_.cs[s] = RUN_NONE;
        if (!segDone(s)) continue;
        double cs = (segEnd(s) - segBegin(s)) / 10.0;
        rec_.cs[s] = cs < 0 ? 0 : cs > 65000 ? 65000 : (uint16_t)lround(cs);
        newBest_[s] = rec_.cs[s] < log.best(s);
    }
    rec_.maxKmh = (uint16_t)lroundf(maxV_);
    stillSince_ = -1;
    state_ = crossed_[3] ? TS_FINISH : TS_SAVED;
    return state_ == TS_FINISH ? TE_FINISH : TE_SAVED;
}

TimerEvent DragTimer::update(float v, uint32_t stamp, uint32_t now, const RunLog &log) {
    bool stale = !stamp || now - stamp > DATA_STALE_MS;
    if (stale) {
        if (state_ == TS_RUN) return end(log);        // speed vanished mid-run
        if (!hasResult() && state_ != TS_NO_RESULT) state_ = TS_NO_SPEED;
        return TE_NONE;
    }
    if (stamp == lastStamp_) return TE_NONE;          // nothing new this frame
    bool first = lastStamp_ == 0;
    lastStamp_ = stamp;
    double t = stamp;
    if (v < 0) v = 0;
    TimerEvent ev = TE_NONE;

    switch (state_) {
    case TS_NO_SPEED:
    case TS_MOVING:
    case TS_STAGED:
        if (v < kStillKmh) {
            if (stillSince_ < 0) stillSince_ = t;
            zeroT_ = t;
            if (state_ != TS_STAGED && t - stillSince_ >= kArmMs) { state_ = TS_STAGED; ev = TE_ARMED; }
            else if (state_ == TS_NO_SPEED) state_ = TS_MOVING;   // "hold still" until armed
        } else if (state_ == TS_STAGED && !first) {
            begin(t);
            firstV_ = v;
            ev = TE_START;
        } else {
            state_ = TS_MOVING;
            stillSince_ = -1;
        }
        break;

    case TS_RUN: {
        // Second moving sample: back-estimate the launch from the initial acceleration
        // (the car left 0 somewhere between zeroT_ and the first moving sample).
        if (!refined_ && t > firstT_) {
            refined_ = true;
            float a = (v - firstV_) / (float)((t - firstT_) / 1000.0);     // km/h per s
            if (a > 0.5f) {
                double s = firstT_ - firstV_ / a * 1000.0;
                start_ = s < zeroT_ ? zeroT_ : s > firstT_ ? firstT_ : s;
            }
        }
        for (int i = 0; i < kThresholds; i++)
            if (!crossed_[i] && v >= kTh[i]) {
                float f = v > lastV_ ? (kTh[i] - lastV_) / (v - lastV_) : 1;
                if (f < 0) f = 0;
                cross_[i] = lastT_ + f * (t - lastT_);
                crossed_[i] = true;
                ev = TE_SPLIT;
            }
        if (v > maxV_) maxV_ = v;
        if (crossed_[3]) ev = end(log);
        else if (v < maxV_ - kLiftKmh || v < kStillKmh) ev = end(log);
        break;
    }

    case TS_FINISH:
    case TS_SAVED:
    case TS_NO_RESULT:
        if (v < kStillKmh) {
            if (stillSince_ < 0) stillSince_ = t;
            zeroT_ = t;
            if (t - stillSince_ >= kRearmMs) {        // parked again: ready for the next run
                memset(crossed_, 0, sizeof crossed_);
                memset(newBest_, 0, sizeof newBest_);
                maxV_ = 0;
                state_ = TS_STAGED;
                ev = TE_ARMED;
            }
        } else {
            stillSince_ = -1;
        }
        break;
    }
    lastT_ = t;
    lastV_ = v;
    return ev;
}
