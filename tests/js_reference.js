// Generates test input and the JS engine's output for tests/engine_match.cpp
const fs = require('fs'), path = require('path');
const Engine = require(process.argv[2] || require('path').join(__dirname, 'reference', 'opto-engine.js'));
const out = process.argv[3] || 'build/ref';
fs.mkdirSync(out, { recursive: true });
const cases = [
  { name: 'vocal_2to1', p: { threshold: 3.0, ratio: 2, output: 6.0, range: 'high', character: 'stock', link: true }, fs: 48000, block: 64 },
  { name: 'lead_4to1', p: { threshold: 5.2, ratio: 4, output: 6.0, range: 'high', character: 'stock', link: true }, fs: 48000, block: 128 },
  { name: 'slam_20to1', p: { threshold: 8.0, ratio: 20, output: 7.0, range: 'high', character: 'stock', link: true }, fs: 44100, block: 512 },
  { name: 'modded_low', p: { threshold: 3.5, ratio: 8, output: 5.0, range: 'low', character: 'modded', link: false }, fs: 96000, block: 32 },
  { name: 'lead_8to1_96k', p: { threshold: 3.5, ratio: 8, output: 5.0, range: 'high', character: 'stock', link: false }, fs: 96000, block: 32 },
  { name: 'bypass_mid', p: { threshold: 6.0, ratio: 12, output: 6.0, range: 'high', character: 'stock', link: true }, fs: 48000, block: 256 },
];
let seed = 11; const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296) * 2 - 1;
for (const c of cases) {
  const n = Math.round(c.fs * 2.5), L = new Float32Array(n), R = new Float32Array(n);
  const lowAmp = c.p.range === 'low' ? 0.08 : 1;
  for (let i = 0; i < n; i++) {
    const t = i / c.fs, env = 0.05 + 0.6 * Math.pow(Math.max(0, Math.sin(2 * Math.PI * 0.9 * t)), 2);
    L[i] = lowAmp * env * (0.6 * Math.sin(2 * Math.PI * 180 * t) + 0.3 * Math.sin(2 * Math.PI * 2300 * t) + 0.2 * rnd());
    R[i] = lowAmp * env * (0.5 * Math.sin(2 * Math.PI * 220 * t + 1) + 0.25 * rnd());
  }
  const e = new Engine(c.fs, 2); e.setParams(c.p);
  const oL = new Float32Array(n), oR = new Float32Array(n);
  for (let i = 0; i < n; i += c.block) {
    const m = Math.min(c.block, n - i);
    if (c.name === 'bypass_mid' && i === Math.floor(n / 2 / c.block) * c.block) e.setParams({ bypass: true });
    e.process([L.subarray(i, i + m), R.subarray(i, i + m)], [oL.subarray(i, i + m), oR.subarray(i, i + m)], m);
  }
  const w = (a, f) => fs.writeFileSync(path.join(out, f), Buffer.from(a.buffer));
  w(L, `in_${c.name}_L.f32`); w(R, `in_${c.name}_R.f32`); w(oL, `js_${c.name}_L.f32`); w(oR, `js_${c.name}_R.f32`);
}
console.log('reference written to', out);
