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
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtokirisAudioProcessorEditor)
};
