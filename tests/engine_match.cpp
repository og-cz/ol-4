// Runs the C++ engine over the same input and settings as tests/js_reference.js and reports the max difference.
#include "../Source/OptoEngine.h"
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>

static std::vector<float> readF32(const std::string& path) {
    std::ifstream f(path, std::ios::binary); f.seekg(0, std::ios::end); size_t n = (size_t) f.tellg() / 4; f.seekg(0);
    std::vector<float> v(n); f.read((char*) v.data(), (std::streamsize) (n * 4)); return v;
}

struct Case { const char* name; ol4::Params p; double fs; int block; };

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    Case cases[] = {
        { "vocal_2to1", { 3.0, 2, 6.0, false, false, true, false }, 48000, 64 },
        { "lead_4to1",  { 5.2, 4, 6.0, false, false, true, false }, 48000, 128 },
        { "slam_20to1", { 8.0, 20, 7.0, false, false, true, false }, 44100, 512 },
        { "modded_low", { 3.5, 8, 5.0, true, true, false, false }, 96000, 32 },
        { "lead_8to1_96k", { 3.5, 8, 5.0, false, false, false, false }, 96000, 32 },
        { "bypass_mid", { 6.0, 12, 6.0, false, false, true, false }, 48000, 256 },
    };
    double worstAll = 0;
    for (auto& cs : cases) {
        auto L = readF32(dir + "/in_" + cs.name + "_L.f32"), Rr = readF32(dir + "/in_" + cs.name + "_R.f32");
        auto refL = readF32(dir + "/js_" + cs.name + "_L.f32"), refR = readF32(dir + "/js_" + cs.name + "_R.f32");
        const int n = (int) L.size();
        ol4::OptoEngine e; e.prepare(cs.fs, 2); e.setParams(cs.p);
        std::vector<float> oL(n), oR(n);
        for (int i = 0; i < n; i += cs.block) {
            const int m = std::min(cs.block, n - i);
            if (std::string(cs.name) == "bypass_mid" && i == (n / 2 / cs.block) * cs.block) { auto q = cs.p; q.bypass = true; e.setParams(q); }
            const float* in[2] = { L.data() + i, Rr.data() + i }; float* out[2] = { oL.data() + i, oR.data() + i };
            e.process(in, out, m);
        }
        double worst = 0;
        for (int i = 0; i < n; ++i) { worst = std::max(worst, (double) std::abs(oL[i] - refL[i])); worst = std::max(worst, (double) std::abs(oR[i] - refR[i])); }
        std::printf("%-12s fs %6.0f block %4d  max |C++ - JS| = %.3e\n", cs.name, cs.fs, cs.block, worst);
        worstAll = std::max(worstAll, worst);
    }
    std::printf("WORST %.3e\n", worstAll);
    return worstAll < 1e-5 ? 0 : 1;
}
