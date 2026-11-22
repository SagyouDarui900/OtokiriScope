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
    bool hasSavedPosition = false;
    float savedPositionCentre = 0.0f;
    float savedPositionWidth = 0.55f;
    float savedPositionFreqCentre = 0.5f;
    float savedPositionFreqWidth = 0.67f;

    bool hasSavedDepth = false;
    float savedDepthCentre = 0.5f;
    float savedDepthWidth = 1.0f;
    float savedDepthFreqCentre = 0.5f;
    float savedDepthFreqWidth = 0.67f;

    juce::Point<float> dragStart;
    juce::Rectangle<float> dragAreaAtStart;
    int activeScope = 0;
    int dragEdges = 0;
    int activeControl = 0;

    juce::Rectangle<float> getScopeNormalizedRect (int scopeIndex) const;

    // A/B snapshot
    struct StateSnapshot
    {
        float posCentre = 0.0f;
        float posWidth = 0.55f;
        float freqCentre = 0.5f;
        float freqWidth = 0.67f;
        float depthCentre = 0.5f;
        float depthWidth = 1.0f;
        bool invert = false;
        bool hasValue = false;
    };
    StateSnapshot slotA;
    StateSnapshot slotB;
    bool isSlotB = false;

    void updateHeatmapImages();
    void applyPreset (int presetId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtokirisAudioProcessorEditor)
};
