#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

OtokirisAudioProcessor::OtokirisAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputFifo[0].resize (fftSize, 0.0f);
    inputFifo[1].resize (fftSize, 0.0f);
    outputOlaBuffer[0].resize (olaBufferSize, 0.0f);
    outputOlaBuffer[1].resize (olaBufferSize, 0.0f);

    for (auto& level : spectrumLevel)
        level.store (0.0f);

    updateBinMapping (44100.0);
}

void OtokirisAudioProcessor::getHeatmapData (std::array<float, numBands * numSlots>& outPan,
                                             std::array<float, numBands * numSlots>& outDepth) const noexcept
{
    const int idx = activeGridIndex.load (std::memory_order_acquire);
    outPan = heatmapGrids[(size_t) idx].pan;
    outDepth = heatmapGrids[(size_t) idx].depth;
}

float OtokirisAudioProcessor::getSpectrumLevel (int index) const noexcept
{
    return index >= 0 && index < numBands ? spectrumLevel[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout OtokirisAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputVolume", "Output Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "positionScale", "Position Scale", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "colourScale", "Colour Scale", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "positionCentre", "Position Centre", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "positionWidth", "Position Width", juce::NormalisableRange<float> (0.05f, 1.0f), 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "frequencyCentre", "Frequency Centre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "frequencyWidth", "Frequency Width", juce::NormalisableRange<float> (0.02f, 1.0f), 0.67f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "depthCentre", "Depth Centre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "depthWidth", "Depth Width", juce::NormalisableRange<float> (0.02f, 1.0f), 0.47f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "positionLock", "Position Lock", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "frequencyLock", "Frequency Lock", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "invertArea", "Invert Area", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "noVisual", "No Visual", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "extractionAmount", "Extraction Amount", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "maskSoftness", "Mask Softness", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "reverbReduction", "Reverb Reduction", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    return { params.begin(), params.end() };
}

void OtokirisAudioProcessor::updateBinMapping (double sampleRate)
{
    const int numBins = fftSize / 2 + 1;
    binMap.resize ((size_t) numBins);
    const double nyquist = sampleRate * 0.5;
    const double logMin = std::log (20.0);
    const double logMax = std::log (juce::jmax (20.0, nyquist));
    const double logRange = juce::jmax (1e-6, logMax - logMin);

    for (int k = 0; k < numBins; ++k)
    {
        const float freq = (float) (k * sampleRate / fftSize);
        float logPos = 0.0f;
        if (freq > 20.0f)
            logPos = juce::jlimit (0.0f, 1.0f, (float) ((std::log ((double) freq) - logMin) / logRange));

        const int band = juce::jlimit (0, numBands - 1, (int) std::round (logPos * (numBands - 1)));
        binMap[(size_t) k] = { band, freq, logPos };
    }
}

void OtokirisAudioProcessor::resetSTFTBuffers()
{
    inputFifo[0].assign (fftSize, 0.0f);
    inputFifo[1].assign (fftSize, 0.0f);
    outputOlaBuffer[0].assign (olaBufferSize, 0.0f);
    outputOlaBuffer[1].assign (olaBufferSize, 0.0f);
    fifoWritePos = 0;
    olaReadPos = 0;
    samplesSinceLastHop = 0;
    leftFFTData.fill (0.0f);
    rightFFTData.fill (0.0f);

    for (auto& grid : heatmapGrids)
    {
        grid.pan.fill (0.0f);
        grid.depth.fill (0.0f);
    }
    currentAccumulator.pan.fill (0.0f);
    currentAccumulator.depth.fill (0.0f);
    for (auto& lvl : spectrumLevel)
        lvl.store (0.0f);
}

void OtokirisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    const auto sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysisSampleRate.store (sr);
    setLatencySamples (fftSize);

    inputFifo[0].resize (fftSize, 0.0f);
    inputFifo[1].resize (fftSize, 0.0f);
    outputOlaBuffer[0].resize (olaBufferSize, 0.0f);
    outputOlaBuffer[1].resize (olaBufferSize, 0.0f);

    updateBinMapping (sr);
    resetSTFTBuffers();

    haveHostPosition = false;
    hostPlaying.store (false);
}

void OtokirisAudioProcessor::releaseResources()
{
}

bool OtokirisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

void OtokirisAudioProcessor::processSTFTFrame()
{
    // Extract last fftSize samples from circular inputFifo with analysis window
    for (int i = 0; i < fftSize; ++i)
    {
        const int readIdx = (fifoWritePos - fftSize + i + fftSize) % fftSize;
        leftFFTData[(size_t) i] = inputFifo[0][(size_t) readIdx];
        rightFFTData[(size_t) i] = inputFifo[1][(size_t) readIdx];
    }
    std::fill (leftFFTData.begin() + fftSize, leftFFTData.end(), 0.0f);
    std::fill (rightFFTData.begin() + fftSize, rightFFTData.end(), 0.0f);

    window.multiplyWithWindowingTable (leftFFTData.data(), fftSize);
    window.multiplyWithWindowingTable (rightFFTData.data(), fftSize);

    fft.performRealOnlyForwardTransform (leftFFTData.data());
    fft.performRealOnlyForwardTransform (rightFFTData.data());

    // Parameters
    const auto* freqCentreParam = parameters.getRawParameterValue ("frequencyCentre");
    const auto* freqWidthParam = parameters.getRawParameterValue ("frequencyWidth");
    const auto* panCentreParam = parameters.getRawParameterValue ("positionCentre");
    const auto* panWidthParam = parameters.getRawParameterValue ("positionWidth");
    const auto* depthCentreParam = parameters.getRawParameterValue ("depthCentre");
    const auto* depthWidthParam = parameters.getRawParameterValue ("depthWidth");
    const auto* extAmountParam = parameters.getRawParameterValue ("extractionAmount");
    const auto* maskSoftParam = parameters.getRawParameterValue ("maskSoftness");
    const auto* invertParam = parameters.getRawParameterValue ("invertArea");
    const auto* reverbRedParam = parameters.getRawParameterValue ("reverbReduction");

    const float freqCentre = freqCentreParam != nullptr ? freqCentreParam->load() : 0.5f;
    const float freqWidth = freqWidthParam != nullptr ? freqWidthParam->load() : 0.67f;
    const float panCentre = panCentreParam != nullptr ? panCentreParam->load() : 0.0f;
    const float panWidth = panWidthParam != nullptr ? panWidthParam->load() : 0.55f;
    const float depthCentre = depthCentreParam != nullptr ? depthCentreParam->load() : 0.5f;
    const float depthWidth = depthWidthParam != nullptr ? depthWidthParam->load() : 0.47f;
    const float extAmount = extAmountParam != nullptr ? extAmountParam->load() : 1.0f;
    const float maskSoftness = juce::jmax (0.005f, maskSoftParam != nullptr ? maskSoftParam->load() : 0.15f);
    const bool invert = invertParam != nullptr && invertParam->load() > 0.5f;
    const float reverbRed = reverbRedParam != nullptr ? reverbRedParam->load() : 0.0f;

    // Decay accumulator for smooth heatmap visuals
    for (float& v : currentAccumulator.pan)
        v *= 0.72f;
    for (float& v : currentAccumulator.depth)
        v *= 0.72f;

    std::array<float, numBands> currentBandEnergy {};

    const int numBins = fftSize / 2 + 1;
    for (int k = 0; k < numBins; ++k)
    {
        float realL = 0.0f, imagL = 0.0f;
        float realR = 0.0f, imagR = 0.0f;

        if (k == 0)
        {
            realL = leftFFTData[0];
            realR = rightFFTData[0];
        }
        else if (k == fftSize / 2)
        {
            realL = leftFFTData[(size_t) fftSize];
            realR = rightFFTData[(size_t) fftSize];
        }
        else
        {
            realL = leftFFTData[(size_t) (2 * k)];
            imagL = leftFFTData[(size_t) (2 * k + 1)];
            realR = rightFFTData[(size_t) (2 * k)];
            imagR = rightFFTData[(size_t) (2 * k + 1)];
        }

        const float magL = std::hypot (realL, imagL);
        const float magR = std::hypot (realR, imagR);
        const float sumMag = magL + magR + 1e-6f;

        // Pan estimation: -1 (Left) to +1 (Right)
        const float pan = (magR - magL) / sumMag;

        // Depth / Diffuseness estimation from Mid/Side components
        const float realMid = (realL + realR) * 0.5f;
        const float imagMid = (imagL + imagR) * 0.5f;
        const float realSide = (realL - realR) * 0.5f;
        const float imagSide = (imagL - imagR) * 0.5f;
        const float magMid = std::hypot (realMid, imagMid);
        const float magSide = std::hypot (realSide, imagSide);
        const float depth = magSide / (magMid + magSide + 1e-6f); // 0 (direct/front) to 1 (diffuse/back)

        // Visual Heatmap accumulation
        const auto& bInfo = binMap[(size_t) k];
        const int band = bInfo.band;
        const float avgMag = 0.5f * (magL + magR);
        const float visualMag = juce::jlimit (0.0f, 1.0f, avgMag / 32.0f);

        currentBandEnergy[(size_t) band] = juce::jmax (currentBandEnergy[(size_t) band], visualMag);

        const int panSlot = juce::jlimit (0, numSlots - 1, (int) std::round ((pan + 1.0f) * 0.5f * (numSlots - 1)));
        const int depthSlot = juce::jlimit (0, numSlots - 1, (int) std::round (depth * (numSlots - 1)));
        const size_t panIdx = (size_t) (band * numSlots + panSlot);
        const size_t depthIdx = (size_t) (band * numSlots + depthSlot);

        currentAccumulator.pan[panIdx] = juce::jmax (currentAccumulator.pan[panIdx], visualMag);
        currentAccumulator.depth[depthIdx] = juce::jmax (currentAccumulator.depth[depthIdx], visualMag);

        // STFT Spectral Mask calculation
        const float logPos = bInfo.logPos;
        const float freqDist = std::abs (logPos - freqCentre);
        const float freqMask = juce::jlimit (0.0f, 1.0f, (freqWidth * 0.5f - freqDist) / maskSoftness + 0.5f);

        const float panDist = std::abs (pan - panCentre);
        const float panMask = juce::jlimit (0.0f, 1.0f, (panWidth * 0.5f - panDist) / maskSoftness + 0.5f);

        const float depthDist = std::abs (depth - depthCentre);
        float depthMask = juce::jlimit (0.0f, 1.0f, (depthWidth * 0.5f - depthDist) / maskSoftness + 0.5f);

        // Reverb reduction suppresses high-depth (ambient/diffuse) components
        if (reverbRed > 0.0f)
            depthMask *= juce::jmax (0.0f, 1.0f - reverbRed * 0.6f * depth);

        float mask = freqMask * panMask * depthMask;
        if (invert)
            mask = 1.0f - mask;

        const float binGain = (1.0f - extAmount) + extAmount * mask;

        // Apply gain to complex spectrum
        if (k == 0)
        {
            leftFFTData[0] = realL * binGain;
            rightFFTData[0] = realR * binGain;
        }
        else if (k == fftSize / 2)
        {
            leftFFTData[(size_t) fftSize] = realL * binGain;
            rightFFTData[(size_t) fftSize] = realR * binGain;
        }
        else
        {
            leftFFTData[(size_t) (2 * k)] = realL * binGain;
            leftFFTData[(size_t) (2 * k + 1)] = imagL * binGain;
            rightFFTData[(size_t) (2 * k)] = realR * binGain;
            rightFFTData[(size_t) (2 * k + 1)] = imagR * binGain;
        }
    }

    // Update spectrumLevel array for single-band meter access
    for (size_t b = 0; b < numBands; ++b)
        spectrumLevel[b].store (currentBandEnergy[b], std::memory_order_relaxed);

    // Swap heatmap buffer for UI thread
    const int nextIdx = 1 - activeGridIndex.load (std::memory_order_relaxed);
    heatmapGrids[(size_t) nextIdx] = currentAccumulator;
    activeGridIndex.store (nextIdx, std::memory_order_release);

    // Inverse STFT
    fft.performRealOnlyInverseTransform (leftFFTData.data());
    fft.performRealOnlyInverseTransform (rightFFTData.data());

    // Synthesis Hann window & OLA scaling (2/3 / fftSize for 75% Hann OLA)
    window.multiplyWithWindowingTable (leftFFTData.data(), fftSize);
    window.multiplyWithWindowingTable (rightFFTData.data(), fftSize);
    const float olaScale = (2.0f / 3.0f) / (float) fftSize;

    // Overlap-add into output circular buffer
    const int targetPos = (olaReadPos + fftSize) % olaBufferSize;
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (targetPos + i) % olaBufferSize;
        outputOlaBuffer[0][(size_t) idx] += leftFFTData[(size_t) i] * olaScale;
        outputOlaBuffer[1][(size_t) idx] += rightFFTData[(size_t) i] * olaScale;
    }
}

void OtokirisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    bool isPlaying = true;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            const auto playing = position->getIsPlaying();
            const auto wasPlaying = hostPlaying.load();
            hostPlaying.store (playing);

            const auto samplePos = position->getTimeInSamples();
            const auto currentSample = samplePos.hasValue() ? *samplePos : lastHostSample;
            const auto discontinuity = samplePos.hasValue() && haveHostPosition
                && (currentSample < lastHostSample
                    || currentSample - lastHostSample > (juce::int64) numSamples * 4);

            if (! haveHostPosition || discontinuity || (playing && ! wasPlaying))
                resetSTFTBuffers();

            lastHostSample = currentSample + numSamples;
            haveHostPosition = true;
            isPlaying = playing;
        }
    }

    const int numChannels = buffer.getNumChannels();
    const auto* leftIn = buffer.getReadPointer (0);
    const auto* rightIn = numChannels > 1 ? buffer.getReadPointer (1) : leftIn;

    auto* leftOut = buffer.getWritePointer (0);
    auto* rightOut = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const auto* gainParam = parameters.getRawParameterValue ("outputVolume");
    const float outputGain = gainParam != nullptr ? gainParam->load() : 1.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        // Push incoming samples to input circular FIFO
        inputFifo[0][(size_t) fifoWritePos] = leftIn[s];
        inputFifo[1][(size_t) fifoWritePos] = rightIn[s];
        fifoWritePos = (fifoWritePos + 1) % fftSize;

        if (isPlaying)
        {
            if (++samplesSinceLastHop >= hopSize)
            {
                samplesSinceLastHop = 0;
                processSTFTFrame();
            }
        }

        // Pull processed sample from output overlap-add buffer
        const float outL = outputOlaBuffer[0][(size_t) olaReadPos] * outputGain;
        const float outR = outputOlaBuffer[1][(size_t) olaReadPos] * outputGain;

        outputOlaBuffer[0][(size_t) olaReadPos] = 0.0f;
        outputOlaBuffer[1][(size_t) olaReadPos] = 0.0f;
        olaReadPos = (olaReadPos + 1) % olaBufferSize;

        leftOut[s] = outL;
        if (rightOut != nullptr)
            rightOut[s] = outR;
    }
}

juce::AudioProcessorEditor* OtokirisAudioProcessor::createEditor()
{
    return new OtokirisAudioProcessorEditor (*this);
}

void OtokirisAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void OtokirisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OtokirisAudioProcessor();
}
