// Loads the built OL-4 VST3 like a DAW does, runs the reference input through it with the same
// settings as tests/js_reference.js, and compares the plugin's output with the JS engine.
// Also opens the editor off-screen and saves a snapshot PNG.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>
#include <iostream>

using namespace juce;

static std::vector<float> readF32 (const File& f)
{
    MemoryBlock mb; f.loadFileAsData (mb);
    std::vector<float> v (mb.getSize() / 4); std::memcpy (v.data(), mb.getData(), v.size() * 4); return v;
}

struct Case { const char* name; float thr; int ratioIdx; float out; bool low; bool modded; bool link; double fs; int block; };

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::cerr << "usage: HostTest <plugin.vst3> <refdir> [snapshot.png]\n"; return 2; }
    const File pluginFile { String (argv[1]) }, refDir { String (argv[2]) };

    AudioPluginFormatManager fm; addDefaultFormatsToManager (fm);
    OwnedArray<PluginDescription> descs;
    for (auto* fmt : fm.getFormats()) fmt->findAllTypesForFile (descs, pluginFile.getFullPathName());
    if (descs.isEmpty()) { std::cerr << "could not scan plugin\n"; return 3; }
    std::cout << "Loaded: " << descs[0]->name << " (" << descs[0]->pluginFormatName << "), "
              << descs[0]->manufacturerName << ", category " << descs[0]->category << "\n";

    Case cases[] = {
        { "vocal_2to1", 3.0f, 0, 6.0f, false, false, true, 48000, 64 },
        { "lead_4to1",  5.2f, 1, 6.0f, false, false, true, 48000, 128 },
        { "slam_20to1", 8.0f, 4, 7.0f, false, false, true, 44100, 512 },
        { "lead_8to1_96k", 3.5f, 2, 5.0f, false, false, false, 96000, 32 },
    };
    double worstAll = 0;
    for (auto& cs : cases)
    {
        String err;
        auto inst = fm.createPluginInstance (*descs[0], cs.fs, cs.block, err);
        if (! inst) { std::cerr << "instantiate failed: " << err << "\n"; return 4; }
        auto setParam = [&] (const String& id, float plainOrIndex, bool isChoice, int nChoices)
        {
            for (auto* p : inst->getParameters())
                if (auto* hp = dynamic_cast<HostedAudioProcessorParameter*> (p))
                    if (hp->getName (64) == id)
                    {
                        float v01 = isChoice ? (nChoices > 1 ? plainOrIndex / (float) (nChoices - 1) : 0.0f) : plainOrIndex / 10.0f;
                        if (id == "Stereo Link" || id == "Power") v01 = plainOrIndex;
                        hp->setValue (v01);
                    }
        };
        setParam ("Threshold", cs.thr, false, 0);
        setParam ("Output Level", cs.out, false, 0);
        setParam ("Ratio", (float) cs.ratioIdx, true, 5);
        setParam ("Stereo Link", cs.link ? 1.0f : 0.0f, false, 0);
        setParam ("Power", 1.0f, false, 0);
        inst->setPlayConfigDetails (2, 2, cs.fs, cs.block);
        inst->prepareToPlay (cs.fs, cs.block);

        auto L = readF32 (refDir.getChildFile (String ("in_") + cs.name + "_L.f32"));
        auto R = readF32 (refDir.getChildFile (String ("in_") + cs.name + "_R.f32"));
        auto rL = readF32 (refDir.getChildFile (String ("js_") + cs.name + "_L.f32"));
        auto rR = readF32 (refDir.getChildFile (String ("js_") + cs.name + "_R.f32"));
        const int n = (int) L.size();
        AudioBuffer<float> buf (2, cs.block); MidiBuffer midi;
        double worst = 0; int firstBad = -1; int latency = inst->getLatencySamples();
        { String ps; for (auto* pp : inst->getParameters()) if (auto* hp = dynamic_cast<HostedAudioProcessorParameter*> (pp)) ps << hp->getName (64) << "=" << hp->getCurrentValueAsText() << "  "; std::cout << "  " << ps << "\n"; }
        for (int i = 0; i < n; i += cs.block)
        {
            const int m = jmin (cs.block, n - i);
            buf.setSize (2, m, false, false, true);
            buf.copyFrom (0, 0, L.data() + i, m); buf.copyFrom (1, 0, R.data() + i, m);
            inst->processBlock (buf, midi);
            for (int k = 0; k < m; ++k)
            {
                const double d = jmax (std::abs (buf.getSample (0, k) - rL[(size_t) (i + k)]), std::abs (buf.getSample (1, k) - rR[(size_t) (i + k)]));
                if (d > 1e-4 && firstBad < 0) { firstBad = i + k; std::cout << "  first diff at sample " << firstBad << ": plugin " << buf.getSample (0, k) << " ref " << rL[(size_t) (i + k)] << " in " << L[(size_t) (i + k)] << "\n"; }
                worst = jmax (worst, d);
            }
        }
        inst->releaseResources();
        std::cout << String (cs.name).paddedRight (' ', 12) << " fs " << cs.fs << " block " << cs.block
                  << "  latency " << latency << "  max |plugin - reference| = " << worst << "\n";
        worstAll = jmax (worstAll, worst);

        if (argc > 3 && String (cs.name) == "lead_4to1")
        {
            // editor snapshot
            if (auto* ed = inst->createEditorAndMakeActive())
            {
                ed->setSize (1080, 445);
                MessageManager::getInstance()->runDispatchLoopUntil (300);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                File out { String (argv[3]) }; out.deleteFile();
                FileOutputStream os (out); PNGImageFormat().writeImageToStream (img, os);
                std::cout << "Editor snapshot: " << img.getWidth() << "x" << img.getHeight() << " -> " << out.getFullPathName() << "\n";
                delete ed;
            }
        }
    }
    std::cout << "WORST " << worstAll << "\n";
    return worstAll < 1e-5 ? 0 : 1;
}
