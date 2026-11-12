#pragma once

#include "PluginProcessor.h"

class OtokirisAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit OtokirisAudioProcessorEditor (OtokirisAudioProcessor&);
    ~OtokirisAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void timerCallback() override;

private:
    OtokirisAudioProcessor& processor;

    juce::Image panImage { juce::Image::RGB, OtokirisAudioProcessor::numSlots, OtokirisAudioProcessor::numBands, true };
    juce::Image depthImage { juce::Image::RGB, OtokirisAudioProcessor::numSlots, OtokirisAudioProcessor::numBands, true };

    std::array<float, OtokirisAudioProcessor::numBands * OtokirisAudioProcessor::numSlots> rawPanData {};
    std::array<float, OtokirisAudioProcessor::numBands * OtokirisAudioProcessor::numSlots> rawDepthData {};

    bool draggingPosition = false;
    juce::Rectangle<float> positionArea { 0.25f, 0.18f, 0.55f, 0.67f };
    juce::Rectangle<float> depthArea { 0.01f, 0.18f, 0.47f, 0.67f };
    juce::Rectangle<float> savedPositionArea;
    juce::Rectangle<float> savedDepthArea;
    juce::Point<float> dragStart;
    juce::Rectangle<float> dragAreaAtStart;
    int activeScope = 0;
    int dragEdges = 0;
    int activeControl = 0;

    // A/B snapshot
    struct StateSnapshot
    {
        float posCentre = 0.0f;
        float posWidth = 0.55f;
        float freqCentre = 0.5f;
        float freqWidth = 0.67f;
        float depthCentre = 0.5f;
        float depthWidth = 0.47f;
        bool invert = false;
        bool hasValue = false;
    };
    StateSnapshot slotA;
    StateSnapshot slotB;
    bool isSlotB = false;

    void updateHeatmapImages();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtokirisAudioProcessorEditor)
};
