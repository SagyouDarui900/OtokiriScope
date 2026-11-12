#include "PluginEditor.h"

namespace
{
const auto background = juce::Colour (0xff2f2f2f);
const auto controlBackground = juce::Colour (0xff3d3d3d);
const auto lightText = juce::Colour (0xffeeeeee);
const auto scopeBackground = juce::Colour (0xff284953);
const auto gridColour = juce::Colour (0xff24d28a);
const auto areaColour = juce::Colour (0xffff0080);
const auto areaFill = juce::Colour (0xff00005d);
const auto darkText = juce::Colour (0xff202020);

void drawScope (juce::Graphics&, OtokirisAudioProcessor&, juce::Rectangle<int>,
                bool, float, float, float, float);
juce::String text (const char* utf8) { return juce::String::fromUTF8 (utf8); }
}

OtokirisAudioProcessorEditor::OtokirisAudioProcessorEditor (OtokirisAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (825, 612);
    setResizeLimits (640, 480, 1920, 1440);
    setResizable (true, true);
    startTimerHz (12);
}

OtokirisAudioProcessorEditor::~OtokirisAudioProcessorEditor() = default;

void OtokirisAudioProcessorEditor::timerCallback()
{
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
    graphics.addTransform (juce::AffineTransform::translation (offsetX, offsetY)
                               .scaled (scale, scale));
    graphics.setColour (background);
    graphics.fillRect (0, 0, (int) designWidth, (int) designHeight);
    graphics.setColour (lightText);
    graphics.setFont (17.0f);
    graphics.drawText (text (u8"定位"), 20, 34, 560, 20, juce::Justification::centred);
    graphics.drawText (text (u8"奥行き"), 615, 34, 196, 20, juce::Justification::centred);

    const auto left = juce::Rectangle<int> (20, 68, 560, 340);
    const auto right = juce::Rectangle<int> (615, 68, 196, 340);
    const auto* centreParameter = processor.parameters.getRawParameterValue ("positionCentre");
    const auto* widthParameter = processor.parameters.getRawParameterValue ("positionWidth");
    const auto* frequencyCentreParameter = processor.parameters.getRawParameterValue ("frequencyCentre");
    const auto* frequencyWidthParameter = processor.parameters.getRawParameterValue ("frequencyWidth");
    const auto* depthCentreParameter = processor.parameters.getRawParameterValue ("depthCentre");
    const auto* depthWidthParameter = processor.parameters.getRawParameterValue ("depthWidth");
    const auto centre = centreParameter != nullptr ? centreParameter->load() : 0.0f;
    const auto width = widthParameter != nullptr ? widthParameter->load() : 1.0f;
    const auto frequencyCentre = frequencyCentreParameter != nullptr ? frequencyCentreParameter->load() : 0.5f;
    const auto frequencyWidth = frequencyWidthParameter != nullptr ? frequencyWidthParameter->load() : 0.67f;
    const auto depthCentre = depthCentreParameter != nullptr ? depthCentreParameter->load() : 0.5f;
    const auto depthWidth = depthWidthParameter != nullptr ? depthWidthParameter->load() : 0.47f;
    drawScope (graphics, processor, left, false, centre, width,
               frequencyCentre, frequencyWidth);
    drawScope (graphics, processor, right, true, depthCentre, depthWidth,
               frequencyCentre, frequencyWidth);

    graphics.setColour (controlBackground);
    graphics.fillRect (0, 420, (int) designWidth, 192);
    graphics.setColour (lightText);
    graphics.setFont (12.0f);
    const auto nyquist = processor.getAnalysisSampleRate() * 0.5;
    const auto lowFraction = juce::jlimit (0.0f, 1.0f, frequencyCentre - frequencyWidth * 0.5f);
    const auto highFraction = juce::jlimit (0.0f, 1.0f, frequencyCentre + frequencyWidth * 0.5f);
    const auto lowHz = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) lowFraction);
    const auto highHz = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) highFraction);
    const auto formatHz = [] (double hz)
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz"
                            : juce::String (hz, 0) + " Hz";
    };
    graphics.drawText (text (u8"周波数範囲: ") + formatHz (lowHz) + " - "
                       + formatHz (highHz) + "  (" + juce::String (frequencyWidth * 100.0f, 0) + "%)",
                       20, 410, 260, 16, juce::Justification::left);
    graphics.drawText (text (u8"奥行: ") + juce::String (depthCentre * 100.0f, 0)
                       + "% / " + juce::String (depthWidth * 100.0f, 0) + "%",
                       615, 410, 196, 16, juce::Justification::left);

    const auto drawButton = [&graphics] (juce::Rectangle<int> bounds, const juce::String& text)
    {
        graphics.setColour (juce::Colour (0xff555555));
        graphics.fillRect (bounds);
        graphics.setColour (juce::Colour (0xff9a9a9a));
        graphics.drawRect (bounds, 1);
        graphics.setColour (lightText);
        graphics.setFont (14.0f);
        graphics.drawText (text, bounds, juce::Justification::centred);
    };
    drawButton ({ 97, 430, 86, 24 }, text (u8"定位最大化"));
    drawButton ({ 257, 430, 86, 24 }, text (u8"定位中央"));
    drawButton ({ 418, 430, 86, 24 }, text (u8"周波数最大化"));
    drawButton ({ 672, 430, 86, 24 }, text (u8"奥行最大化"));

    graphics.setColour (lightText);
    graphics.setFont (15.0f);
    graphics.drawText (text (u8"抽出強度"), 20, 467, 90, 20, juce::Justification::left);
    graphics.drawText (text (u8"境界の滑らかさ"), 20, 499, 110, 20, juce::Justification::left);
    graphics.drawText (text (u8"残響抑制"), 20, 531, 90, 20, juce::Justification::left);
    graphics.drawText (text (u8"再生音量"), 450, 467, 78, 20, juce::Justification::left);
    graphics.drawText (text (u8"定位スケール"), 450, 499, 78, 20, juce::Justification::left);
    graphics.drawText (text (u8"カラースケール"), 450, 531, 90, 20, juce::Justification::left);
    for (const auto y : { 476, 508, 540 })
    {
        graphics.setColour (juce::Colour (0xffbcbcbc));
        graphics.fillRect (y < 550 ? 530 : 530, y - 4, 135, 8);
        graphics.setColour (juce::Colour (0xff777777));
        const auto* id = y == 476 ? "outputVolume" : (y == 508 ? "positionScale" : "colourScale");
        const auto* value = processor.parameters.getRawParameterValue (id);
        const auto knobX = 530 + (int) std::round ((value != nullptr ? value->load() : 0.5f) * 127.0f);
        graphics.fillRect (knobX - 4, y - 9, 8, 18);
    }
    for (const auto y : { 476, 508, 540 })
    {
        graphics.setColour (juce::Colour (0xffbcbcbc));
        graphics.fillRect (110, y - 4, 135, 8);
        const auto* id = y == 476 ? "extractionAmount"
                       : y == 508 ? "maskSoftness" : "reverbReduction";
        const auto* value = processor.parameters.getRawParameterValue (id);
        const auto knobX = 110 + (int) std::round ((value != nullptr ? value->load() : 0.0f) * 127.0f);
        graphics.setColour (juce::Colour (0xff777777));
        graphics.fillRect (knobX - 4, y - 9, 8, 18);
    }
    for (const auto& item : { std::pair<juce::String, int> { text (u8"定位固定"), 468 },
                              { text (u8"周波数固定"), 492 }, { text (u8"抽出エリア反転"), 516 },
                              { text (u8"ビジュアルなし"), 540 } })
    {
        const auto* id = item.second == 468 ? "positionLock"
                       : item.second == 492 ? "frequencyLock"
                       : item.second == 516 ? "invertArea" : "noVisual";
        const auto* value = processor.parameters.getRawParameterValue (id);
        graphics.setColour (value != nullptr && value->load() > 0.5f
                                ? juce::Colour (0xffff0080) : juce::Colours::white);
        graphics.fillRect (706, item.second, 15, 15);
        graphics.setColour (lightText);
        graphics.drawRect (706, item.second, 15, 15, 1);
        graphics.drawText (item.first, 727, item.second - 3, 110, 20, juce::Justification::left);
    }
    drawButton ({ 94, 570, 73, 31 }, text (u8"先頭へ"));
    drawButton ({ 204, 570, 87, 31 }, text (u8"再生 / 停止"));
    drawButton ({ 328, 570, 73, 31 }, text (u8"A / B"));
    graphics.setColour (lightText);
    graphics.setFont (12.0f);
    const auto rateText = juce::String (processor.getAnalysisSampleRate(), 0) + " Hz";
    const auto transportText = processor.isHostPlaying() ? text (u8"再生中") : text (u8"停止中");
    graphics.drawText (rateText, 20, 574, 70, 18, juce::Justification::left);
    graphics.drawText (transportText, 420, 574, 90, 18, juce::Justification::left);
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

    if (juce::Rectangle<float> (97, 430, 86, 24).contains (point))
        setParameter ("positionWidth", 1.0f);
    else if (juce::Rectangle<float> (257, 430, 86, 24).contains (point))
        setParameter ("positionCentre", 0.0f);
    else if (juce::Rectangle<float> (418, 430, 86, 24).contains (point))
        setParameter ("frequencyWidth", 1.0f);
    else if (juce::Rectangle<float> (672, 430, 86, 24).contains (point))
        setParameter ("depthWidth", 1.0f);
    else if (juce::Rectangle<float> (706, 468, 150, 18).contains (point))
        toggleParameter ("positionLock");
    else if (juce::Rectangle<float> (706, 492, 150, 18).contains (point))
        toggleParameter ("frequencyLock");
    else if (juce::Rectangle<float> (706, 516, 150, 18).contains (point))
        toggleParameter ("invertArea");
    else if (juce::Rectangle<float> (706, 540, 150, 18).contains (point))
        toggleParameter ("noVisual");
    else if (juce::Rectangle<float> (110, 465, 135, 22).contains (point))
        activeControl = 4;
    else if (juce::Rectangle<float> (110, 497, 135, 22).contains (point))
        activeControl = 5;
    else if (juce::Rectangle<float> (110, 529, 135, 22).contains (point))
        activeControl = 6;
    else if (juce::Rectangle<float> (530, 465, 135, 22).contains (point))
        activeControl = 1;
    else if (juce::Rectangle<float> (530, 497, 135, 22).contains (point))
        activeControl = 2;
    else if (juce::Rectangle<float> (530, 529, 135, 22).contains (point))
        activeControl = 3;
    else
        activeControl = 0;

    if (activeControl != 0)
    {
        const auto isAdvanced = activeControl >= 4;
        const auto sliderX = isAdvanced ? 110.0f : 530.0f;
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
    const auto centre = centreParameter != nullptr ? centreParameter->load() : 0.0f;
    const auto width = widthParameter != nullptr ? widthParameter->load() : 0.5f;
    const auto frequencyCentre = frequencyCentreParameter != nullptr ? frequencyCentreParameter->load() : 0.5f;
    const auto frequencyWidth = frequencyWidthParameter != nullptr ? frequencyWidthParameter->load() : 0.67f;
    const auto current = activeScope == 1
        ? juce::Rectangle<float> (scope.getCentreX() + centre * scope.getWidth() * 0.5f
                                  - width * scope.getWidth() * 0.5f,
                                  scope.getY() + (1.0f - frequencyCentre - frequencyWidth * 0.5f)
                                  * scope.getHeight(),
                                  width * scope.getWidth(), frequencyWidth * scope.getHeight())
        : juce::Rectangle<float> (scope.getX() + centre * scope.getWidth()
                                  - width * scope.getWidth() * 0.5f,
                                  scope.getY() + (1.0f - frequencyCentre - frequencyWidth * 0.5f)
                                  * scope.getHeight(),
                                  width * scope.getWidth(), frequencyWidth * scope.getHeight());
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
    if (activeControl != 0)
    {
        const auto scale = juce::jmin (getWidth() / 825.0f, getHeight() / 612.0f);
        const auto point = (event.position - juce::Point<float> ((getWidth() - 825.0f * scale) * 0.5f,
                                                                  (getHeight() - 612.0f * scale) * 0.5f)) / scale;
        const auto sliderX = activeControl >= 4 ? 110.0f : 530.0f;
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
    const auto scale = juce::jmin (getWidth() / 825.0f, getHeight() / 612.0f);
    const auto point = (event.position - juce::Point<float> ((getWidth() - 825.0f * scale) * 0.5f,
                                                              (getHeight() - 612.0f * scale) * 0.5f)) / scale;
    const auto scope = activeScope == 1 ? juce::Rectangle<float> (20.0f, 68.0f, 560.0f, 340.0f)
                                        : juce::Rectangle<float> (615.0f, 68.0f, 196.0f, 340.0f);
    auto area = dragAreaAtStart;
    const auto delta = (point - dragStart) / scope.getWidth();
    if (dragEdges & 1) area.setLeft (area.getX() + delta.x);
    if (dragEdges & 2) area.setRight (area.getRight() + delta.x);
    if (dragEdges & 4) area.setTop (area.getY() + delta.y);
    if (dragEdges & 8) area.setBottom (area.getBottom() + delta.y);
    if (dragEdges == 0)
        area.setPosition (area.getPosition() + delta);
    if (auto* locked = processor.parameters.getRawParameterValue ("positionLock"))
        if (locked->load() > 0.5f)
            area.setX (dragAreaAtStart.getX());
    if (auto* locked = processor.parameters.getRawParameterValue ("frequencyLock"))
        if (locked->load() > 0.5f)
            area.setY (dragAreaAtStart.getY());
    area = area.constrainedWithin (juce::Rectangle<float> (0.0f, 0.0f, 1.0f, 1.0f));
    if (activeScope == 1) positionArea = area; else depthArea = area;
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
    if (activeScope == 1)
    {
        if (savedPositionArea.isEmpty()) { savedPositionArea = positionArea; positionArea = { 0.0f, 0.0f, 1.0f, 1.0f }; }
        else { positionArea = savedPositionArea; savedPositionArea = {}; }
    }
    else if (activeScope == 2)
    {
        if (savedDepthArea.isEmpty()) { savedDepthArea = depthArea; depthArea = { 0.0f, 0.0f, 1.0f, 1.0f }; }
        else { depthArea = savedDepthArea; savedDepthArea = {}; }
    }
    mouseUp (event);
    repaint();
}

namespace
{
void drawScope (juce::Graphics& graphics, OtokirisAudioProcessor& processor,
                juce::Rectangle<int> area, bool depth,
                float centre, float width, float frequencyCentre, float frequencyWidth)
{
    graphics.setColour (scopeBackground);
    graphics.fillRect (area);

    const auto selection = depth
        ? juce::Rectangle<int> ((int) (area.getX() + centre * area.getWidth()
                                      - width * area.getWidth() * 0.5f),
                                (int) (area.getY() + (1.0f - frequencyCentre - frequencyWidth * 0.5f)
                                      * area.getHeight()),
                                (int) (width * area.getWidth()),
                                (int) (frequencyWidth * area.getHeight()))
        : juce::Rectangle<int> ((int) (area.getX() + area.getWidth() * 0.5f + centre * area.getWidth() * 0.5f
                                      - width * area.getWidth() * 0.5f),
                                (int) (area.getY() + (1.0f - frequencyCentre - frequencyWidth * 0.5f)
                                      * area.getHeight()),
                                (int) (width * area.getWidth()),
                                (int) (frequencyWidth * area.getHeight()));

    const auto* noVisualParam = processor.parameters.getRawParameterValue ("noVisual");
    const bool noVisual = noVisualParam != nullptr && noVisualParam->load() > 0.5f;

    if (! noVisual)
    {
        juce::Image spectrogram (juce::Image::RGB, area.getWidth(), area.getHeight(), true);
        juce::Image::BitmapData pixels (spectrogram, juce::Image::BitmapData::writeOnly);
        for (int x = 0; x < spectrogram.getWidth(); ++x)
            for (int y = 0; y < spectrogram.getHeight(); ++y)
            {
                const auto frequencyPosition = 1.0f - (float) y
                                                 / (float) juce::jmax (1, spectrogram.getHeight() - 1);
                const auto band = juce::jlimit (0, 127,
                                                (int) std::round (frequencyPosition * 127.0f));
                const auto spatial = depth ? std::abs ((float) x
                                                       / (float) juce::jmax (1, spectrogram.getWidth() - 1)
                                                       - 0.20f)
                                           : std::abs ((float) x
                                                       / (float) juce::jmax (1, spectrogram.getWidth() - 1)
                                                       - 0.50f);
                const auto level = processor.getSpectrumLevel (band)
                                 * std::exp (-spatial * spatial * 42.0f);
                const auto shaped = std::sqrt (juce::jlimit (0.0f, 1.0f, level));
                pixels.setPixelColour (x, y, juce::Colour::fromHSV (
                    0.76f - shaped * 0.76f, 0.96f,
                    juce::jlimit (0.025f, 1.0f, 0.025f + shaped * 1.15f), 1.0f));
            }
        graphics.drawImageAt (spectrogram, area.getX(), area.getY());
    }

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
    for (const auto frequency : { 20.0, 50.0, 100.0, 200.0, 500.0,
                                  1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        if (frequency > nyquist)
            continue;
        const auto fraction = std::log (frequency / 20.0) / std::log (nyquist / 20.0);
        const auto y = (int) std::round (area.getBottom() - fraction * area.getHeight());
        graphics.setColour (frequency == 1000.0 || frequency == 10000.0
                                ? juce::Colour (0xff35cfa0)
                                : juce::Colour (0xff16785d));
        graphics.drawHorizontalLine (y, (float) area.getX(), (float) area.getRight());
    }

    graphics.setColour (areaFill.withAlpha (0.35f));
    graphics.fillRect (selection);
    graphics.setColour (areaColour);
    graphics.drawRect (selection, 3);
    graphics.setColour (juce::Colours::white);
    graphics.setFont (14.0f);
    const auto labelX = area.getX() + 5;
    for (const auto item : { std::pair<double, const char*> { 20000.0, "20k" },
                             { 10000.0, "10k" }, { 5000.0, "5k" },
                             { 1000.0, "1k" }, { 100.0, "100" } })
    {
        if (item.first > nyquist)
            continue;
        const auto fraction = std::log (item.first / 20.0) / std::log (nyquist / 20.0);
        const auto y = (int) std::round (area.getBottom() - fraction * area.getHeight());
        graphics.setColour (juce::Colour (0xaa202020));
        graphics.fillRect (labelX - 2, y - 10, 42, 19);
        graphics.setColour (juce::Colours::white);
        graphics.drawText (item.second, labelX, y - 9, 38, 18, juce::Justification::left);
    }
    graphics.setColour (juce::Colour (0xff1a9e85));
    graphics.setFont (34.0f);
    graphics.drawText (depth ? "F" : "L", area.getX() + 6, area.getBottom() - 50, 42, 45,
                       juce::Justification::left);
    graphics.drawText (depth ? "B" : "R", area.getRight() - 43, area.getBottom() - 50, 42, 45,
                       juce::Justification::right);
    graphics.setColour (juce::Colours::white);
    graphics.setFont (12.0f);
    const auto scaleLabels = depth ? std::array<const char*, 5> { "0", "25", "50", "75", "100" }
                                   : std::array<const char*, 5> { "-1", "-0.5", "0", "0.5", "1" };
    for (int i = 0; i < 5; ++i)
        graphics.drawText (scaleLabels[(size_t) i],
                           area.getX() + i * (area.getWidth() - 24) / 4 - 5,
                           area.getBottom() + 3, 32, 16, juce::Justification::centred);

}
}
