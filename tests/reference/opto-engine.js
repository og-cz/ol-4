/*
 * OL-4 opto leveler engine, v3 — physical model of a late-70s solid-state
 * electro-optical compressor/limiter (LA-4 / LA-4A style).
 *
 * Built from: the UREI LA-4 manual (1980), E. Ciletti's LA-4 write-ups,
 * JLM/PRR notes on the LA-4A sidechain (GroupDIY), and DIY Recording
 * Equipment's OLA5 design notes (which reuse the LA-4 sidechain).
 *
 * Signal path
 *   balanced op-amp input (RC4136: slew-limited) → LOW-range gain stage
 *   → attenuator: build-out resistor R13 + photocell to ground
 *        gain = 1 / (1 + G),   G = R13 / R_ldr  (photocell conductance)
 *   → output op-amp (RC4136) → output transformer (adds weight)
 *
 * Sidechain (LA-4A): LED current is driven mostly by the ATTENUATED signal
 * (feedback, via 2k2) plus a little of the signal BEFORE the attenuator
 * (via 1M). The small input share keeps output flat when the photocell
 * runs out of range at heavy limiting. No sidechain high-pass: heavy lows
 * drive the gain reduction, as on the original.
 *   LED light  ℓ = max(0, g·(|y| + β·|x|) − 1)          (turn-on knee)
 *   photocell  G_target = κ · ℓ^γ                        (CdS, near-log response)
 * RATIO switches LED drive (κ) and the feed mix (β); THRESHOLD sets g.
 * Constants κ, β, g per ratio are fitted so each switch position measures
 * its nominal ratio over the working range (fit-sidechain.js).
 *
 * Photocell dynamics: rises faster under more light; recovers in the
 * conductance domain (quick at first, slow tail) and recovers more slowly
 * the longer it has been lit (memory).
 *
 * Manual targets: attack 1–10 ms (63%), release 100 ms–1 s (63%),
 * 2:1 lowers threshold ~16 dB, response ±0.5 dB 20 Hz–20 kHz, zero latency.
 * Level calibration: 0 dBFS = +20 dBu.
 */
(function (root) {
  'use strict';

  const RATIOS = [2, 4, 8, 12, 20];
  const GAMMA = 0.92;
  // fitted per ratio position: LED drive κ, input-feed share β, sidechain gain g0 (at 0 dBFS threshold)
  const RATIO_TABLE = /*__FIT__*/ {
    2: { kappa: 1.575, beta: 0.08, g0: 0.37788 },
    4: { kappa: 3.357, beta: 0.204, g0: 0.51862 },
    8: { kappa: 1.792, beta: 0.9, g0: 0.40371 },
    12: { kappa: 2.631, beta: 0.562, g0: 0.47721 },
    20: { kappa: 4.83, beta: 0.294, g0: 0.55275 },
  };
  // higher ratio positions drive the LED harder, so the photocell reacts faster (Ciletti)
  const LED_SPEED = { 2: 1.3, 4: 1.45, 8: 1.66, 12: 1.82, 20: 2.0 };
  // LED turn-on softness (diode knee). The 2:1 position is a deliberately soft knee (GroupDIY, M. Jacobs).
  const LED_KNEE = { 2: 0.42, 4: 0.16, 8: 0.11, 12: 0.1, 20: 0.09 };
  const VPK_FS = 10.96; // volts peak at 0 dBFS

  function dbToLin(db) { return Math.pow(10, db / 20); }

  class OptoLevelerEngine {
    constructor(sampleRate, numChannels) {
      this.fs = sampleRate;
      this.nch = numChannels || 2;
      this.params = {
        threshold: 0, ratio: 4, output: 5, range: 'high',
        character: 'stock', link: true, bypass: false,
      };
      this.k = {
        tauRise: 0.0100,    // photocell rise at LED turn-on, s
        riseLight: 0.15,    // brighter → faster rise
        tauRel: 0.068,      // recovery, fresh cell, s
        tauRelMem: 0.76,    // extra recovery after long limiting, s
        memUp: 1.6, memDn: 3.0,
        slewStock: 1.7, slewModded: 13,       // V/us (RC4136 vs TL072-class)
        gritStock: 0.023, gritModded: 0.004,  // op-amp HF non-linearity
        darkStock: 3.3, darkModded: 0.7,      // voiced HF loss under heavy GR (owner reports)
        weightDb: 0.5,                        // output transformer low-end weight
      };
      this._derive();
      this.reset();
    }

    reset() {
      const n = this.nch, z = () => new Float64Array(n);
      this.G = z(); this.mem = z(); this.gs = z();
      // sidechain history (y = attenuated, x = input) for 4x interpolated LED drive
      this.yh = new Float64Array(n * 4); this.xh = new Float64Array(n * 4);
      this.s1 = z(); this.s2 = z(); this.s3 = z();
      this.hp = z(); this.hpx = z();
      this.ls1 = z(); this.ls2 = z();
      this.lf = z(); this.dc_x = z(); this.dc_y = z();
      this.z1 = z(); this.z2 = z();
      this.shelf = new Array(n);
      for (let c = 0; c < n; c++) this.shelf[c] = this._shelfCoefs(0, false);
      this.lowShelf = this._shelfCoefs(this.k.weightDb, true);
      this.vu = 0; this.meterGR = 0; this.overload = false; this.ctr = 0;
      this.wet = this.params && this.params.bypass ? 0 : 1; this.outS = this.outGain || 1; this.idleBypass = false;
    }

    setParams(p) { Object.assign(this.params, p); this._derive(); }

    _derive() {
      const p = this.params, fs = this.fs, k = this.k;
      const R = RATIOS.includes(+p.ratio) ? +p.ratio : 4;
      this.R = R;
      const T = RATIO_TABLE[R];
      this.kappa = T.kappa; this.beta = T.beta; this.ledSpeed = LED_SPEED[R]; this.ledKnee = LED_KNEE[R]; this.ledOff = LED_KNEE[R] * Math.log1p(Math.exp(-1 / LED_KNEE[R]));
      const kn = Math.min(10, Math.max(0, +p.threshold));
      let thr = 10 - 5.6 * kn;                  // level for 1 dB GR; knob 0 → +10 dBFS (off)
      if (R === 2) thr -= 16;                   // manual: 2:1 lowers the threshold
      this.thr = thr;
      this.g = T.g0 * Math.pow(10, -thr / 20);
      this.sens = p.range === 'low' ? 10 : 1;
      this.outGain = dbToLin((Math.min(10, Math.max(0, +p.output)) - 5) * 4);
      const stock = p.character !== 'modded';
      this.slew = (stock ? k.slewStock : k.slewModded) * 1e6 / fs / VPK_FS;
      this.grit = stock ? k.gritStock : k.gritModded;
      this.darkMax = stock ? k.darkStock : k.darkModded;
      this.h2 = stock ? 0.004 : 0.0015;
      this.xfmr = 1.0;
      this.cHp = Math.exp(-2 * Math.PI * 1500 / fs);
      this.cGs = 1 - Math.exp(-1 / (0.03 * fs));
      this.cSmooth = 1 - Math.exp(-1 / (0.008 * fs));   // 8 ms parameter smoothing
      this.cVu = Math.exp(-1 / (0.065 * fs));
      this.cDc = Math.exp(-2 * Math.PI * 5 / fs);
      this.cLf = Math.exp(-2 * Math.PI * 90 / fs);
      this.memUp = 1 / (k.memUp * fs);
      this.memDn = 1 / (k.memDn * fs);
      this.relScale = 1 / (1 + 0.05 * (R - 2));
      this.lowShelf = this._shelfCoefs(k.weightDb, true);
    }

    // RBJ shelf: high shelf at 4.5 kHz, or low shelf at 110 Hz
    _shelfCoefs(dbG, low) {
      const fs = this.fs, f0 = low ? 110 : Math.min(4500, 0.4 * fs);
      const A = Math.pow(10, dbG / 40), w0 = 2 * Math.PI * f0 / fs;
      const cw = Math.cos(w0), sw = Math.sin(w0), S = 0.7;
      const alpha = sw / 2 * Math.sqrt((A + 1 / A) * (1 / S - 1) + 2), sA = 2 * Math.sqrt(A) * alpha;
      if (low) {
        const a0 = (A + 1) + (A - 1) * cw + sA;
        return {
          b0: A * ((A + 1) - (A - 1) * cw + sA) / a0, b1: 2 * A * ((A - 1) - (A + 1) * cw) / a0,
          b2: A * ((A + 1) - (A - 1) * cw - sA) / a0, a1: -2 * ((A - 1) + (A + 1) * cw) / a0,
          a2: ((A + 1) + (A - 1) * cw - sA) / a0,
        };
      }
      const a0 = (A + 1) - (A - 1) * cw + sA;
      return {
        b0: A * ((A + 1) + (A - 1) * cw + sA) / a0, b1: -2 * A * ((A - 1) + (A + 1) * cw) / a0,
        b2: A * ((A + 1) + (A - 1) * cw - sA) / a0, a1: 2 * ((A - 1) - (A + 1) * cw) / a0,
        a2: ((A + 1) - (A - 1) * cw - sA) / a0,
      };
    }

    _slew(x, prev) {
      const S = this.slew, d = x - prev;
      if (Math.abs(d) < 0.25 * S) return x;
      return prev + S * Math.tanh(d / S);
    }

    process(inputs, outputs, n) {
      const nch = Math.min(this.nch, inputs.length, outputs.length);
      // bypass is a short crossfade (no click); the engine keeps running so its state stays continuous
      const wetGoal = this.params.bypass ? 0 : 1;
      if (wetGoal === 0 && this.wet < 1e-6) {
        for (let c = 0; c < nch; c++) if (outputs[c] !== inputs[c]) outputs[c].set(inputs[c].subarray(0, n));
        this.wet = 0; this.meterGR = 0; this.idleBypass = true;
        return;
      }
      if (this.idleBypass) { this.reset(); this.wet = 0; this.idleBypass = false; }
      const cSm = this.cSmooth, outGoal = this.outGain;
      const dry = new Float64Array(nch);
      const k = this.k, fs = this.fs, sens = this.sens, invSens = 1 / sens;
      const link = this.params.link && nch > 1;
      const g = this.g, beta = this.beta, kappa = this.kappa;
      const cGs = this.cGs, cVu = this.cVu, cDc = this.cDc, cLf = this.cLf, cHp = this.cHp;
      const ls = this.lowShelf;
      let ovl = false, grMax = 0;
      const xs = new Float64Array(nch), drive = new Float64Array(nch * 4);
      const yh = this.yh, xh = this.xh;
      // Catmull-Rom between p1 and p2 at t (sidechain runs 1.5 samples behind, at 4x resolution)
      const cr = (p0, p1, p2, p3, t) => 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);

      for (let i = 0; i < n; i++) {
        // ---- input stage + sidechain drive ----
        for (let c = 0; c < nch; c++) {
          dry[c] = inputs[c][i];
          let x = dry[c] * sens;
          const a = Math.abs(x);
          if (a > 0.9) { ovl = true; x = Math.sign(x) * (0.9 + 0.1 * Math.tanh((a - 0.9) / 0.1)); }
          x = this._slew(x, this.s1[c]); this.s1[c] = x;            // U1/U2 differential input
          if (sens > 1) { x = this._slew(x, this.s2[c]); this.s2[c] = x; } // U3 high-gain stage
          xs[c] = x;
          const o = c * 4;
          for (let q = 0; q < 4; q++) {
            const t = q * 0.25;
            const yq = cr(yh[o + 3], yh[o + 2], yh[o + 1], yh[o], t);
            const xq = cr(xh[o + 3], xh[o + 2], xh[o + 1], xh[o], t);
            drive[o + q] = g * (Math.abs(yq) + beta * Math.abs(xq));
          }
          xh[o + 3] = xh[o + 2]; xh[o + 2] = xh[o + 1]; xh[o + 1] = xh[o]; xh[o] = x;
        }
        if (link) {           // strapped detectors: loudest channel drives both
          for (let q = 0; q < 4; q++) {
            let m = 0; for (let c = 0; c < nch; c++) if (drive[c * 4 + q] > m) m = drive[c * 4 + q];
            for (let c = 0; c < nch; c++) drive[c * 4 + q] = m;
          }
        }

        let vuIn = 0;
        for (let c = 0; c < nch; c++) {
          // ---- LED + photocell ----
          let gsum = 0, lsum = 0;
          const sk = this.ledKnee, isk = 1 / sk, off = this.ledOff;
          for (let q = 0; q < 4; q++) {
            const u = (drive[c * 4 + q] - 1) * isk;         // soft diode turn-on (softplus), zero light at zero drive
            let lq = (u > 20 ? u * sk : sk * Math.log1p(Math.exp(u))) - off;
            if (lq > 1e-6) { if (lq > 60) lq = 60; lsum += lq; gsum += kappa * Math.pow(lq, GAMMA); }
          }
          const l = lsum * 0.25, Gt = gsum * 0.25;
          let G = this.G[c];
          if (Gt > G) {
            let tau = k.tauRise / (1 + k.riseLight * l) / this.ledSpeed;
            if (tau < 0.0003) tau = 0.0003;
            G += (Gt - G) * (1 - Math.exp(-1 / (tau * fs)));
          } else {
            const tau = (k.tauRel + k.tauRelMem * Math.pow(this.mem[c], 1.3)) * this.relScale;
            G = Gt + (G - Gt) * Math.exp(-1 / (tau * fs));
          }
          this.G[c] = G;
          const gr = 20 * Math.log10(1 + G);
          if (gr > 1) {
            const goal = Math.min(1, gr / 4);
            if (goal > this.mem[c]) this.mem[c] += (goal - this.mem[c]) * this.memUp;
          } else this.mem[c] -= this.mem[c] * this.memDn;
          if (gr > grMax) grMax = gr;

          // ---- attenuator ----
          let y = xs[c] / (1 + G);
          const ho = c * 4;
          yh[ho + 3] = yh[ho + 2]; yh[ho + 2] = yh[ho + 1]; yh[ho + 1] = yh[ho]; yh[ho] = y;

          // ---- output op-amp: slew + HF grit (abrasive when hot) ----
          y = this._slew(y, this.s3[c]); this.s3[c] = y;
          const hpv = cHp * (this.hp[c] + y - this.hpx[c]);       // 1-pole high-pass 1.5 kHz
          this.hpx[c] = y; this.hp[c] = hpv;
          y -= this.grit * hpv * hpv * hpv;

          // voiced darkening with heavy gain reduction (owner reports)
          this.gs[c] += cGs * (gr - this.gs[c]);
          if ((this.ctr & 31) === 0) {
            const dv = Math.max(0, this.gs[c] - 0.8);
            this.shelf[c] = this._shelfCoefs(-this.darkMax * (1 - Math.exp(-dv / 4)), false);
          }
          const sh = this.shelf[c];
          const o = sh.b0 * y + this.z1[c];
          this.z1[c] = sh.b1 * y - sh.a1 * o + this.z2[c];
          this.z2[c] = sh.b2 * y - sh.a2 * o;
          y = o;

          // ---- output level, transformer weight + core saturation ----
          y *= invSens * this.outS;
          y += this.h2 * y * y;
          const lo = ls.b0 * y + this.ls1[c];
          this.ls1[c] = ls.b1 * y - ls.a1 * lo + this.ls2[c];
          this.ls2[c] = ls.b2 * y - ls.a2 * lo;
          y = lo;
          this.lf[c] = cLf * this.lf[c] + (1 - cLf) * y;
          const t = this.lf[c];
          y += this.xfmr * (Math.tanh(t * 0.8) / 0.8 - t);
          const dy = y - this.dc_x[c] + cDc * this.dc_y[c];
          this.dc_x[c] = y; this.dc_y[c] = dy; y = dy;
          const ay = Math.abs(y);
          if (ay > 1.2) y = Math.sign(y) * (1.2 + 0.38 * Math.tanh((ay - 1.2) / 0.38));

          outputs[c][i] = this.wet * y + (1 - this.wet) * dry[c];
          vuIn += Math.abs(y);
        }
        this.vu = cVu * this.vu + (1 - cVu) * (vuIn / nch);
        this.outS += (outGoal - this.outS) * cSm;           // output knob glides (no zipper noise)
        this.wet += (wetGoal - this.wet) * cSm;             // bypass crossfade
        this.ctr++;
      }
      this.meterGR = grMax;
      this.overload = ovl;
    }

    vuDb(mode) {
      const ref = mode === '+8' ? dbToLin(-12) : dbToLin(-16);
      return 20 * Math.log10(this.vu * 1.1107 / ref + 1e-9);
    }
    currentGR() { let m = 0; for (let c = 0; c < this.nch; c++) m = Math.max(m, 20 * Math.log10(1 + this.G[c])); return m; }
  }

  OptoLevelerEngine.RATIOS = RATIOS;
  OptoLevelerEngine.RATIO_TABLE = RATIO_TABLE;
  OptoLevelerEngine.VERSION = 3;
  if (typeof module !== 'undefined' && module.exports) module.exports = OptoLevelerEngine;
  else root.OptoLevelerEngine = OptoLevelerEngine;
})(typeof globalThis !== 'undefined' ? globalThis : this);
