// Renders the plugin editor in-process (same code as the plugin) and saves PNG snapshots:
// default state, and a second state with other switch positions, while audio drives the meter.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

using namespace juce;

static void setP (OL4Processor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static void pump (OL4Processor& proc, double seconds, float level)
{
    // feed audio through the processor so the meter has something to show, while the UI timer runs
    const double sr = 48000; const int block = 512;
    AudioBuffer<float> buf (2, block); MidiBuffer midi;
    const double end = Time::getMillisecondCounterHiRes() + seconds * 1000.0; double ph = 0;
    while (Time::getMillisecondCounterHiRes() < end)
    {
        for (int i = 0; i < block; ++i)
        {
            const float s = level * (float) std::sin (ph); ph += 2 * MathConstants<double>::pi * 220.0 / sr;
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        proc.processBlock (buf, midi);
        MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}

static void shot (Component& c, const String& path)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    File f { path }; f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
    std::cout << "saved " << img.getWidth() << "x" << img.getHeight() << " " << path << "\n";
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const String dir = argc > 1 ? String (argv[1]) : String (".");
    OL4Processor proc;
    proc.setPlayConfigDetails (2, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    std::unique_ptr<AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1080, 445);
    ed->setVisible (true);

    setP (proc, "threshold", 6.0f);
    pump (proc, 3.0, 0.35f);
    shot (*ed, dir + "/plugin-default.png");

    setP (proc, "ratio", 0); setP (proc, "meter", 1); setP (proc, "link", 0);
    setP (proc, "threshold", 2.5f); setP (proc, "output", 7.5f);
    pump (proc, 1.5, 0.05f);
    shot (*ed, dir + "/plugin-alt.png");

    setP (proc, "power", 0);
    pump (proc, 2.0, 0.05f);
    shot (*ed, dir + "/plugin-off.png");
    ed = nullptr;
    return 0;
}
