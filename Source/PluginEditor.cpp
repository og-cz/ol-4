#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    Image loadArt (const char* name)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (name, size))
            return ImageCache::getFromMemory (data, size);
        return {};
    }
    Point<float> pt (const var& v) { return { (float) (double) v[0], (float) (double) v[1] }; }
    const Colour kRack (0xff121415);
    constexpr double kVuMin = -20.0, kVuMax = 3.0;
    double vuPos (double db)
    {
        const double v = std::pow (10.0, jlimit (-30.0, 6.0, db) / 20.0);
        const double a = std::pow (10.0, kVuMin / 20.0), b = std::pow (10.0, kVuMax / 20.0);
        return (v - a) / (b - a);
    }
}

OL4Editor::OL4Editor (OL4Processor& p) : AudioProcessorEditor (&p), processor (p)
{
    plate = loadArt ("plate_jpg");
    knobSheet = loadArt ("knob_png");

    int lsize = 0;
    const auto* ljson = BinaryData::getNamedResource ("layout_json", lsize);
    const var L = JSON::parse (String::fromUTF8 (ljson, lsize));
    ppm = (float) (double) L["ppm"];
    cThr = pt (L["knob_thr"]); cOut = pt (L["knob_out"]);
    cTogSt = pt (L["tog_st"]); cTogPwr = pt (L["tog_pwr"]);
    cLedPwr = pt (L["led_pwr"]); cLedOvl = pt (L["led_ovl"]);
    const var F = L["face"];
    face = { (float) (double) F[0], (float) (double) F[1], (float) ((double) F[2] - (double) F[0]), (float) ((double) F[3] - (double) F[1]) };
    const var mm = L["mm"];
    const var mo = mm["meter_open"]; const float ear = (float) (double) L["ear"];
    meterOpen = { ((float) (double) mo[0] + ear) * ppm, (float) (double) mo[1] * ppm,
                  (float) ((double) mo[2] - (double) mo[0]) * ppm, (float) ((double) mo[3] - (double) mo[1]) * ppm };
    skirtR = (float) (double) mm["skirt_r"] * ppm;
    knobR = (float) (double) mm["knob_r"] * ppm;
    const var kb = L["knob_box"];
    knobBox = { (int) kb[0], (int) kb[1], (int) kb[2], (int) kb[3] };
    frames = (int) L["knob_frames"];
    const var sp = L["sprite"];
    sCols = (int) sp["cols"]; sRows = (int) sp["rows"]; fW = (int) sp["fw"]; fH = (int) sp["fh"];
    if (auto* cr = L["crops"].getDynamicObject())
        for (auto& prop : cr->getProperties())
        {
            const var b = prop.value;
            const String name = prop.name.toString();
            cropBox[name] = { (int) b[0], (int) b[1], (int) b[2] - (int) b[0], (int) b[3] - (int) b[1] };
            crops[name] = loadArt (("crop_" + name + "_png").toRawUTF8());
        }


    // meter paper grain
    paper = Image (Image::ARGB, 380, 152, true);
    {
        Image::BitmapData bd (paper, Image::BitmapData::writeOnly);
        Random r (9);
        for (int y = 0; y < paper.getHeight(); ++y)
            for (int x = 0; x < paper.getWidth(); ++x)
            {
                const auto v = (uint8) (128 + (r.nextFloat() - 0.5f) * 34);
                bd.setPixelColour (x, y, Colour (v, v, v, (uint8) 22));
            }
    }

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kImgW / kImgH);
    setResizeLimits (kImgW * 30 / 100, kImgH * 30 / 100, kImgW * 110 / 100, kImgH * 110 / 100);
    const int s100 = 80;
    setSize (kImgW * s100 / 100, kImgH * s100 / 100);
    lamp = 0.0;
    lastTime = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

void OL4Editor::resized()
{
    scale = (float) getWidth() / (float) kImgW;
    cache = {};
}

// ---------------------------------------------------------------------------------------------- params
RangedAudioParameter* OL4Editor::param (Ctl c) const
{
    auto& v = processor.apvts;
    switch (c)
    {
        case Ctl::Threshold: return v.getParameter ("threshold");
        case Ctl::Output:    return v.getParameter ("output");
        case Ctl::Ratio:     return v.getParameter ("ratio");
        case Ctl::Meter:     return v.getParameter ("meter");
        case Ctl::Power:     return v.getParameter ("power");
        case Ctl::Stereo:    return v.getParameter ("link");
        case Ctl::None:      break;
    }
    return nullptr;
}

OL4Editor::Ctl OL4Editor::hit (Point<float> q) const
{
    auto in = [&] (Point<float> c, float r) { return q.getDistanceFrom (c) <= r; };
    if (in (cThr, knobR)) return Ctl::Threshold;
    if (in (cOut, knobR)) return Ctl::Output;
    if (in (cThr, skirtR)) return Ctl::Ratio;
    if (in (cOut, skirtR)) return Ctl::Meter;
    if (Rectangle<float> (100, 160).withCentre (cTogPwr).contains (q)) return Ctl::Power;
    if (Rectangle<float> (170, 100).withCentre (cTogSt).contains (q)) return Ctl::Stereo;
    return Ctl::None;
}

void OL4Editor::stepDiscrete (Ctl c, int delta)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (param (c)))
    {
        const int n = p->choices.size();
        const int idx = jlimit (0, n - 1, p->getIndex() + delta);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
        p->endChangeGesture();
    }
}

void OL4Editor::toggle (Ctl c)
{
    if (auto* p = param (c))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

String OL4Editor::tipText (Ctl c) const
{
    const int ratio = OL4Processor::kRatios[jlimit (0, 4, roundToInt (value ("ratio")))];
    switch (c)
    {
        case Ctl::Threshold:
        {
            const float k = value ("threshold");
            if (k < 0.05f) return "Threshold off";
            double t = 10.0 - 5.6 * k; if (ratio == 2) t -= 16;
            return "Threshold " + String (k, 1) + "  (" + String (t, 1) + " dBFS)";
        }
        case Ctl::Output:
        {
            const float d = (value ("output") - 5.0f) * 4.0f;
            return "Output " + String (value ("output"), 1) + "  (" + (d > 0 ? "+" : "") + String (d, 1) + " dB)";
        }
        case Ctl::Ratio: return "Ratio " + String (ratio) + ":1" + (ratio >= 8 ? "  (limit)" : "");
        case Ctl::Meter:
        {
            const int m = roundToInt (value ("meter"));
            return m == 0 ? String ("Meter: gain reduction") : String ("Meter: output, 0 VU = ") + (m == 1 ? "+4" : "+8") + " dBu";
        }
        case Ctl::None: case Ctl::Power: case Ctl::Stereo: break;
    }
    return {};
}

void OL4Editor::showTip (Ctl c) { tipCtl = c; tipUntil = Time::getMillisecondCounterHiRes() * 0.001 + 1.2; }

// ---------------------------------------------------------------------------------------------- mouse
void OL4Editor::mouseMove (const MouseEvent& e)
{
    const auto c = hit (toImg (e.position));
    if (c != hoverCtl)
    {
        hoverCtl = c;
        setMouseCursor (c == Ctl::None ? MouseCursor::NormalCursor
                        : (c == Ctl::Threshold || c == Ctl::Output) ? MouseCursor::UpDownResizeCursor
                        : MouseCursor::PointingHandCursor);
    }
}

void OL4Editor::mouseExit (const MouseEvent&) { hoverCtl = Ctl::None; }

void OL4Editor::mouseDown (const MouseEvent& e)
{
    dragCtl = hit (toImg (e.position));
    dragStartY = e.position.y; dragMoved = false; dragSteps = 0;
    if (dragCtl == Ctl::Threshold || dragCtl == Ctl::Output)
    {
        auto* p = param (dragCtl);
        dragStartValue = p->convertFrom0to1 (p->getValue());
        p->beginChangeGesture();
        showTip (dragCtl);
    }
    else if (dragCtl == Ctl::Ratio || dragCtl == Ctl::Meter) showTip (dragCtl);
}

void OL4Editor::mouseDrag (const MouseEvent& e)
{
    const float dy = dragStartY - e.position.y;
    if (std::abs (dy) > 2.0f) dragMoved = true;
    if (dragCtl == Ctl::Threshold || dragCtl == Ctl::Output)
    {
        auto* p = param (dragCtl);
        const float perPixel = 10.0f / (e.mods.isShiftDown() ? 800.0f : 200.0f);
        const float v = jlimit (0.0f, 10.0f, dragStartValue + dy * perPixel);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        showTip (dragCtl);
    }
    else if (dragCtl == Ctl::Ratio || dragCtl == Ctl::Meter)
    {
        const int steps = (int) (dy / 26.0f);
        if (steps != dragSteps) { stepDiscrete (dragCtl, steps - dragSteps); dragSteps = steps; }
        showTip (dragCtl);
    }
}

void OL4Editor::mouseUp (const MouseEvent& e)
{
    const auto c = dragCtl;
    dragCtl = Ctl::None;
    if (c == Ctl::Threshold || c == Ctl::Output) { param (c)->endChangeGesture(); return; }
    if (dragMoved) return;
    if (hit (toImg (e.position)) != c) return;
    if (c == Ctl::Ratio || c == Ctl::Meter)
    {
        auto* p = dynamic_cast<AudioParameterChoice*> (param (c));
        const int n = p->choices.size();
        stepDiscrete (c, p->getIndex() == n - 1 ? -(n - 1) : 1);    // click walks through the positions
        showTip (c);
    }
    else if (c != Ctl::None) toggle (c);
}

void OL4Editor::mouseDoubleClick (const MouseEvent& e)
{
    const auto c = hit (toImg (e.position));
    if (c == Ctl::Threshold || c == Ctl::Output)
    {
        auto* p = param (c);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
        showTip (c);
    }
}

// ---------------------------------------------------------------------------------------------- timer
void OL4Editor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.25, now - lastTime); lastTime = now;
    const bool power = value ("power") > 0.5f;

    lamp += ((power ? 1.0 : 0.0) - lamp) * (1 - std::exp (-dt * (power ? 1.5 : 3.0)));   // incandescent lamp

    const int m = roundToInt (value ("meter"));
    double target;
    if (! power || lamp < 0.3) target = -0.05;
    else if (m == 0) target = vuPos (-processor.grDb.load());
    else target = vuPos (m == 1 ? processor.vu4.load() : processor.vu8.load());
    const int steps = jmax (1, (int) std::ceil (dt / 0.004)); const double h = dt / steps;
    for (int i = 0; i < steps; ++i)          // VU ballistics: ~300 ms to 99 %, ~1.5 % overshoot
    {
        const double wn = 19.0, z = 0.8;
        const double acc = wn * wn * (target - needle) - 2 * z * wn * needleV;
        needleV += acc * h; needle += needleV * h;
        if (needle < -0.05) { needle = -0.05; needleV = std::abs (needleV) * 0.25; }
        if (needle > 1.07) { needle = 1.07; needleV = -std::abs (needleV) * 0.25; }
    }
    const int oc = processor.overloadCount.load();
    if (oc != lastOvl) { lastOvl = oc; ovlHold = now + 0.22; }
    repaint();
}

// ---------------------------------------------------------------------------------------------- paint
void OL4Editor::drawImageAt (Graphics& g, const Image& im, float x, float y)
{
    if (im.isValid()) g.drawImage (im, Rectangle<float> (x, y, (float) im.getWidth(), (float) im.getHeight()));
}

void OL4Editor::renderStatic (Graphics& g)
{
    g.fillAll (kRack);
    drawImageAt (g, plate, 0, 0);
    auto overlay = [&] (const String& name)
    {
        auto it = crops.find (name); auto b = cropBox.find (name);
        if (it != crops.end() && b != cropBox.end()) drawImageAt (g, it->second, (float) b->second.getX(), (float) b->second.getY());
    };
    overlay ("skirt_thr_" + String (roundToInt (value ("ratio"))));
    overlay ("skirt_out_" + String (roundToInt (value ("meter"))));
    overlay (String ("tog_pwr_") + (value ("power") > 0.5f ? "1" : "0"));
    overlay (String ("tog_st_") + (value ("link") > 0.5f ? "1" : "0"));
    overlay (String ("led_pwr_") + (value ("power") > 0.5f ? "1" : "0"));

    auto knob = [&] (Point<float> c, float v)
    {
        const int f = jlimit (0, frames - 1, roundToInt (v / 10.0f * (float) (frames - 1)));
        const int sx = (f % sCols) * fW, sy = (f / sCols) * fH;
        g.drawImage (knobSheet, (int) (c.x + (float) knobBox.getX()), (int) (c.y + (float) knobBox.getY()), fW, fH, sx, sy, fW, fH);
    };
    knob (cThr, value ("threshold"));
    knob (cOut, value ("output"));
}

void OL4Editor::drawMeter (Graphics& g)
{
    const float W = face.getWidth(), H = face.getHeight(), u = H / 100.0f;
    const double L = lamp;
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (face.toNearestInt());
    g.addTransform (AffineTransform::translation (face.getX(), face.getY()));
    auto mix = [&] (Colour a, Colour b) { return a.interpolatedWith (b, (float) L); };
    ColourGradient lit (mix (Colour (64, 52, 34), Colour (255, 236, 176)), W / 2, H * 1.25f,
                        mix (Colour (40, 33, 22), Colour (214, 150, 66)), W / 2 + W * 0.62f, H * 0.6f, true);
    lit.addColour (0.55, mix (Colour (56, 46, 30), Colour (246, 204, 120)));
    g.setGradientFill (lit); g.fillRect (0.0f, 0.0f, W, H);
    g.drawImage (paper, Rectangle<float> (0, 0, W, H));

    const float cx = W / 2, cy = H * 1.95f, R = H * 1.6f;
    const double SW = 37.0 * MathConstants<double>::pi / 180.0;
    auto ang = [&] (double p) { return -MathConstants<double>::halfPi - SW + p * 2 * SW; };      // canvas angle
    auto at = [&] (double a, float r) { return Point<float> (cx + (float) std::cos (a) * r, cy + (float) std::sin (a) * r); };
    auto jang = [&] (double a) { return (float) (a + MathConstants<double>::halfPi); };        // JUCE arc angle
    const Colour ink = Colour (26, 20, 12).withAlpha ((float) (0.5 + 0.45 * L));
    const Colour red = Colour (196, 44, 22).withAlpha ((float) (0.55 + 0.45 * L));

    Path band;
    band.addCentredArc (cx, cy, R, R, 0, jang (ang (vuPos (0))), jang (ang (1)), true);
    band.addCentredArc (cx, cy, R - 7 * u, R - 7 * u, 0, jang (ang (1)), jang (ang (vuPos (0))), false);
    band.closeSubPath();
    g.setColour (red); g.fillPath (band);
    Path arc1; arc1.addCentredArc (cx, cy, R, R, 0, jang (ang (0)), jang (ang (vuPos (0))), true);
    g.setColour (ink); g.strokePath (arc1, PathStrokeType (0.9f * u));
    Path arc2; arc2.addCentredArc (cx, cy, R, R, 0, jang (ang (vuPos (0))), jang (ang (1)), true);
    g.setColour (red); g.strokePath (arc2, PathStrokeType (0.9f * u));

    const int majors[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    auto isMajor = [&] (double db) { for (int mj : majors) if (std::abs (db - mj) < 1e-6) return true; return false; };
    for (double db = -20; db <= 3.001; db += (db < -10 ? 5 : db < -5 ? 1 : 0.5))
    {
        const double a = ang (vuPos (db)); const bool mj = isMajor (db);
        g.setColour (db > 0 ? red : ink);
        g.drawLine (Line<float> (at (a, R), at (a, R + (mj ? 8 : 4.5f) * u)), (mj ? 1.1f : 0.7f) * u);
    }
    g.setFont (FontOptions (10.5f * u));
    for (int db : majors)
    {
        const auto p = at (ang (vuPos (db)), R + 16 * u);
        g.setColour (db > 0 ? red : ink);
        g.drawText ((db > 0 ? "+" : "") + String (db), Rectangle<float> (40 * u, 14 * u).withCentre (p), Justification::centred);
    }
    g.setFont (FontOptions (8.8f * u)); g.setColour (ink);
    for (int pc : { 0, 20, 40, 60, 80, 100 })
    {
        const double a = ang (jmax (0.0, vuPos (pc == 0 ? -30.0 : 20 * std::log10 (pc / 100.0))));
        g.fillEllipse (Rectangle<float> (1.1f * u, 1.1f * u).withCentre (at (a, R - 3.2f * u)));
        auto p = at (a, R - 11 * u); if (pc == 0) p.x -= 3 * u;
        g.drawText (String (pc), Rectangle<float> (30 * u, 12 * u).withCentre (p), Justification::centred);
    }
    g.setFont (FontOptions (12.0f * u));
    g.setColour (ink); g.drawText ("VU", Rectangle<float> (30 * u, 16 * u).withCentre ({ W * 0.12f, H * 0.2f }), Justification::centred);
    g.setColour (red); g.drawText ("VU", Rectangle<float> (30 * u, 16 * u).withCentre ({ W * 0.88f, H * 0.2f }), Justification::centred);
    g.setColour (Colour (26, 20, 12).withAlpha ((float) (0.35 + 0.35 * L)));
    g.setFont (FontOptions (12.0f * u, Font::bold | Font::italic));
    g.drawText ("OL", Rectangle<float> (40 * u, 16 * u).withCentre ({ cx, H * 0.78f }), Justification::centred);

    const double a = ang (needle);
    const auto p1 = at (a, R * 0.5f), p2 = at (a, R + 10 * u);
    g.setColour (Colour (40, 24, 6).withAlpha ((float) (0.14 + 0.14 * L)));
    g.drawLine (Line<float> (p1 + Point<float> (3 * u, 4 * u), p2 + Point<float> (3 * u, 4 * u)), 1.6f * u);
    g.setColour (Colour (0xff121110)); g.drawLine (Line<float> (p1, p2), 0.95f * u);

    ColourGradient vg (Colours::transparentBlack, W / 2, H * 0.62f, Colour (30, 16, 0).withAlpha ((float) (0.28 + 0.2 * (1 - L))), W / 2 + W * 0.62f, H * 0.62f, true);
    g.setGradientFill (vg); g.fillRect (0.0f, 0.0f, W, H);

}

void OL4Editor::drawLamps (Graphics& g)
{
    // glass reflection and inner shadow on the meter window
    {
        const auto r = meterOpen;
        ColourGradient sh (Colours::black.withAlpha (0.55f), 0, r.getY(), Colours::transparentBlack, 0, r.getY() + 18, false);
        g.setGradientFill (sh); g.fillRect (r.withHeight (18));
        ColourGradient gl (Colours::white.withAlpha (0.13f), r.getX(), r.getY(), Colours::transparentWhite, r.getX() + r.getWidth() * 0.34f, r.getBottom(), false);
        g.setGradientFill (gl); g.fillRect (r);
    }
    const bool power = value ("power") > 0.5f;
    const bool ovl = power && Time::getMillisecondCounterHiRes() * 0.001 < ovlHold;
    auto overlay = [&] (const String& name)
    {
        auto it = crops.find (name); auto b = cropBox.find (name);
        if (it != crops.end() && b != cropBox.end()) drawImageAt (g, it->second, (float) b->second.getX(), (float) b->second.getY());
    };
    overlay (ovl ? "led_ovl_1" : "led_ovl_0");
    auto glow = [&] (Point<float> c)
    {
        const float r = 7.5f * ppm;
        ColourGradient gg (Colour (255, 90, 40).withAlpha (0.45f), c.x, c.y, Colour (255, 40, 20).withAlpha (0.0f), c.x + r, c.y, true);
        gg.addColour (0.32, Colour (255, 60, 25).withAlpha (0.18f));
        g.setGradientFill (gg); g.fillEllipse (Rectangle<float> (2 * r, 2 * r).withCentre (c));
    };
    if (power) glow (cLedPwr);
    if (ovl) glow (cLedOvl);
}

void OL4Editor::drawTip (Graphics& g)
{
    if (tipCtl == Ctl::None || Time::getMillisecondCounterHiRes() * 0.001 > tipUntil) return;
    const auto c = (tipCtl == Ctl::Threshold || tipCtl == Ctl::Ratio) ? cThr : cOut;
    const String s = tipText (tipCtl);
    g.setFont (FontOptions (26.0f));
    const float w = GlyphArrangement::getStringWidth (g.getCurrentFont(), s) + 36.0f;
    auto r = Rectangle<float> (w, 50.0f).withCentre ({ c.x, c.y - 296.0f - 25.0f });
    g.setColour (Colour (0xe60c0e0d)); g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Colours::white.withAlpha (0.12f)); g.drawRoundedRectangle (r, 10.0f, 1.5f);
    g.setColour (Colour (0xfff5e8cc)); g.drawText (s, r, Justification::centred);
}

void OL4Editor::paint (Graphics& g)
{
    // static layer, cached at the physical resolution of the window
    const float phys = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    const String key = String (getWidth()) + "x" + String (getHeight()) + "@" + String (phys, 2) + ":"
                     + String (value ("threshold"), 2) + "," + String (value ("output"), 2) + "," + String (roundToInt (value ("ratio")))
                     + String (roundToInt (value ("meter"))) + (value ("power") > 0.5f ? "1" : "0") + (value ("link") > 0.5f ? "1" : "0")
;
    if (! cache.isValid() || key != cacheKey)
    {
        const int cw = jmax (1, roundToInt ((float) getWidth() * phys)), ch = jmax (1, roundToInt ((float) getHeight() * phys));
        cache = Image (Image::RGB, cw, ch, false);
        Graphics cg (cache);
        cg.setImageResamplingQuality (Graphics::highResamplingQuality);
        cg.addTransform (AffineTransform::scale ((float) cw / (float) kImgW, (float) ch / (float) kImgH));
        renderStatic (cg);
        cacheKey = key;
    }
    g.drawImage (cache, getLocalBounds().toFloat());

    g.addTransform (AffineTransform::scale (scale));
    drawMeter (g);
    drawLamps (g);
    drawTip (g);
}
