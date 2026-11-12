#include "PluginEditor.h"

namespace
{
const auto background = juce::Colour (0xff2f2f2f);
const auto controlBackground = juce::Colour (0xff3d3d3d);
const auto statusBarBackground = juce::Colour (0xff262626);
const auto lightText = juce::Colour (0xffeeeeee);
const auto mutedText = juce::Colour (0xffaaaaaa);
const auto scopeBackground = juce::Colour (0xff284953);
const auto gridColour = juce::Colour (0xff24d28a);
const auto areaColour = juce::Colour (0xffff0080);
const auto areaFill = juce::Colour (0xff00005d);

juce::String text (const char* utf8) { return juce::String::fromUTF8 (utf8); }

juce::Colour valueToHeatmapColour (float v)
{
    if (v <= 0.003f)
        return scopeBackground;

    // Power curve for higher dynamic range and contrast
    const float shaped = std::pow (juce::jlimit (0.0f, 1.0f, v), 0.65f);
    // Hue: 0.52 (cyan/green) down to 0.0 (red)
    const float hue = juce::jlimit (0.0f, 0.52f, 0.52f * (1.0f - shaped * 1.15f));
    const float sat = juce::jlimit (0.6f, 1.0f, 0.6f + shaped * 0.4f);
    const float bri = juce::jlimit (0.32f, 1.0f, 0.32f + shaped * 0.75f);
    return juce::Colour::fromHSV (hue, sat, bri, 1.0f);
}
}

OtokirisAudioProcessorEditor::OtokirisAudioProcessorEditor (OtokirisAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (825, 612);
    setResizeLimits (640, 480, 1920, 1440);
    setResizable (true, true);
    startTimerHz (25);
}

OtokirisAudioProcessorEditor::~OtokirisAudioProcessorEditor() = default;

void OtokirisAudioProcessorEditor::updateHeatmapImages()
{
    const auto* noVisualParam = processor.parameters.getRawParameterValue ("noVisual");
    if (noVisualParam != nullptr && noVisualParam->load() > 0.5f)
        return;

    processor.getHeatmapData (rawPanData, rawDepthData);

    const auto* colourScaleParam = processor.parameters.getRawParameterValue ("colourScale");
    const float colourScale = colourScaleParam != nullptr ? colourScaleParam->load() : 0.5f;
    const float colourGain = std::pow (2.0f, (colourScale - 0.5f) * 4.0f);

    juce::Image::BitmapData panPixels (panImage, juce::Image::BitmapData::writeOnly);
    juce::Image::BitmapData depthPixels (depthImage, juce::Image::BitmapData::writeOnly);

    constexpr int bands = OtokirisAudioProcessor::numBands;
    constexpr int slots = OtokirisAudioProcessor::numSlots;

    for (int y = 0; y < bands; ++y)
    {
        // 0 = low freq (bottom), bands-1 = high freq (top)
        const int bandIdx = bands - 1 - y;
        for (int x = 0; x < slots; ++x)
        {
            const size_t idx = (size_t) (bandIdx * slots + x);
            const float panVal = juce::jlimit (0.0f, 1.0f, rawPanData[idx] * colourGain);
            const float depthVal = juce::jlimit (0.0f, 1.0f, rawDepthData[idx] * colourGain);

            panPixels.setPixelColour (x, y, valueToHeatmapColour (panVal));
            depthPixels.setPixelColour (x, y, valueToHeatmapColour (depthVal));
        }
    }
}

void OtokirisAudioProcessorEditor::timerCallback()
{
    updateHeatmapImages();
    repaint();
}

void OtokirisAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    constexpr float designWidth = 825.0f;
    constexpr float designHeight = 612.0f;
    const auto scale = juce::jmin (getWidth() / designWidth, getHeight() / designHeight);
    const auto offsetX = (getWidth() - designWidth * scale) * 0.5f;
    const auto offsetY = (getHeight() - designHeight * scale) * 0.5f;

    graphics.fillAll (background);
    graphics.addTransform (juce::AffineTransform::translation (offsetX, offsetY).scaled (scale, scale));
    graphics.setColour (background);
    graphics.fillRect (0, 0, (int) designWidth, (int) designHeight);

    // Header titles
    graphics.setColour (lightText);
    graphics.setFont (17.0f);
    graphics.drawText (text (u8"定位"), 20, 34, 560, 22, juce::Justification::centred);
    graphics.drawText (text (u8"奥行き"), 615, 34, 196, 22, juce::Justification::centred);

    const auto leftArea = juce::Rectangle<int> (20, 68, 560, 340);
    const auto rightArea = juce::Rectangle<int> (615, 68, 196, 340);

    const auto* centreParam = processor.parameters.getRawParameterValue ("positionCentre");
    const auto* widthParam = processor.parameters.getRawParameterValue ("positionWidth");
    const auto* freqCentreParam = processor.parameters.getRawParameterValue ("frequencyCentre");
    const auto* freqWidthParam = processor.parameters.getRawParameterValue ("frequencyWidth");
    const auto* depthCentreParam = processor.parameters.getRawParameterValue ("depthCentre");
    const auto* depthWidthParam = processor.parameters.getRawParameterValue ("depthWidth");

    const auto centre = centreParam != nullptr ? centreParam->load() : 0.0f;
    const auto width = widthParam != nullptr ? widthParam->load() : 0.55f;
    const auto freqCentre = freqCentreParam != nullptr ? freqCentreParam->load() : 0.5f;
    const auto freqWidth = freqWidthParam != nullptr ? freqWidthParam->load() : 0.67f;
    const auto depthCentre = depthCentreParam != nullptr ? depthCentreParam->load() : 0.5f;
    const auto depthWidth = depthWidthParam != nullptr ? depthWidthParam->load() : 0.47f;

    const auto* noVisualParam = processor.parameters.getRawParameterValue ("noVisual");
    const bool noVisual = noVisualParam != nullptr && noVisualParam->load() > 0.5f;

    // Draw scope screen
    auto drawScopeScreen = [&] (juce::Rectangle<int> area, bool isDepth,
                                const juce::Image& heatmapImg,
                                float c, float w, float fc, float fw)
    {
        graphics.setColour (scopeBackground);
        graphics.fillRect (area);

        if (! noVisual)
            graphics.drawImage (heatmapImg, area.toFloat(), juce::RectanglePlacement::stretchToFit);

        // Grid lines
        graphics.setColour (gridColour);
        graphics.drawRect (area, 1);
        graphics.setColour (juce::Colour (0xff16785d));
        for (const auto fraction : { 0.125f, 0.25f, 0.375f, 0.625f, 0.75f, 0.875f })
            graphics.drawVerticalLine ((int) std::round (area.getX() + area.getWidth() * fraction),
                                       (float) area.getY(), (float) area.getBottom());

        graphics.setColour (juce::Colour (0xff35cfa0));
        for (const auto fraction : { 0.0f, 0.50f, 1.0f })
            graphics.drawVerticalLine ((int) std::round (area.getX() + area.getWidth() * fraction),
                                       (float) area.getY(), (float) area.getBottom());

        const auto nyquist = processor.getAnalysisSampleRate() * 0.5;
        for (const auto freq : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            if (freq > nyquist)
                continue;
            const auto fraction = std::log (freq / 20.0) / std::log (nyquist / 20.0);
            const auto y = (int) std::round (area.getBottom() - fraction * area.getHeight());
            graphics.setColour (freq == 1000.0 || freq == 10000.0 ? juce::Colour (0xff35cfa0)
                                                                 : juce::Colour (0xff16785d));
            graphics.drawHorizontalLine (y, (float) area.getX(), (float) area.getRight());
        }

        // Selection rectangle
        const auto selRect = isDepth
            ? juce::Rectangle<int> ((int) (area.getX() + c * area.getWidth() - w * area.getWidth() * 0.5f),
                                    (int) (area.getY() + (1.0f - fc - fw * 0.5f) * area.getHeight()),
                                    (int) (w * area.getWidth()),
                                    (int) (fw * area.getHeight()))
            : juce::Rectangle<int> ((int) (area.getX() + area.getWidth() * 0.5f + c * area.getWidth() * 0.5f - w * area.getWidth() * 0.5f),
                                    (int) (area.getY() + (1.0f - fc - fw * 0.5f) * area.getHeight()),
                                    (int) (w * area.getWidth()),
                                    (int) (fw * area.getHeight()));

        graphics.setColour (areaFill.withAlpha (0.35f));
        graphics.fillRect (selRect);
        graphics.setColour (areaColour);
        graphics.drawRect (selRect, 2);

        // Frequency labels inside screen
        graphics.setColour (juce::Colours::white);
        graphics.setFont (13.0f);
        const auto labelX = area.getX() + 5;
        for (const auto item : { std::pair<double, const char*> { 20000.0, "20k" },
                                 { 10000.0, "10k" }, { 5000.0, "5k" },
                                 { 1000.0, "1k" }, { 100.0, "100" } })
        {
            if (item.first > nyquist)
                continue;
            const auto fraction = std::log (item.first / 20.0) / std::log (nyquist / 20.0);
            const auto y = (int) std::round (area.getBottom() - fraction * area.getHeight());
            graphics.setColour (juce::Colour (0xbb202020));
            graphics.fillRect (labelX - 2, y - 9, 36, 17);
            graphics.setColour (juce::Colours::white);
            graphics.drawText (item.second, labelX, y - 8, 34, 15, juce::Justification::left);
        }

        // Watermark labels L/R or F/B
        graphics.setColour (juce::Colour (0x661a9e85));
        graphics.setFont (32.0f);
        graphics.drawText (isDepth ? "F" : "L", area.getX() + 6, area.getBottom() - 45, 40, 40,
                           juce::Justification::left);
        graphics.drawText (isDepth ? "B" : "R", area.getRight() - 46, area.getBottom() - 45, 40, 40,
                           juce::Justification::right);

        // Scale labels below screen (y = 409, height = 12)
        graphics.setColour (mutedText);
        graphics.setFont (11.0f);
        const auto scaleLabels = isDepth ? std::array<const char*, 5> { "0", "25", "50", "75", "100" }
                                         : std::array<const char*, 5> { "-1", "-0.5", "0", "0.5", "1" };
        for (int i = 0; i < 5; ++i)
        {
            graphics.drawText (scaleLabels[(size_t) i],
                               area.getX() + i * (area.getWidth() - 24) / 4 - 4,
                               area.getBottom() + 1, 32, 12, juce::Justification::centred);
        }
    };

    drawScopeScreen (leftArea, false, panImage, centre, width, freqCentre, freqWidth);
    drawScopeScreen (rightArea, true, depthImage, depthCentre, depthWidth, freqCentre, freqWidth);

    // Control panel (y = 422 onwards)
    graphics.setColour (controlBackground);
    graphics.fillRect (0, 422, (int) designWidth, 190);

    // Status bar (y = 422 to 440)
    graphics.setColour (statusBarBackground);
    graphics.fillRect (0, 422, (int) designWidth, 19);
    graphics.setColour (juce::Colour (0xff1a1a1a));
    graphics.drawHorizontalLine (441, 0.0f, designWidth);

    // Formatted frequency and pan text
    const auto nyquist = processor.getAnalysisSampleRate() * 0.5;
    const auto lowFraction = juce::jlimit (0.0f, 1.0f, freqCentre - freqWidth * 0.5f);
    const auto highFraction = juce::jlimit (0.0f, 1.0f, freqCentre + freqWidth * 0.5f);
    const auto lowHz = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) lowFraction);
    const auto highHz = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) highFraction);

    const auto formatHz = [] (double hz) -> juce::String
    {
        if (hz >= 10000.0)
            return juce::String (hz / 1000.0, 1) + " kHz";
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, 2) + " kHz";
        return juce::String ((int) std::round (hz)) + " Hz";
    };

    const auto formatPan = [] (float p) -> juce::String
    {
        if (std::abs (p) < 0.01f)
            return "Center";
        if (p < 0.0f)
            return "L " + juce::String (-p, 2);
        return "R " + juce::String (p, 2);
    };

    graphics.setColour (lightText);
    graphics.setFont (12.0f);
    const auto freqStatus = text (u8"周波数: ") + formatHz (lowHz) + " - " + formatHz (highHz)
                          + " (" + juce::String (freqWidth * 100.0f, 0) + "%)";
    const auto panStatus = text (u8"定位: ") + formatPan (centre) + " (" + text (u8"幅 ")
                         + juce::String (width * 100.0f, 0) + "%)";
    const auto depthStatus = text (u8"奥行: ") + juce::String (depthCentre * 100.0f, 0) + "% ("
                           + text (u8"幅 ") + juce::String (depthWidth * 100.0f, 0) + "%)";

    graphics.drawText (freqStatus + "    |    " + panStatus, 20, 424, 560, 16, juce::Justification::left);
    graphics.drawText (depthStatus, 615, 424, 196, 16, juce::Justification::left);

    // Button drawing helper
    const auto drawButton = [&graphics] (juce::Rectangle<int> bounds, const juce::String& btnText, bool highlighted = false)
    {
        graphics.setColour (highlighted ? juce::Colour (0xff707070) : juce::Colour (0xff505050));
        graphics.fillRect (bounds);
        graphics.setColour (juce::Colour (0xff888888));
        graphics.drawRect (bounds, 1);
        graphics.setColour (lightText);
        graphics.setFont (13.0f);
        graphics.drawText (btnText, bounds, juce::Justification::centred);
    };

    // Preset buttons (y = 445, height = 22)
    drawButton ({ 97, 445, 86, 22 }, text (u8"定位最大化"));
    drawButton ({ 257, 445, 86, 22 }, text (u8"定位中央"));
    drawButton ({ 418, 445, 86, 22 }, text (u8"周波数最大化"));
    drawButton ({ 672, 445, 86, 22 }, text (u8"奥行最大化"));

    // Labels for sliders
    graphics.setColour (lightText);
    graphics.setFont (14.0f);
    graphics.drawText (text (u8"抽出強度"), 20, 477, 90, 18, juce::Justification::left);
    graphics.drawText (text (u8"境界の滑らかさ"), 20, 509, 110, 18, juce::Justification::left);
    graphics.drawText (text (u8"残響抑制"), 20, 541, 90, 18, juce::Justification::left);
    graphics.drawText (text (u8"再生音量"), 450, 477, 78, 18, juce::Justification::left);
    graphics.drawText (text (u8"定位スケール"), 450, 509, 78, 18, juce::Justification::left);
    graphics.drawText (text (u8"カラースケール"), 450, 541, 90, 18, juce::Justification::left);

    // Left sliders (Extraction amount, Mask softness, Reverb reduction)
    for (const auto y : { 486, 518, 550 })
    {
        graphics.setColour (juce::Colour (0xffbbbbbb));
        graphics.fillRect (130, y - 3, 135, 6);
        const auto* id = y == 486 ? "extractionAmount" : (y == 518 ? "maskSoftness" : "reverbReduction");
        const auto* value = processor.parameters.getRawParameterValue (id);
        const auto knobX = 130 + (int) std::round ((value != nullptr ? value->load() : 0.0f) * 127.0f);
        graphics.setColour (juce::Colour (0xffe0e0e0));
        graphics.fillRect (knobX - 4, y - 8, 8, 16);
    }

    // Right sliders (Output volume, Position scale, Colour scale)
    for (const auto y : { 486, 518, 550 })
    {
        graphics.setColour (juce::Colour (0xffbbbbbb));
        graphics.fillRect (540, y - 3, 135, 6);
        const auto* id = y == 486 ? "outputVolume" : (y == 518 ? "positionScale" : "colourScale");
        const auto* value = processor.parameters.getRawParameterValue (id);
        const auto knobX = 540 + (int) std::round ((value != nullptr ? value->load() : 0.5f) * 127.0f);
        graphics.setColour (juce::Colour (0xffe0e0e0));
        graphics.fillRect (knobX - 4, y - 8, 8, 16);
    }

    // Checkboxes
    for (const auto& item : { std::pair<juce::String, int> { text (u8"定位固定"), 476 },
                              { text (u8"周波数固定"), 500 },
                              { text (u8"抽出エリア反転"), 524 },
                              { text (u8"ビジュアルなし"), 548 } })
    {
        const auto* id = item.second == 476 ? "positionLock"
                       : item.second == 500 ? "frequencyLock"
                       : item.second == 524 ? "invertArea" : "noVisual";
        const auto* value = processor.parameters.getRawParameterValue (id);
        graphics.setColour (value != nullptr && value->load() > 0.5f ? areaColour : juce::Colours::white);
        graphics.fillRect (706, item.second, 14, 14);
        graphics.setColour (lightText);
        graphics.drawRect (706, item.second, 14, 14, 1);
        graphics.setFont (13.0f);
        graphics.drawText (item.first, 726, item.second - 2, 110, 18, juce::Justification::left);
    }

    // Bottom transport bar (y = 572, height = 28)
    drawButton ({ 94, 572, 73, 28 }, text (u8"先頭へ"));
    drawButton ({ 180, 572, 87, 28 }, text (u8"再生 / 停止"));
    drawButton ({ 280, 572, 75, 28 }, isSlotB ? text (u8"Slot B") : text (u8"Slot A"), isSlotB);

    graphics.setColour (lightText);
    graphics.setFont (12.0f);
    const auto rateText = juce::String (processor.getAnalysisSampleRate(), 0) + " Hz";
    const auto transportText = processor.isHostPlaying() ? text (u8"再生中") : text (u8"停止中");
    graphics.drawText (rateText, 20, 576, 70, 18, juce::Justification::left);
    graphics.setColour (processor.isHostPlaying() ? juce::Colour (0xff35cfa0) : mutedText);
    graphics.drawText (transportText, 370, 576, 90, 18, juce::Justification::left);
}

void OtokirisAudioProcessorEditor::resized()
{
}

void OtokirisAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto scale = juce::jmin (getWidth() / 825.0f, getHeight() / 612.0f);
    const auto point = (event.position - juce::Point<float> ((getWidth() - 825.0f * scale) * 0.5f,
                                                              (getHeight() - 612.0f * scale) * 0.5f)) / scale;
    const auto setParameter = [this] (const char* id, float value)
    {
        if (auto* parameter = processor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };
    const auto toggleParameter = [this] (const char* id)
    {
        if (auto* parameter = processor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->getValue() > 0.5f ? 0.0f : 1.0f);
    };

    // Preset buttons (y = 445, height = 22)
    if (juce::Rectangle<float> (97, 445, 86, 22).contains (point))
        setParameter ("positionWidth", 1.0f);
    else if (juce::Rectangle<float> (257, 445, 86, 22).contains (point))
        setParameter ("positionCentre", 0.0f);
    else if (juce::Rectangle<float> (418, 445, 86, 22).contains (point))
        setParameter ("frequencyWidth", 1.0f);
    else if (juce::Rectangle<float> (672, 445, 86, 22).contains (point))
        setParameter ("depthWidth", 1.0f);

    // Checkboxes (y = 476, 500, 524, 548)
    else if (juce::Rectangle<float> (706, 476, 140, 18).contains (point))
        toggleParameter ("positionLock");
    else if (juce::Rectangle<float> (706, 500, 140, 18).contains (point))
        toggleParameter ("frequencyLock");
    else if (juce::Rectangle<float> (706, 524, 140, 18).contains (point))
        toggleParameter ("invertArea");
    else if (juce::Rectangle<float> (706, 548, 140, 18).contains (point))
        toggleParameter ("noVisual");

    // A/B switch button
    else if (juce::Rectangle<float> (280, 572, 75, 28).contains (point))
    {
        const auto getVal = [this] (const char* id) -> float
        {
            auto* p = processor.parameters.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        if (! isSlotB)
        {
            slotA.posCentre = getVal ("positionCentre");
            slotA.posWidth = getVal ("positionWidth");
            slotA.freqCentre = getVal ("frequencyCentre");
            slotA.freqWidth = getVal ("frequencyWidth");
            slotA.depthCentre = getVal ("depthCentre");
            slotA.depthWidth = getVal ("depthWidth");
            slotA.invert = getVal ("invertArea") > 0.5f;
            slotA.hasValue = true;

            if (slotB.hasValue)
            {
                setParameter ("positionCentre", slotB.posCentre);
                setParameter ("positionWidth", slotB.posWidth);
                setParameter ("frequencyCentre", slotB.freqCentre);
                setParameter ("frequencyWidth", slotB.freqWidth);
                setParameter ("depthCentre", slotB.depthCentre);
                setParameter ("depthWidth", slotB.depthWidth);
                if (auto* p = processor.parameters.getParameter ("invertArea"))
                    p->setValueNotifyingHost (slotB.invert ? 1.0f : 0.0f);
            }
            isSlotB = true;
        }
        else
        {
            slotB.posCentre = getVal ("positionCentre");
            slotB.posWidth = getVal ("positionWidth");
            slotB.freqCentre = getVal ("frequencyCentre");
            slotB.freqWidth = getVal ("frequencyWidth");
            slotB.depthCentre = getVal ("depthCentre");
            slotB.depthWidth = getVal ("depthWidth");
            slotB.invert = getVal ("invertArea") > 0.5f;
            slotB.hasValue = true;

            if (slotA.hasValue)
            {
                setParameter ("positionCentre", slotA.posCentre);
                setParameter ("positionWidth", slotA.posWidth);
                setParameter ("frequencyCentre", slotA.freqCentre);
                setParameter ("frequencyWidth", slotA.freqWidth);
                setParameter ("depthCentre", slotA.depthCentre);
                setParameter ("depthWidth", slotA.depthWidth);
                if (auto* p = processor.parameters.getParameter ("invertArea"))
                    p->setValueNotifyingHost (slotA.invert ? 1.0f : 0.0f);
            }
            isSlotB = false;
        }
        repaint();
        return;
    }

    // Sliders
    else if (juce::Rectangle<float> (130, 476, 135, 20).contains (point))
        activeControl = 4;
    else if (juce::Rectangle<float> (130, 508, 135, 20).contains (point))
        activeControl = 5;
    else if (juce::Rectangle<float> (130, 540, 135, 20).contains (point))
        activeControl = 6;
    else if (juce::Rectangle<float> (540, 476, 135, 20).contains (point))
        activeControl = 1;
    else if (juce::Rectangle<float> (540, 508, 135, 20).contains (point))
        activeControl = 2;
    else if (juce::Rectangle<float> (540, 540, 135, 20).contains (point))
        activeControl = 3;
    else
        activeControl = 0;

    if (activeControl != 0)
    {
        const auto isAdvanced = activeControl >= 4;
        const auto sliderX = isAdvanced ? 130.0f : 540.0f;
        const auto value = juce::jlimit (0.0f, 1.0f, (point.x - sliderX) / 135.0f);
        const auto* id = activeControl == 1 ? "outputVolume"
                      : activeControl == 2 ? "positionScale"
                      : activeControl == 3 ? "colourScale"
                      : activeControl == 4 ? "extractionAmount"
                      : activeControl == 5 ? "maskSoftness" : "reverbReduction";
        setParameter (id, value);
        repaint();
        return;
    }

    // Scope selection rectangles
    const auto left = juce::Rectangle<float> (20.0f, 68.0f, 560.0f, 340.0f);
    const auto right = juce::Rectangle<float> (615.0f, 68.0f, 196.0f, 340.0f);
    activeScope = left.contains (point) ? 1 : (right.contains (point) ? 2 : 0);
    if (activeScope == 0)
        return;

    const auto positionLocked = processor.parameters.getRawParameterValue ("positionLock");
    const auto frequencyLocked = processor.parameters.getRawParameterValue ("frequencyLock");
    const auto scope = activeScope == 1 ? left : right;

    const auto centreParameter = processor.parameters.getRawParameterValue (
        activeScope == 1 ? "positionCentre" : "depthCentre");
    const auto widthParameter = processor.parameters.getRawParameterValue (
        activeScope == 1 ? "positionWidth" : "depthWidth");
    const auto frequencyCentreParameter = processor.parameters.getRawParameterValue ("frequencyCentre");
    const auto frequencyWidthParameter = processor.parameters.getRawParameterValue ("frequencyWidth");

    const auto centre = centreParameter != nullptr ? centreParameter->load() : (activeScope == 1 ? 0.0f : 0.5f);
    const auto width = widthParameter != nullptr ? widthParameter->load() : 0.5f;
    const auto freqC = frequencyCentreParameter != nullptr ? frequencyCentreParameter->load() : 0.5f;
    const auto freqW = frequencyWidthParameter != nullptr ? frequencyWidthParameter->load() : 0.67f;

    const auto current = activeScope == 1
        ? juce::Rectangle<float> (scope.getCentreX() + centre * scope.getWidth() * 0.5f - width * scope.getWidth() * 0.5f,
                                  scope.getY() + (1.0f - freqC - freqW * 0.5f) * scope.getHeight(),
                                  width * scope.getWidth(), freqW * scope.getHeight())
        : juce::Rectangle<float> (scope.getX() + centre * scope.getWidth() - width * scope.getWidth() * 0.5f,
                                  scope.getY() + (1.0f - freqC - freqW * 0.5f) * scope.getHeight(),
                                  width * scope.getWidth(), freqW * scope.getHeight());

    const auto margin = 10.0f;
    dragEdges = (std::abs (point.x - current.getX()) <= margin ? 1 : 0)
              | (std::abs (point.x - current.getRight()) <= margin ? 2 : 0)
              | (std::abs (point.y - current.getY()) <= margin ? 4 : 0)
              | (std::abs (point.y - current.getBottom()) <= margin ? 8 : 0);

    if (positionLocked != nullptr && positionLocked->load() > 0.5f)
        dragEdges &= ~3;
    if (frequencyLocked != nullptr && frequencyLocked->load() > 0.5f)
        dragEdges &= ~12;

    draggingPosition = current.expanded (margin, margin).contains (point);
    dragStart = point;
    dragAreaAtStart = { (current.getX() - scope.getX()) / scope.getWidth(),
                        (current.getY() - scope.getY()) / scope.getHeight(),
                        current.getWidth() / scope.getWidth(),
                        current.getHeight() / scope.getHeight() };
}

void OtokirisAudioProcessorEditor::mouseDrag (const juce::MouseEvent& event)
{
    const auto scale = juce::jmin (getWidth() / 825.0f, getHeight() / 612.0f);
    const auto point = (event.position - juce::Point<float> ((getWidth() - 825.0f * scale) * 0.5f,
                                                              (getHeight() - 612.0f * scale) * 0.5f)) / scale;

    if (activeControl != 0)
    {
        const auto sliderX = activeControl >= 4 ? 130.0f : 540.0f;
        const auto value = juce::jlimit (0.0f, 1.0f, (point.x - sliderX) / 135.0f);
        const auto* id = activeControl == 1 ? "outputVolume"
                      : activeControl == 2 ? "positionScale"
                      : activeControl == 3 ? "colourScale"
                      : activeControl == 4 ? "extractionAmount"
                      : activeControl == 5 ? "maskSoftness" : "reverbReduction";
        if (auto* parameter = processor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        repaint();
        return;
    }

    if (! draggingPosition)
        return;

    const auto scope = activeScope == 1 ? juce::Rectangle<float> (20.0f, 68.0f, 560.0f, 340.0f)
                                        : juce::Rectangle<float> (615.0f, 68.0f, 196.0f, 340.0f);

    // Shift key enables fine precision tuning (0.2x drag speed)
    const float speed = event.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto deltaX = ((point.x - dragStart.x) / scope.getWidth()) * speed;
    const auto deltaY = ((point.y - dragStart.y) / scope.getHeight()) * speed;

    auto area = dragAreaAtStart;

    if (dragEdges & 1) area.setLeft (area.getX() + deltaX);
    if (dragEdges & 2) area.setRight (area.getRight() + deltaX);
    if (dragEdges & 4) area.setTop (area.getY() + deltaY);
    if (dragEdges & 8) area.setBottom (area.getBottom() + deltaY);
    if (dragEdges == 0)
        area.setPosition (area.getX() + deltaX, area.getY() + deltaY);

    if (auto* locked = processor.parameters.getRawParameterValue ("positionLock"))
        if (locked->load() > 0.5f)
            area.setX (dragAreaAtStart.getX());
    if (auto* locked = processor.parameters.getRawParameterValue ("frequencyLock"))
        if (locked->load() > 0.5f)
            area.setY (dragAreaAtStart.getY());

    area = area.constrainedWithin (juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f));
    if (activeScope == 1)
        positionArea = area;
    else
        depthArea = area;

    const auto centre = activeScope == 1
        ? juce::jlimit (-1.0f, 1.0f, (area.getCentreX() - 0.5f) * 2.0f)
        : juce::jlimit (0.0f, 1.0f, area.getCentreX());
    const auto width = area.getWidth();

    const auto centreId = activeScope == 1 ? "positionCentre" : "depthCentre";
    const auto widthId = activeScope == 1 ? "positionWidth" : "depthWidth";

    if (auto* parameter = processor.parameters.getParameter (centreId))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (centre));
    if (auto* parameter = processor.parameters.getParameter (widthId))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (width));
    if (auto* parameter = processor.parameters.getParameter ("frequencyCentre"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (1.0f - area.getCentreY()));
    if (auto* parameter = processor.parameters.getParameter ("frequencyWidth"))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (area.getHeight()));

    repaint();
}

void OtokirisAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    draggingPosition = false;
    dragEdges = 0;
    activeScope = 0;
    activeControl = 0;
}

void OtokirisAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! draggingPosition)
        mouseDown (event);

    const auto setParameter = [this] (const char* id, float value)
    {
        if (auto* parameter = processor.parameters.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    if (activeScope == 1)
    {
        if (savedPositionArea.isEmpty())
        {
            savedPositionArea = positionArea;
            setParameter ("positionCentre", 0.0f);
            setParameter ("positionWidth", 1.0f);
            setParameter ("frequencyCentre", 0.5f);
            setParameter ("frequencyWidth", 1.0f);
        }
        else
        {
            const auto centre = juce::jlimit (-1.0f, 1.0f, (savedPositionArea.getCentreX() - 0.5f) * 2.0f);
            setParameter ("positionCentre", centre);
            setParameter ("positionWidth", savedPositionArea.getWidth());
            setParameter ("frequencyCentre", 1.0f - savedPositionArea.getCentreY());
            setParameter ("frequencyWidth", savedPositionArea.getHeight());
            savedPositionArea = {};
        }
    }
    else if (activeScope == 2)
    {
        if (savedDepthArea.isEmpty())
        {
            savedDepthArea = depthArea;
            setParameter ("depthCentre", 0.5f);
            setParameter ("depthWidth", 1.0f);
            setParameter ("frequencyCentre", 0.5f);
            setParameter ("frequencyWidth", 1.0f);
        }
        else
        {
            setParameter ("depthCentre", savedDepthArea.getCentreX());
            setParameter ("depthWidth", savedDepthArea.getWidth());
            setParameter ("frequencyCentre", 1.0f - savedDepthArea.getCentreY());
            setParameter ("frequencyWidth", savedDepthArea.getHeight());
            savedDepthArea = {};
        }
    }

    mouseUp (event);
    repaint();
}
