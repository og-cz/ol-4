#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <map>

/*  The rendered OL-4 faceplate.
    Everything is laid out in the coordinates of the 2560 x 890 render (10 px per mm), plus a
    the whole editor is scaled as one picture.                       */
class OL4Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OL4Editor (OL4Processor&);
    ~OL4Editor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int kImgW = 2160, kPlateH = 890, kImgH = kPlateH;

private:
    enum class Ctl { None, Threshold, Output, Ratio, Meter, Power, Stereo };

    void timerCallback() override;
    Ctl hit (juce::Point<float> img) const;
    juce::Point<float> toImg (juce::Point<float> p) const { return p / scale; }
    juce::RangedAudioParameter* param (Ctl) const;
    void stepDiscrete (Ctl, int delta);
    void toggle (Ctl);
    void showTip (Ctl);
    juce::String tipText (Ctl) const;
    float value (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }

    void renderStatic (juce::Graphics&);   // plate + state overlays + knobs (cached)
    void drawMeter (juce::Graphics&);
    void drawLamps (juce::Graphics&);
    void drawTip (juce::Graphics&);
    void drawImageAt (juce::Graphics&, const juce::Image&, float x, float y);

    OL4Processor& processor;
    float scale = 0.5f;

    // art + layout (image px)
    juce::Image plate, knobSheet, paper;
    std::map<juce::String, juce::Image> crops;
    std::map<juce::String, juce::Rectangle<int>> cropBox;
    juce::Point<float> cThr, cOut, cTogSt, cTogPwr, cLedPwr, cLedOvl;
    juce::Rectangle<float> face, meterOpen;
    float ppm = 10, skirtR = 156, knobR = 96;
    juce::Rectangle<int> knobBox;             // relative to knob centre
    int frames = 101, sCols = 11, sRows = 10, fW = 232, fH = 232;

    // cached static layer
    juce::Image cache; juce::String cacheKey;

    // meter + lamps
    double needle = -0.05, needleV = 0, lamp = 0, ovlHold = 0;
    int lastOvl = 0; double lastTime = 0;

    // interaction
    Ctl dragCtl = Ctl::None, hoverCtl = Ctl::None, tipCtl = Ctl::None;
    float dragStartValue = 0; float dragStartY = 0; int dragSteps = 0; bool dragMoved = false;
    double tipUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OL4Editor)
};
