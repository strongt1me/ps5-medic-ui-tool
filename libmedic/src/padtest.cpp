// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/padtest.hpp"

#include <algorithm>
#include <cmath>

namespace medic {

namespace {

constexpr std::uint32_t kButtonBits[PadTest::kButtons] = {kPadCross, kPadCircle, kPadSquare,  kPadTriangle, kPadL1,  kPadR1,   kPadL2,   kPadR2,
                                                          kPadL3,    kPadR3,     kPadOptions, kPadTouch,    kPadUp,  kPadDown, kPadLeft, kPadRight};
constexpr int kL2Idx = 6, kR2Idx = 7;

constexpr std::uint32_t kRestSettleMs = 1000, kRestWindowMs = 3000, kMotionSettleMs = 800, kMotionRestMs = 2000;
constexpr double kDriftOk = 5.0, kDriftLight = 10.0;  // Prozent des Hubs: ab 5 % leichter Drift, ab 10 % Drift (Richtwerte)
constexpr double kTouchedStick = 0.25;                // waehrend der Ruhemessung ueber 25 % Ausschlag: der Stick wurde angefasst
constexpr int kCircleMinSectors = 68;                 // 94 % der 72 Sektoren
constexpr double kCircleMinAvg = 0.88;
constexpr int kTriggerFull = 250, kTriggerReleased = 8, kTriggerDigitalFromAnalog = 200;
constexpr int kMinSamples = 5;

double stick_dist(std::uint8_t x, std::uint8_t y) {
    const double dx = (static_cast<int>(x) - 128) / 127.0, dy = (static_cast<int>(y) - 128) / 127.0;
    return std::sqrt(dx * dx + dy * dy);
}

double mag3(const std::array<double, 3>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

bool looks_like_gravity(double m) { return (m > 0.8 && m < 1.25) || (m > 7.8 && m < 11.8); }  // 1 g oder 9,8 m/s^2

PadTest::StageResult ok_result() { return {PadTest::Verdict::Ok, ""}; }
PadTest::StageResult warn_result(const char* reason) { return {PadTest::Verdict::Warn, reason}; }
PadTest::StageResult skipped_result(const char* reason) { return {PadTest::Verdict::Skipped, reason}; }

}  // namespace

bool decode_battery_state(const std::uint8_t* raw, std::size_t n, int& pct, int& state) {
    pct = state = -1;
    if (!raw || n < 8) return false;
    bool any = false;
    for (std::size_t i = 0; i < 8; ++i) any = any || raw[i] != 0;
    if (!any) return false;
    auto u32 = [&](std::size_t o) { return static_cast<unsigned long long>(raw[o]) | static_cast<unsigned long long>(raw[o + 1]) << 8 | static_cast<unsigned long long>(raw[o + 2]) << 16 | static_cast<unsigned long long>(raw[o + 3]) << 24; };
    if (u32(4) > 100) return false;
    pct = static_cast<int>(u32(4));
    state = static_cast<int>(std::min<unsigned long long>(u32(0), 255));
    return true;
}

const char* PadTest::drift_label(double pct) { return pct < kDriftOk ? "ok" : (pct < kDriftLight ? "light" : "drift"); }

int PadTest::rest_deadzone_pct(int stick) const { return static_cast<int>(std::ceil(rest_dist_[static_cast<std::size_t>(stick)])) + 2; }

int PadTest::buttons_pressed() const {
    int n = 0;
    for (const bool b : pressed_) n += b ? 1 : 0;
    return n;
}

int PadTest::sectors_reached(int stick) const {
    int n = 0;
    for (const float r : circle_max_[static_cast<std::size_t>(stick)]) n += r >= kReach ? 1 : 0;
    return n;
}

double PadTest::circle_avg_radius(int stick) const {
    double sum = 0;
    int n = 0;
    for (const float r : circle_max_[static_cast<std::size_t>(stick)])
        if (r >= kReach) {
            sum += std::min(r, 1.0f);
            ++n;
        }
    return n ? sum / n : 0.0;
}

int PadTest::cells_touched() const {
    int n = 0;
    for (const bool c : cells_) n += c ? 1 : 0;
    return n;
}

double PadTest::measure_progress() const {
    if (stage_ == Stage::StickRest) {
        if (rest_done_) return 1.0;
        if (!win_started_) return 0.0;
        return std::min(1.0, static_cast<double>(last_now_ - win_t0_) / kRestWindowMs);
    }
    if (stage_ == Stage::Motion) {
        if (mphase_ == MotionPhase::Move) return 1.0;
        if (mphase_ == MotionPhase::Settle || !win_started_) return 0.0;
        return std::min(1.0, static_cast<double>(last_now_ - win_t0_) / kMotionRestMs);
    }
    return 0.0;
}

void PadTest::enter(Stage s) {
    stage_ = s;
    done_ = false;
    stage_started_ = false;
    win_started_ = false;
    mphase_ = MotionPhase::Settle;
}

void PadTest::finish_stage(bool user_skip) {
    if (stage_ == Stage::Summary) return;
    results_[static_cast<std::size_t>(stage_)] = user_skip ? skipped_result("not_tested") : evaluate(stage_);
    enter(static_cast<Stage>(static_cast<int>(stage_) + 1));
}

void PadTest::next() { finish_stage(false); }
void PadTest::skip() { finish_stage(true); }

PadTest::StageResult PadTest::evaluate(Stage s) const {
    switch (s) {
        case Stage::Buttons: {
            const int n = buttons_pressed();
            if (n == kButtons) return ok_result();
            return n == 0 ? skipped_result("not_tested") : warn_result("buttons_missing");
        }
        case Stage::StickRest: {
            if (!rest_done_) return skipped_result("not_tested");
            return rest_dist_[0] < kDriftOk && rest_dist_[1] < kDriftOk ? ok_result() : warn_result("stick_drift");
        }
        case Stage::StickCircle: {
            const int a = sectors_reached(0), b = sectors_reached(1);
            if (a + b == 0) return skipped_result("not_tested");
            const bool good = a >= kCircleMinSectors && b >= kCircleMinSectors && circle_avg_radius(0) >= kCircleMinAvg && circle_avg_radius(1) >= kCircleMinAvg;
            return good ? ok_result() : warn_result("stick_range");
        }
        case Stage::Triggers: {
            if (!any_extra_) return skipped_result("not_supported");
            if (trig_max_[0] < 16 && trig_max_[1] < 16) return skipped_result("not_tested");
            const bool good = trig_max_[0] >= kTriggerFull && trig_max_[1] >= kTriggerFull && trig_released_[0] && trig_released_[1];
            return good ? ok_result() : warn_result("trigger_range");
        }
        case Stage::Touchpad: {
            if (!any_extra_) return skipped_result("not_supported");
            if (!saw_finger_) return skipped_result("not_tested");
            if (cells_touched() < kCols * kRows) return warn_result("touch_dead_zone");
            return saw_two_ ? ok_result() : warn_result("touch_one_finger");
        }
        case Stage::Motion: {
            if (!any_extra_) return skipped_result("not_supported");
            if (mphase_ != MotionPhase::Move) return skipped_result("not_tested");
            if (motion_zero_) return warn_result("motion_none");
            if (accel_group_ == 0) return warn_result("motion_gravity");
            if (accel_moved_ && gyro_moved_) return ok_result();
            return accel_moved_ || gyro_moved_ ? warn_result("motion_weak") : skipped_result("not_tested");
        }
        case Stage::Summary: break;
    }
    return {};
}

void PadTest::feed(const PadSample& s, std::uint32_t now_ms) {
    cur_ = s;
    last_now_ = now_ms;
    if (s.extra) any_extra_ = true;
    switch (stage_) {
        case Stage::Buttons: feed_buttons(s); break;
        case Stage::StickRest: feed_rest(s, now_ms); break;
        case Stage::StickCircle: feed_circle(s); break;
        case Stage::Triggers: feed_triggers(s); break;
        case Stage::Touchpad: feed_touch(s); break;
        case Stage::Motion: feed_motion(s, now_ms); break;
        case Stage::Summary: break;
    }
}

void PadTest::feed_buttons(const PadSample& s) {
    std::uint32_t down_mask = 0;  // Bit i = Taste i gedrueckt
    for (int i = 0; i < kButtons; ++i) {
        bool down = (s.buttons & kButtonBits[i]) != 0;
        if (s.extra && ((i == kL2Idx && s.l2 >= kTriggerDigitalFromAnalog) || (i == kR2Idx && s.r2 >= kTriggerDigitalFromAnalog))) down = true;  // der digitale Bit kann fehlen
        if (down) down_mask |= 1u << i;
    }
    if (!buttons_seen_) {  // was beim Start schon gedrueckt ist (die Taste, die den Test gestartet hat), zaehlt nicht
        buttons_seen_ = true;
        prev_down_ = down_mask;
        prev_buttons_ = s.buttons;
    }
    const std::uint32_t rising = down_mask & ~prev_down_, newly = s.buttons & ~prev_buttons_;
    prev_down_ = down_mask;
    prev_buttons_ = s.buttons;
    for (int i = 0; i < kButtons; ++i)
        if (rising & (1u << i)) pressed_[static_cast<std::size_t>(i)] = true;
    if (newly & kPadCircle) {
        if (last_was_circle_) circle_exit_ = true;
        last_was_circle_ = true;
    } else if (newly & ~kPadCircle) {
        last_was_circle_ = false;
    }
    done_ = buttons_pressed() == kButtons;
}

void PadTest::feed_rest(const PadSample& s, std::uint32_t now) {
    if (rest_done_) return;
    if (!stage_started_) {
        stage_started_ = true;
        stage_t0_ = now;
    }
    if (now - stage_t0_ < kRestSettleMs) return;  // dem Nutzer Zeit lassen, die Sticks loszulassen
    const double d[2] = {stick_dist(s.lx, s.ly), stick_dist(s.rx, s.ry)};
    if (!win_started_ || d[0] > kTouchedStick || d[1] > kTouchedStick) {
        if (win_started_) ++rest_restarts_;  // angefasst: von vorn
        win_started_ = true;
        win_t0_ = now;
        rest_n_ = 0;
        rest_sum_x_ = rest_sum_y_ = {};
        return;
    }
    rest_sum_x_[0] += (static_cast<int>(s.lx) - 128) / 127.0;
    rest_sum_y_[0] += (static_cast<int>(s.ly) - 128) / 127.0;
    rest_sum_x_[1] += (static_cast<int>(s.rx) - 128) / 127.0;
    rest_sum_y_[1] += (static_cast<int>(s.ry) - 128) / 127.0;
    ++rest_n_;
    for (int k = 0; k < 2; ++k) rest_max_[static_cast<std::size_t>(k)] = std::max(rest_max_[static_cast<std::size_t>(k)], d[k] * 100.0);
    if (now - win_t0_ >= kRestWindowMs && rest_n_ >= kMinSamples) {
        for (std::size_t k = 0; k < 2; ++k) {
            const double mx = rest_sum_x_[k] / rest_n_, my = rest_sum_y_[k] / rest_n_;
            rest_dist_[k] = std::sqrt(mx * mx + my * my) * 100.0;
        }
        rest_done_ = true;
        done_ = true;
    }
}

void PadTest::feed_circle(const PadSample& s) {
    const std::uint8_t xy[2][2] = {{s.lx, s.ly}, {s.rx, s.ry}};
    for (std::size_t k = 0; k < 2; ++k) {
        const double dx = (static_cast<int>(xy[k][0]) - 128) / 127.0, dy = (static_cast<int>(xy[k][1]) - 128) / 127.0;
        const double r = std::sqrt(dx * dx + dy * dy);
        if (r < 0.2) continue;  // in der Mitte ist der Winkel Rauschen
        double a = std::atan2(dy, dx);
        if (a < 0) a += 2 * 3.14159265358979323846;
        const int sector = std::min(kSectors - 1, static_cast<int>(a / (2 * 3.14159265358979323846) * kSectors));
        float& m = circle_max_[k][static_cast<std::size_t>(sector)];
        m = std::max(m, static_cast<float>(r));
    }
    done_ = sectors_reached(0) >= kCircleMinSectors && sectors_reached(1) >= kCircleMinSectors;
}

void PadTest::feed_triggers(const PadSample& s) {
    if (!s.extra) return;
    const int v[2] = {s.l2, s.r2};
    for (std::size_t k = 0; k < 2; ++k) {
        trig_max_[k] = std::max(trig_max_[k], v[k]);
        if (trig_max_[k] >= kTriggerFull && v[k] <= kTriggerReleased) trig_released_[k] = true;
    }
    done_ = trig_released_[0] && trig_released_[1];
}

void PadTest::feed_touch(const PadSample& s) {
    if (!s.extra) return;
    const int n = std::min(2, std::max(0, s.touch_count));
    for (int i = 0; i < n; ++i) {
        const int col = std::clamp(s.finger[i].x * kCols / 1920, 0, kCols - 1);
        const int row = std::clamp(s.finger[i].y * kRows / 1080, 0, kRows - 1);
        cells_[static_cast<std::size_t>(row * kCols + col)] = true;
        saw_finger_ = true;
    }
    if (n == 2 && (std::abs(s.finger[0].x - s.finger[1].x) > 150 || std::abs(s.finger[0].y - s.finger[1].y) > 150)) saw_two_ = true;
    done_ = cells_touched() == kCols * kRows && saw_two_;
}

void PadTest::feed_motion(const PadSample& s, std::uint32_t now) {
    if (!s.extra) return;
    if (!stage_started_) {
        stage_started_ = true;
        stage_t0_ = now;
    }
    if (mphase_ == MotionPhase::Settle) {
        if (now - stage_t0_ < kMotionSettleMs) return;
        mphase_ = MotionPhase::Rest;
        win_started_ = true;
        win_t0_ = now;
        motion_n_ = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            msum_a_[i] = msum_b_[i] = 0;
            mmin_a_[i] = mmin_b_[i] = 1e30;
            mmax_a_[i] = mmax_b_[i] = -1e30;
        }
    }
    if (mphase_ == MotionPhase::Rest) {
        for (std::size_t i = 0; i < 3; ++i) {
            msum_a_[i] += s.va[i];
            msum_b_[i] += s.vb[i];
            mmin_a_[i] = std::min(mmin_a_[i], static_cast<double>(s.va[i]));
            mmax_a_[i] = std::max(mmax_a_[i], static_cast<double>(s.va[i]));
            mmin_b_[i] = std::min(mmin_b_[i], static_cast<double>(s.vb[i]));
            mmax_b_[i] = std::max(mmax_b_[i], static_cast<double>(s.vb[i]));
        }
        ++motion_n_;
        if (now - win_t0_ >= kMotionRestMs && motion_n_ >= kMinSamples) {
            noise_a_ = noise_b_ = 0;
            for (std::size_t i = 0; i < 3; ++i) {
                mean_a_[i] = msum_a_[i] / motion_n_;
                mean_b_[i] = msum_b_[i] / motion_n_;
                noise_a_ = std::max(noise_a_, mmax_a_[i] - mmin_a_[i]);
                noise_b_ = std::max(noise_b_, mmax_b_[i] - mmin_b_[i]);
            }
            const double ma = mag3(mean_a_), mb = mag3(mean_b_);
            motion_zero_ = ma < 1e-6 && mb < 1e-6 && noise_a_ == 0 && noise_b_ == 0;
            const bool ga = looks_like_gravity(ma), gb = looks_like_gravity(mb);
            accel_group_ = (ga && !gb) ? 1 : ((gb && !ga) ? 2 : 0);
            mphase_ = MotionPhase::Move;
        }
        return;
    }
    // Move: Ausschlag gegen den Ruhewert
    double dev_a = 0, dev_b = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        dev_a += (s.va[i] - mean_a_[i]) * (s.va[i] - mean_a_[i]);
        dev_b += (s.vb[i] - mean_b_[i]) * (s.vb[i] - mean_b_[i]);
    }
    peak_a_ = std::max(peak_a_, std::sqrt(dev_a));
    peak_b_ = std::max(peak_b_, std::sqrt(dev_b));
    if (accel_group_ == 0) return;
    const double accel_peak = accel_group_ == 1 ? peak_a_ : peak_b_, gyro_peak = accel_group_ == 1 ? peak_b_ : peak_a_;
    const double accel_mean = accel_group_ == 1 ? mag3(mean_a_) : mag3(mean_b_), gyro_noise = accel_group_ == 1 ? noise_b_ : noise_a_;
    const double accel_need = 0.3 * accel_mean, gyro_need = std::max(0.5, 8.0 * gyro_noise);
    accel_moved_ = accel_peak >= accel_need;
    gyro_moved_ = gyro_peak >= gyro_need;
    accel_prog_ = std::min(1.0, accel_peak / accel_need);
    gyro_prog_ = std::min(1.0, gyro_peak / gyro_need);
    done_ = accel_moved_ && gyro_moved_;
}

TestResult PadTest::result(const std::string& started) const {
    TestResult r;
    r.id = "hw.pad.buttons";
    r.started = started;
    int ok = 0, warn = 0, skipped = 0;
    std::string first_warn;
    for (int i = 0; i < kStageCount; ++i) {
        const StageResult& sr = results_[static_cast<std::size_t>(i)];
        if (sr.verdict == Verdict::Ok) ++ok;
        else if (sr.verdict == Verdict::Warn) {
            ++warn;
            if (first_warn.empty()) first_warn = sr.reason;
        } else ++skipped;  // Pending (nie erreicht) zaehlt wie uebersprungen
    }
    if (warn) {
        r.status = TestStatus::Warn;
        r.reason = first_warn;
    } else if (ok == 0) {
        r.status = TestStatus::Skipped;
        r.reason = "not_tested";
    } else {
        r.status = TestStatus::Passed;
        if (skipped) r.reason = "partial";
    }
    const int pressed = buttons_pressed();
    r.measurements.push_back({"buttons_pressed", static_cast<double>(pressed), ""});
    r.measurements.push_back({"buttons_missing", static_cast<double>(kButtons - pressed), ""});
    if (rest_done_) {
        r.measurements.push_back({"stick_rest_left_pct", std::round(rest_dist_[0] * 10) / 10, "%"});
        r.measurements.push_back({"stick_rest_right_pct", std::round(rest_dist_[1] * 10) / 10, "%"});
    }
    if (sectors_reached(0) + sectors_reached(1) > 0) {
        r.measurements.push_back({"stick_sectors_left", static_cast<double>(sectors_reached(0)), "of 72"});
        r.measurements.push_back({"stick_sectors_right", static_cast<double>(sectors_reached(1)), "of 72"});
    }
    if (trig_max_[0] || trig_max_[1]) {
        r.measurements.push_back({"trigger_l2_max", static_cast<double>(trig_max_[0]), "of 255"});
        r.measurements.push_back({"trigger_r2_max", static_cast<double>(trig_max_[1]), "of 255"});
    }
    if (saw_finger_) r.measurements.push_back({"touch_cells", static_cast<double>(cells_touched()), "of 18"});
    if (cur_.battery_pct >= 0) {  // nur zur Anzeige und fuer die Akten: aendert das Ergebnis nicht
        r.measurements.push_back({"battery_pct", static_cast<double>(cur_.battery_pct), "%"});
        if (cur_.battery_state >= 0) r.measurements.push_back({"battery_state_raw", static_cast<double>(cur_.battery_state), ""});
    }
    return r;
}

}  // namespace medic
