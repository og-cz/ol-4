/*
 * OL-4 opto leveler engine (v3.0) — C++ port of opto-engine.js, line for line.
 * Physical model of a late-70s solid-state electro-optical compressor/limiter (LA-4 / LA-4A style).
 *
 * Signal path: balanced op-amp input (RC4136, slew-limited) -> LOW-range gain stage
 *   -> attenuator: build-out resistor + photocell, gain = 1/(1+G)
 *   -> output op-amp (slew + HF grit)
 * Sidechain (LA-4A): LED driven mostly by the attenuated signal (feedback) plus a little
 *   of the input (feedforward), 4x interpolated; soft diode turn-on (2:1 = soft knee);
 *   CdS photocell rises faster with more light, recovers in the conductance domain,
 *   and recovers more slowly the longer it has been lit (memory).
 * Calibration: 0 dBFS = +20 dBu. Zero latency. No allocation in process().
 */
#pragma once
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ol4 {

struct Params {
    double threshold = 0.0;   // knob 0..10 (0 = no compression)
    int    ratio = 4;         // 2, 4, 8, 12, 20
    double output = 5.0;      // knob 0..10, 5 = 0 dB, +-20 dB
    bool   rangeLow = false;  // rear INPUT RANGE: LOW = +20 dB sensitivity
    bool   modded = false;    // rear OP-AMPS: stock RC4136 or modded (faster, cleaner)
    bool   link = true;       // STEREO strap
    bool   bypass = false;    // power off / bypass (crossfaded)
};

class OptoEngine {
public:
    static constexpr int kMaxCh = 8;

    void prepare(double sampleRate, int numChannels) {
        fs = sampleRate; nch = std::clamp(numChannels, 1, kMaxCh);
        derive(); reset();
    }

    void setParams(const Params& np) { p = np; derive(); }
    const Params& params() const { return p; }

    void reset() {
        std::memset(&st, 0, sizeof(st));
        for (int c = 0; c < nch; ++c) shelf[c] = shelfCoefs(0.0, false);
        lowShelf = shelfCoefs(kWeightDb, true);
        vu = 0; meterGR = 0; overload = false; ctr = 0;
        wet = p.bypass ? 0.0 : 1.0; outS = outGain; idleBypass = false;
    }

    // in-place processing allowed (in[c] == out[c])
    void process(const float* const* in, float* const* out, int n) {
        const double wetGoal = p.bypass ? 0.0 : 1.0;
        if (p.bypass && wet < 1e-6) {
            for (int c = 0; c < nch; ++c) if (out[c] != in[c]) std::memcpy(out[c], in[c], sizeof(float) * (size_t) n);
            wet = 0; meterGR = 0; idleBypass = true;
            return;
        }
        if (idleBypass) { reset(); wet = 0; idleBypass = false; }
        const double cSm = cSmooth, outGoal = outGain;
        const double invSens = 1.0 / sens;
        const bool doLink = p.link && nch > 1;
        bool ovl = false; double grMax = 0;
        double dry[kMaxCh], xs[kMaxCh], drive[kMaxCh * 4];

        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < nch; ++c) {
                dry[c] = in[c][i];
                double x = dry[c] * sens;
                const double a = std::abs(x);
                if (a > 0.9) { ovl = true; x = std::copysign(0.9 + 0.1 * std::tanh((a - 0.9) / 0.1), x); }
                x = slewStep(x, st.s1[c]); st.s1[c] = x;
                if (sens > 1) { x = slewStep(x, st.s2[c]); st.s2[c] = x; }
                xs[c] = x;
                const int o = c * 4;
                for (int q = 0; q < 4; ++q) {
                    const double t = q * 0.25;
                    const double yq = cr(st.yh[o + 3], st.yh[o + 2], st.yh[o + 1], st.yh[o], t);
                    const double xq = cr(st.xh[o + 3], st.xh[o + 2], st.xh[o + 1], st.xh[o], t);
                    drive[o + q] = g * (std::abs(yq) + beta * std::abs(xq));
                }
                st.xh[o + 3] = st.xh[o + 2]; st.xh[o + 2] = st.xh[o + 1]; st.xh[o + 1] = st.xh[o]; st.xh[o] = x;
            }
            if (doLink) {
                for (int q = 0; q < 4; ++q) {
                    double m = 0; for (int c = 0; c < nch; ++c) m = std::max(m, drive[c * 4 + q]);
                    for (int c = 0; c < nch; ++c) drive[c * 4 + q] = m;
                }
            }

            double vuIn = 0;
            for (int c = 0; c < nch; ++c) {
                double gsum = 0, lsum = 0;
                const double sk = ledKnee, isk = 1.0 / sk;
                for (int q = 0; q < 4; ++q) {
                    const double u = (drive[c * 4 + q] - 1.0) * isk;
                    double lq = (u > 20 ? u * sk : sk * std::log1p(std::exp(u))) - ledOff;
                    if (lq > 1e-6) { if (lq > 60) lq = 60; lsum += lq; gsum += kappa * std::pow(lq, kGamma); }
                }
                const double l = lsum * 0.25, Gt = gsum * 0.25;
                double G = st.G[c];
                if (Gt > G) {
                    double tau = kTauRise / (1 + kRiseLight * l) / ledSpeed;
                    if (tau < 0.0003) tau = 0.0003;
                    G += (Gt - G) * (1 - std::exp(-1 / (tau * fs)));
                } else {
                    const double tau = (kTauRel + kTauRelMem * std::pow(st.mem[c], 1.3)) * relScale;
                    G = Gt + (G - Gt) * std::exp(-1 / (tau * fs));
                }
                st.G[c] = G;
                const double gr = 20 * std::log10(1 + G);
                if (gr > 1) {
                    const double goal = std::min(1.0, gr / 4);
                    if (goal > st.mem[c]) st.mem[c] += (goal - st.mem[c]) * memUp;
                } else st.mem[c] -= st.mem[c] * memDn;
                grMax = std::max(grMax, gr);

                double y = xs[c] / (1 + G);
                const int ho = c * 4;
                st.yh[ho + 3] = st.yh[ho + 2]; st.yh[ho + 2] = st.yh[ho + 1]; st.yh[ho + 1] = st.yh[ho]; st.yh[ho] = y;

                y = slewStep(y, st.s3[c]); st.s3[c] = y;
                const double hpv = cHp * (st.hp[c] + y - st.hpx[c]);
                st.hpx[c] = y; st.hp[c] = hpv;
                y -= grit * hpv * hpv * hpv;

                st.gs[c] += cGs * (gr - st.gs[c]);
                if ((ctr & 31) == 0) {
                    const double dv = std::max(0.0, st.gs[c] - 0.8);
                    shelf[c] = shelfCoefs(-darkMax * (1 - std::exp(-dv / 4)), false);
                }
                const Biquad& sh = shelf[c];
                const double o = sh.b0 * y + st.z1[c];
                st.z1[c] = sh.b1 * y - sh.a1 * o + st.z2[c];
                st.z2[c] = sh.b2 * y - sh.a2 * o;
                y = o;

                y *= invSens * outS;
                y += h2 * y * y;
                const double dy = y - st.dcx[c] + cDc * st.dcy[c];
                st.dcx[c] = y; st.dcy[c] = dy; y = dy;
                const double ay = std::abs(y);
                if (ay > 1.2) y = std::copysign(1.2 + 0.38 * std::tanh((ay - 1.2) / 0.38), y);

                out[c][i] = (float) (wet * y + (1 - wet) * dry[c]);
                vuIn += std::abs(y);
            }
            vu = cVu * vu + (1 - cVu) * (vuIn / nch);
            outS += (outGoal - outS) * cSm;
            wet += (wetGoal - wet) * cSm;
            ++ctr;
        }
        meterGR = grMax;
        overload = ovl;
    }

    double currentGR() const { double m = 0; for (int c = 0; c < nch; ++c) m = std::max(m, 20 * std::log10(1 + st.G[c])); return m; }
    double vuDb(int mode /* 4 or 8 */) const {
        const double ref = std::pow(10.0, (mode == 8 ? -12.0 : -16.0) / 20.0);
        return 20 * std::log10(vu * 1.1107 / ref + 1e-9);
    }
    bool   overloaded() const { return overload; }
    bool   isBypassedIdle() const { return idleBypass; }

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State {
        double G[kMaxCh], mem[kMaxCh], gs[kMaxCh];
        double yh[kMaxCh * 4], xh[kMaxCh * 4];
        double s1[kMaxCh], s2[kMaxCh], s3[kMaxCh];
        double hp[kMaxCh], hpx[kMaxCh];
        double ls1[kMaxCh], ls2[kMaxCh];
        double lf[kMaxCh], dcx[kMaxCh], dcy[kMaxCh];
        double z1[kMaxCh], z2[kMaxCh];
    };

    // fitted per ratio position (index order 2,4,8,12,20) — same table as the JS engine
    static constexpr double kGamma = 0.92;
    static constexpr double kTauRise = 0.0120, kRiseLight = 0.12, kTauRel = 0.060, kTauRelMem = 0.70;
    static constexpr double kMemUp = 2.0, kMemDn = 2.5;
    static constexpr double kSlewStock = 1.7, kSlewModded = 13.0;
    static constexpr double kGritStock = 0.023, kGritModded = 0.004;
    static constexpr double kDarkStock = 3.3, kDarkModded = 0.7;
    static constexpr double kWeightDb = 0.5, kVpkFs = 10.96;

    static int ratioIndex(int r) { switch (r) { case 2: return 0; case 8: return 2; case 12: return 3; case 20: return 4; default: return 1; } }
    static constexpr int kRatios[5]     = { 2, 4, 8, 12, 20 };
    static constexpr double kKappa[5]   = { /*__KAPPA__*/ 1.575, 3.357, 1.792, 2.631, 4.83 };
    static constexpr double kBeta[5]    = { /*__BETA__*/ 0.08, 0.204, 0.9, 0.562, 0.294 };
    static constexpr double kG0[5]      = { /*__G0__*/ 0.37788, 0.51862, 0.40371, 0.47721, 0.55275 };
    static constexpr double kLedSpeed[5] = { 1.3, 1.45, 1.66, 1.82, 2.0 };
    static constexpr double kLedKnee[5]  = { 0.42, 0.16, 0.11, 0.1, 0.09 };

    static double cr(double p0, double p1, double p2, double p3, double t) {
        return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
    }
    double slewStep(double x, double prev) const {
        const double S = slew, d = x - prev;
        if (std::abs(d) < 0.25 * S) return x;
        return prev + S * std::tanh(d / S);
    }
    Biquad shelfCoefs(double dbG, bool low) const {
        const double pi = 3.14159265358979323846;
        const double f0 = low ? 110.0 : std::min(4500.0, 0.4 * fs);
        const double A = std::pow(10.0, dbG / 40), w0 = 2 * pi * f0 / fs;
        const double cw = std::cos(w0), sw = std::sin(w0), S = 0.7;
        const double alpha = sw / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2), sA = 2 * std::sqrt(A) * alpha;
        Biquad b;
        if (low) {
            const double a0 = (A + 1) + (A - 1) * cw + sA;
            b.b0 = A * ((A + 1) - (A - 1) * cw + sA) / a0; b.b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
            b.b2 = A * ((A + 1) - (A - 1) * cw - sA) / a0; b.a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
            b.a2 = ((A + 1) + (A - 1) * cw - sA) / a0;
        } else {
            const double a0 = (A + 1) - (A - 1) * cw + sA;
            b.b0 = A * ((A + 1) + (A - 1) * cw + sA) / a0; b.b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
            b.b2 = A * ((A + 1) + (A - 1) * cw - sA) / a0; b.a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
            b.a2 = ((A + 1) - (A - 1) * cw - sA) / a0;
        }
        return b;
    }
    void derive() {
        const double pi = 3.14159265358979323846;
        const int ri = ratioIndex(p.ratio);
        R = kRatios[ri];
        kappa = kKappa[ri]; beta = kBeta[ri]; ledSpeed = kLedSpeed[ri]; ledKnee = kLedKnee[ri];
        ledOff = ledKnee * std::log1p(std::exp(-1 / ledKnee));
        const double kn = std::clamp(p.threshold, 0.0, 10.0);
        double thr = 10 - 5.6 * kn;
        if (R == 2) thr -= 16;
        g = kG0[ri] * std::pow(10.0, -thr / 20);
        sens = p.rangeLow ? 10.0 : 1.0;
        outGain = std::pow(10.0, ((std::clamp(p.output, 0.0, 10.0) - 5) * 4) / 20);
        const bool stock = !p.modded;
        slew = (stock ? kSlewStock : kSlewModded) * 1e6 / fs / kVpkFs;
        grit = stock ? kGritStock : kGritModded;
        darkMax = stock ? kDarkStock : kDarkModded;
        h2 = stock ? 0.004 : 0.0015;
        xfmr = 1.0;
        cHp = std::exp(-2 * pi * 1500 / fs);
        cGs = 1 - std::exp(-1 / (0.03 * fs));
        cSmooth = 1 - std::exp(-1 / (0.008 * fs));
        cVu = std::exp(-1 / (0.065 * fs));
        cDc = std::exp(-2 * pi * 5 / fs);
        cLf = std::exp(-2 * pi * 90 / fs);
        memUp = 1 / (kMemUp * fs);
        memDn = 1 / (kMemDn * fs);
        relScale = 1 / (1 + 0.05 * (R - 2));
        lowShelf = shelfCoefs(kWeightDb, true);
    }

    Params p;
    double fs = 48000; int nch = 2;
    int R = 4; double kappa = 0, beta = 0, ledSpeed = 1, ledKnee = 0.16, ledOff = 0, g = 0, sens = 1, outGain = 1;
    double slew = 0, grit = 0, darkMax = 0, h2 = 0, xfmr = 1;
    double cHp = 0, cGs = 0, cSmooth = 0, cVu = 0, cDc = 0, cLf = 0, memUp = 0, memDn = 0, relScale = 1;
    State st {};
    Biquad shelf[kMaxCh], lowShelf;
    double vu = 0, meterGR = 0, wet = 1, outS = 1; bool overload = false, idleBypass = false;
    unsigned ctr = 0;
};

} // namespace ol4
