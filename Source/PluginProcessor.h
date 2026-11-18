#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

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
    const juce::String getName() const override { return "OtokiriScope"; }
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

    static constexpr int numBands = 128;
    static constexpr int numSlots = 128;

    struct HeatmapGrid
    {
        std::array<float, numBands * numSlots> pan {};
        std::array<float, numBands * numSlots> depth {};
    };

    void getHeatmapData (std::array<float, numBands * numSlots>& outPan,
                         std::array<float, numBands * numSlots>& outDepth) const noexcept;
    float getSpectrumLevel (int index) const noexcept;
    double getAnalysisSampleRate() const noexcept { return analysisSampleRate.load(); }
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }

    struct PlayheadInfo
    {
        bool isPlaying = false;
        double timeSeconds = 0.0;
        double bpm = 120.0;
        int timeSigNumerator = 4;
        int timeSigDenominator = 4;
        juce::int64 barNumber = 0;
        double ppqPosition = 0.0;
    };
    PlayheadInfo getPlayheadInfo() const noexcept;

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder; // 4096
    static constexpr int hopSize = 1024;          // 75% overlap
    static constexpr int olaBufferSize = fftSize * 4;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize,
        juce::dsp::WindowingFunction<float>::hann, false };

    std::array<float, fftSize * 2> leftFFTData {};
    std::array<float, fftSize * 2> rightFFTData {};

    std::array<std::vector<float>, 2> inputFifo;
    int fifoWritePos = 0;

    std::array<std::vector<float>, 2> outputOlaBuffer;
    int olaReadPos = 0;
    int samplesSinceLastHop = 0;

    std::array<HeatmapGrid, 2> heatmapGrids;
    std::atomic<int> activeGridIndex { 0 };
    HeatmapGrid currentAccumulator;
    std::array<std::atomic<float>, numBands> spectrumLevel {};

    struct BinInfo
    {
        int band;
        float freqHz;
        float logPos;
    };
    std::vector<BinInfo> binMap;
    std::vector<float> smoothedMaskGain;
    std::vector<float> rawMask;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain;

    std::atomic<double> analysisSampleRate { 44100.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> hostTimeSeconds { 0.0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostTimeSigNum { 4 };
    std::atomic<int> hostTimeSigDenom { 4 };
    std::atomic<juce::int64> hostBarNumber { 0 };
    std::atomic<double> hostPpqPos { 0.0 };
    juce::int64 lastHostSample = 0;
    bool haveHostPosition = false;

    void processSTFTFrame();
    void resetSTFTBuffers();
    void updateBinMapping (double sampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OtokirisAudioProcessor)
};
