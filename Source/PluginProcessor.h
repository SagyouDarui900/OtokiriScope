#pragma once

#include <JuceHeader.h>
#include <array>

class OtokirisAudioProcessor final : public juce::AudioProcessor
{
public:
    OtokirisAudioProcessor();
    ~OtokirisAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Otokiris"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float getSpectrumLevel (int index) const noexcept;
    double getAnalysisSampleRate() const noexcept { return analysisSampleRate.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize> analysisFifo {};
    int analysisFifoIndex = 0;
    std::array<std::atomic<float>, 128> spectrum {};
    int analysisSampleCounter = 0;
    std::atomic<double> analysisSampleRate { 44100.0 };
    std::atomic<bool> hostPlaying { false };
    juce::int64 lastHostSample = 0;
    bool haveHostPosition = false;
    juce::dsp::WindowingFunction<float> window { fftSize,
        juce::dsp::WindowingFunction<float>::hann, false };
    std::array<juce::dsp::IIR::Filter<float>, 2> highPassFilters;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowPassFilters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtokirisAudioProcessor)
};
