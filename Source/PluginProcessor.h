#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "OptoEngine.h"
#include <atomic>

class OL4Processor final : public juce::AudioProcessor
{
public:
    OL4Processor();
    ~OL4Processor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OL-4 Optical Leveler"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // read by the editor (meters)
    std::atomic<float> grDb { 0.0f }, vu4 { -60.0f }, vu8 { -60.0f };
    std::atomic<int> overloadCount { 0 };

    static constexpr int kRatios[5] = { 2, 4, 8, 12, 20 };

private:
    ol4::OptoEngine engine;
    ol4::Params lastParams;
    bool hasLast = false;

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pRatio = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pPower = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OL4Processor)
};
