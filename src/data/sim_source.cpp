#include "data/sim_source.h"
#include "data/gauge_bus.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>

volatile float SimSource::touchThrottle = 0;

// ---- vehicle constants ----------------------------------------------------------
static const float kRatios[] = GEAR_RATIOS;
static const int   kGears = sizeof(kRatios) / sizeof(kRatios[0]);
static const float kMass = 1250.0f * 1.08f;               // kg, incl. rotating inertia
static const float kWheelR = TIRE_CIRCUMFERENCE_M / (2.0f * 3.14159265f);
static const float kLimiter = RPM_MAX - 300;

static float frand(float a) { return ((rand() & 0xFFFF) / 32767.5f - 1.0f) * a; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float toward(float cur, float target, float dt, float tau) {
    return cur + (target - cur) * (1.0f - expf(-dt / tau));
}

// ---- scripted drive for AUTO mode -------------------------------------------------
enum PhaseKind : uint8_t { P_IDLE, P_ACCEL, P_CRUISE, P_BRAKE };
struct Phase { PhaseKind kind; float targetKmh, throttle, shiftAt, maxTime; };
static const Phase kScript[] = {
    { P_IDLE,     0, 0.00f,              0,  5 },   // warm idle after start-up
    { P_ACCEL,   50, 0.35f,           2700, 15 },   // city
    { P_CRUISE,  50, 0.00f,           2700,  5 },
    { P_ACCEL,   80, 0.55f,           3400, 12 },
    { P_CRUISE,  80, 0.00f,           3400,  5 },
    { P_ACCEL,  175, 1.00f, RPM_SHIFT + 150, 16 },  // full pull through the shift light
    { P_BRAKE,   60, 0.60f,              0, 10 },
    { P_CRUISE,  60, 0.00f,           3000,  4 },
    { P_BRAKE,    0, 0.45f,              0, 12 },
    { P_IDLE,     0, 0.00f,              0,  3 },
    { P_ACCEL,  110, 0.80f,           5200, 12 },   // brisk on-ramp
    { P_CRUISE, 110, 0.00f,           3000,  6 },
    { P_BRAKE,    0, 0.55f,              0, 14 },
};
static const int kPhases = sizeof(kScript) / sizeof(kScript[0]);

void SimSource::script(float dt, float &thr, float &brake, float &shiftAt) {
    const Phase &p = kScript[phase_];
    float kmh = speed_ * 3.6f;
    bool done = false;
    phaseT_ += dt;
    thr = 0; brake = 0;
    if (p.shiftAt > 0) shiftAt = p.shiftAt;
    switch (p.kind) {
        case P_IDLE:   done = phaseT_ > p.maxTime; break;
        case P_ACCEL:  thr = p.throttle; done = kmh >= p.targetKmh; break;
        case P_CRUISE: thr = clampf(0.16f + (p.targetKmh - kmh) * 0.06f, 0, 0.6f);
                       done = phaseT_ > p.maxTime; break;
        case P_BRAKE:  brake = p.throttle; done = kmh <= p.targetKmh + 0.5f; break;
    }
    if (done || phaseT_ > p.maxTime + 20) {
        phase_ = (phase_ + 1) % kPhases;
        if (phase_ == 0) phase_ = 1;       // the start-up idle only runs once
        phaseT_ = 0;
    }
}

// ---- engine ----------------------------------------------------------------------------
float SimSource::engineTorque(float rpm) const {
    // ~190 Nm peak at 4500 rpm, falls off both sides — a lively 2.0 NA four.
    float x = (rpm - 4500.0f) / 5200.0f;
    return clampf(190.0f * (1.0f - x * x), 60.0f, 190.0f);
}

void SimSource::begin() {
    speed_ = 0; gear_ = 0; throttle_ = 0; shiftTimer_ = 0; launching_ = false;
    rpm_ = 0; phase_ = 0; phaseT_ = -1.6f;                  // negative = cranking
    coolant_ = 38; iat_ = ambient_ + 2; volt_ = 12.5f;
    acc_ = 0;
    lastMs_ = bus::nowMs();
    bus::setLink(Link::Simulated, "");
}

void SimSource::poll() {
    uint32_t now = bus::nowMs();
    float dt = (now - lastMs_) / 1000.0f;
    if (dt < 0.01f) return;                                  // 100 Hz physics max
    lastMs_ = now;
    step(dt > 0.1f ? 0.1f : dt);
}

void SimSource::step(float dt) {
    float thrIn = 0, brake = 0, shiftAt = RPM_SHIFT + 150;

    // ---- crank & start (AUTO and TOUCH both do this once) ----
    if (phaseT_ < 0) {
        phaseT_ += dt;
        if (phaseT_ < -0.4f) { rpm_ = 230 + frand(40); volt_ = 10.6f + frand(0.2f); }
        else                 { rpm_ = toward(rpm_, 1700, dt, 0.08f); volt_ = toward(volt_, 14.3f, dt, 0.3f); }
        if (phaseT_ >= 0) phaseT_ = 0;
    } else {
        if (touch_) {
            thrIn = clampf(touchThrottle, 0, 1);
            brake = thrIn > 0.05f ? 0 : 0.12f;               // lift off = gentle engine/brake decel
            shiftAt = thrIn > 0.9f ? RPM_SHIFT + 150 : 3200;
        } else {
            script(dt, thrIn, brake, shiftAt);
        }
    }
    bool running = phaseT_ >= 0;
    throttle_ = toward(throttle_, thrIn, dt, 0.12f);         // pedal isn't instantaneous

    float idle = coolant_ < 55 ? 1150 : RPM_IDLE;
    float kmh = speed_ * 3.6f;

    // ---- gearbox logic ----
    if (running) {
        if (shiftTimer_ > 0) {
            shiftTimer_ -= dt;
            if (shiftTimer_ <= 0) gear_ = pendingGear_;
        } else if (gear_ == 0) {
            if (throttle_ > 0.05f) {
                // pick up the highest gear that keeps the engine above ~1600 rpm
                float wheel = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
                gear_ = 1;
                while (gear_ < kGears && wheel * kRatios[gear_] * FINAL_DRIVE > 1600) gear_++;
                launching_ = gear_ == 1;
            }
        } else {
            // kick-down: flooring it drops to the lowest gear that still has rev headroom
            int kick = gear_;
            if (thrIn > 0.85f && !launching_) {
                float wheel = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
                while (kick > 1 && wheel * kRatios[kick - 2] * FINAL_DRIVE < shiftAt - 1800) kick--;
            }
            if (kick < gear_) {
                pendingGear_ = kick; shiftTimer_ = 0.3f;
            } else if (rpm_ > shiftAt && gear_ < kGears && !launching_) {
                pendingGear_ = gear_ + 1; shiftTimer_ = throttle_ > 0.8f ? 0.18f : 0.35f;
            } else if ((kmh < 6 || (brake > 0 && rpm_ < 1300)) && throttle_ < 0.05f) {
                // stopping, or braking below useful revs: clutch in and let it idle
                gear_ = 0; launching_ = false;
            } else if (gear_ > 1 && rpm_ < (throttle_ > 0.3f ? 1500 : 1300) && !launching_) {
                pendingGear_ = gear_ - 1; shiftTimer_ = 0.3f;
            }
        }
    }

    // ---- forces ----
    float wheelRpm = speed_ / kWheelR * 60.0f / (2.0f * 3.14159265f);
    float drive = 0;
    bool clutchIn = shiftTimer_ > 0 || gear_ == 0 || !running;

    if (!clutchIn) {
        float ratio = kRatios[gear_ - 1] * FINAL_DRIVE;
        float matched = wheelRpm * ratio;
        float engineRpm = matched;
        if (launching_) {
            // clutch slipping: engine held at a launch rpm until wheels catch up
            float launchRpm = idle + throttle_ * 2600;
            engineRpm = fmaxf(matched, launchRpm);
            if (matched >= launchRpm * 0.97f) launching_ = false;
        }
        float tq = rpm_ >= kLimiter ? 0 : engineTorque(engineRpm) * throttle_;
        tq -= (1.0f - throttle_) * (12.0f + engineRpm * 0.006f);   // engine braking
        drive = tq * ratio * 0.9f / kWheelR;
        rpm_ = toward(rpm_, fmaxf(engineRpm, launching_ ? engineRpm : idle * 0.9f), dt, 0.05f);
        if (rpm_ >= kLimiter) rpm_ = kLimiter - 40 - frand(30);      // bounce off the limiter
    } else {
        // free-revving (neutral or mid-shift): rises with throttle, decays toward idle/target
        float target = running ? idle + throttle_ * (RPM_MAX - 1500 - idle) : rpm_;
        if (shiftTimer_ > 0 && pendingGear_ > 0)                    // mid-shift: fall toward new gear
            target = fmaxf(wheelRpm * kRatios[pendingGear_ - 1] * FINAL_DRIVE, idle);
        if (running) rpm_ = toward(rpm_, target, dt, shiftTimer_ > 0 ? 0.07f : 0.3f);
        if (running && shiftTimer_ <= 0 && gear_ == 0) rpm_ += frand(8);   // idle hunt
    }

    float drag = 0.5f * 1.2f * 0.68f * speed_ * speed_ + (speed_ > 0.05f ? 0.013f * 1250 * 9.81f : 0);
    float brakeF = brake * 9500.0f * (speed_ > 0.05f ? 1 : 0);
    speed_ += (drive - drag - brakeF) / kMass * dt;
    if (speed_ < 0) speed_ = 0;

    // ---- thermal & electrical ----
    float load = throttle_ * rpm_ / RPM_MAX;
    if (running) {
        if (coolant_ < 88) coolant_ += (0.45f + rpm_ / 3500.0f + load * 2.0f) * dt;
        else {
            float target = 88 + load * 22 - fminf(kmh / 40.0f, 1.0f) * 3 + (kmh < 5 ? 3 : 0);
            coolant_ = toward(coolant_, target, dt, 18.0f);
        }
        float iatTarget = ambient_ + 5 + (kmh < 10 ? 12 : 0) - fminf(kmh, 100.0f) / 100 * 3 + load * 8;
        iat_ = toward(iat_, iatTarget, dt, 14.0f);
        float vTarget = rpm_ < 1000 ? 13.85f : 14.25f;
        volt_ = toward(volt_, vTarget + frand(0.04f), dt, 0.8f);
    }

    // ---- publish at 25 Hz (roughly what a fast OBD link would give) ----
    acc_ += dt;
    if (acc_ >= 0.04f) {
        acc_ = 0;
        bus::publish(CH_RPM, rpm_);
        bus::publish(CH_SPEED, speed_ * 3.6f);
#if GEAR_COUNT > 0
        bus::publish(CH_GEAR, (float)gear_);          // GEAR_COUNT 0 = no gear shown anywhere
#endif
        bus::publish(CH_COOLANT, coolant_);
        bus::publish(CH_VOLTAGE, volt_);
        bus::publish(CH_IAT, iat_);
        bus::publish(CH_THROTTLE, throttle_ * 100);
    }
}
