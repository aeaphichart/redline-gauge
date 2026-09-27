#pragma once
// Physics-based car simulator: torque curve, gearbox with auto-shift, drag,
// coolant/intake heat model, charging voltage. Two flavours:
//   AUTO  — loops a scripted drive: idle, city, full-throttle pull, braking …
//   TOUCH — you are the throttle: hold the screen to accelerate, release to coast.
#include "data/source.h"
#include <stdint.h>

class SimSource : public DataSource {
public:
    explicit SimSource(bool touchMode) : touch_(touchMode) {}
    const char *name() const override { return touch_ ? "SIM TOUCH" : "SIM AUTO"; }
    void begin() override;
    void poll() override;

    // TOUCH mode input, written from the UI task. 0..1
    static volatile float touchThrottle;

    // Advance the model by dt seconds (public for the host preview).
    void step(float dt);

private:
    bool     touch_;
    uint32_t lastMs_ = 0;
    float    acc_ = 0;           // publish accumulator

    // vehicle state
    float speed_ = 0;            // m/s
    float rpm_ = 0;
    int   gear_ = 0;             // 0 = neutral
    float throttle_ = 0;         // actual pedal 0..1 (smoothed)
    float shiftTimer_ = 0;       // >0 while clutch is in during a shift
    int   pendingGear_ = 0;
    bool  launching_ = false;    // clutch slipping from standstill

    // thermal / electrical
    float coolant_ = 0, iat_ = 0, volt_ = 0, ambient_ = 32;

    // script (AUTO)
    int   phase_ = 0;
    float phaseT_ = 0;

    float engineTorque(float rpm) const;
    void  script(float dt, float &throttle, float &brake, float &shiftAt);
};
