#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout OL4Processor::createLayout()
{
    using namespace juce;
    APVTS::ParameterLayout l;
    auto knob = [] (float v, int) { return String (v, 1); };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "threshold", 1 }, "Threshold",
                                                  NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (knob)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output Level",
                                                  NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f,
                                                  AudioParameterFloatAttributes()
                                                      .withStringFromValueFunction ([] (float v, int) { auto d = (v - 5.0f) * 4.0f; return (d > 0 ? "+" : "") + String (d, 1) + " dB"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "ratio", 1 }, "Ratio", StringArray { "2:1", "4:1", "8:1", "12:1", "20:1" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "meter", 1 }, "Meter", StringArray { "GR", "+4", "+8" }, 0,
                                                   AudioParameterChoiceAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Stereo Link", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "power", 1 }, "Power", true));
    return l;
}

OL4Processor::OL4Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OL4", createLayout())
{
    pThreshold = apvts.getRawParameterValue ("threshold");
    pOutput = apvts.getRawParameterValue ("output");
    pRatio = apvts.getRawParameterValue ("ratio");
    pLink = apvts.getRawParameterValue ("link");
    pPower = apvts.getRawParameterValue ("power");
}

bool OL4Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void OL4Processor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    hasLast = false;
    setLatencySamples (0);
}

void OL4Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    for (int c = nIn; c < nOut; ++c) buffer.clear (c, 0, buffer.getNumSamples());

    ol4::Params p;
    p.threshold = pThreshold->load();
    p.output = pOutput->load();
    p.ratio = kRatios[juce::jlimit (0, 4, (int) std::lround (pRatio->load()))];
    p.rangeLow = false;   // line-level input, stock RC4136 op-amps (the classic unit)
    p.modded = false;
    p.link = pLink->load() > 0.5f;
    p.bypass = pPower->load() < 0.5f;
    if (! hasLast || ! juce::exactlyEqual (p.threshold, lastParams.threshold) || ! juce::exactlyEqual (p.output, lastParams.output) || p.ratio != lastParams.ratio
        || p.rangeLow != lastParams.rangeLow || p.modded != lastParams.modded || p.link != lastParams.link || p.bypass != lastParams.bypass)
    {
        engine.setParams (p);
        lastParams = p; hasLast = true;
    }

    const int nch = juce::jmin (buffer.getNumChannels(), nOut);
    if (nch <= 0) return;
    engine.process (buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), buffer.getNumSamples());

    grDb.store ((float) (p.bypass ? 0.0 : engine.currentGR()));
    vu4.store ((float) engine.vuDb (4));
    vu8.store ((float) engine.vuDb (8));
    if (engine.overloaded()) overloadCount.fetch_add (1);
}

juce::AudioProcessorEditor* OL4Processor::createEditor() { return new OL4Editor (*this); }

void OL4Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", editorScale100.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void OL4Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            if (vt.hasProperty ("uiScale")) editorScale100.store ((int) vt.getProperty ("uiScale"));
            apvts.replaceState (vt);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OL4Processor(); }
