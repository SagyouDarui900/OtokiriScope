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
    const int idx = readyGridIndex.load (std::memory_order_acquire);
    uiReadingIndex.store (idx, std::memory_order_release);
    outPan = heatmapGrids[(size_t) idx].pan;
    outDepth = heatmapGrids[(size_t) idx].depth;
    uiReadingIndex.store (-1, std::memory_order_release);
}

float OtokirisAudioProcessor::getSpectrumLevel (int index) const noexcept
{
    return index >= 0 && index < numBands ? spectrumLevel[(size_t) index].load (std::memory_order_relaxed) : 0.0f;
}

OtokirisAudioProcessor::PlayheadInfo OtokirisAudioProcessor::getPlayheadInfo() const noexcept
{
    PlayheadInfo info;
    info.isPlaying = hostPlaying.load (std::memory_order_relaxed);
    info.timeSeconds = hostTimeSeconds.load (std::memory_order_relaxed);
    info.bpm = hostBpm.load (std::memory_order_relaxed);
    info.timeSigNumerator = hostTimeSigNum.load (std::memory_order_relaxed);
    info.timeSigDenominator = hostTimeSigDenom.load (std::memory_order_relaxed);
    info.barNumber = hostBarNumber.load (std::memory_order_relaxed);
    info.ppqPosition = hostPpqPos.load (std::memory_order_relaxed);
    return info;
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
        "depthWidth", "Depth Width", juce::NormalisableRange<float> (0.02f, 1.0f), 1.0f));
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
    smoothedMaskGain.assign ((size_t) numBins, 1.0f);
    rawMask.assign ((size_t) numBins, 1.0f);
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
    smoothedMaskGain.assign ((size_t) (fftSize / 2 + 1), 1.0f);
    rawMask.assign ((size_t) (fftSize / 2 + 1), 1.0f);
    for (auto& lvl : spectrumLevel)
        lvl.store (0.0f);
}

void OtokirisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    const auto sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysisSampleRate.store (sr);
    setLatencySamples (fftSize - 1);

    inputFifo[0].resize (fftSize, 0.0f);
    inputFifo[1].resize (fftSize, 0.0f);
    outputOlaBuffer[0].resize (olaBufferSize, 0.0f);
    outputOlaBuffer[1].resize (olaBufferSize, 0.0f);

    updateBinMapping (sr);
    resetSTFTBuffers();

    smoothedOutputGain.reset (sr, 0.02);
    smoothedOutputGain.setCurrentAndTargetValue (1.0f);

    haveHostPosition = false;
    hostPlaying.store (false);
}

void OtokirisAudioProcessor::releaseResources()
{
}

bool OtokirisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput  = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    if (mainInput != juce::AudioChannelSet::mono() && mainInput != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

namespace
{
inline float fastHypot (float r, float i) noexcept
{
    return std::sqrt (r * r + i * i + 1e-12f);
}

// Smooth Hermite roll-off curve starting at rectangle boundary.
// Guarantees 1.0 everywhere inside the selected range and at center, with no edge clipping.
inline float smoothMask (float dist, float halfSpan, float softness) noexcept
{
    if (dist <= halfSpan)
        return 1.0f;
    const float s = juce::jmax (0.001f, softness);
    if (dist >= halfSpan + s)
        return 0.0f;
    const float t = (halfSpan + s - dist) / s;
    return t * t * (3.0f - 2.0f * t);
}

// Non-linear pan warp based on positionScale (0.5 is linear, >0.5 expands center resolution)
inline float warpPan (float p, float exponent) noexcept
{
    if (std::abs (p) < 1e-6f || std::abs (exponent - 1.0f) < 1e-4f)
        return p;
    const float sign = p >= 0.0f ? 1.0f : -1.0f;
    return sign * std::pow (std::abs (p), exponent);
}
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
    const auto* panScaleParam = parameters.getRawParameterValue ("positionScale");
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
    const float panScale = panScaleParam != nullptr ? panScaleParam->load() : 0.5f;
    // panScale in [0, 1]. 0.5 is linear. >0.5 expands center resolution, <0.5 expands edge resolution
    const float panExponent = std::pow (2.0f, (0.5f - panScale) * 2.5f);
    const float depthCentre = depthCentreParam != nullptr ? depthCentreParam->load() : 0.5f;
    const float depthWidth = depthWidthParam != nullptr ? depthWidthParam->load() : 1.0f;
    const float extAmount = extAmountParam != nullptr ? extAmountParam->load() : 1.0f;
    const float maskSoftness = juce::jmax (0.005f, maskSoftParam != nullptr ? maskSoftParam->load() : 0.15f);
    const bool invert = invertParam != nullptr && invertParam->load() > 0.5f;
    const float reverbRed = reverbRedParam != nullptr ? reverbRedParam->load() : 0.0f;

    const auto* noVisualParam = parameters.getRawParameterValue ("noVisual");
    const bool noVisual = noVisualParam != nullptr && noVisualParam->load() > 0.5f;
    const bool updateHeatmap = !noVisual && hostPlaying.load (std::memory_order_relaxed);
    if (updateHeatmap)
    {
        // Decay accumulator for responsive transients
        for (float& v : currentAccumulator.pan)
            v *= 0.60f;
        for (float& v : currentAccumulator.depth)
            v *= 0.60f;
    }

    std::array<float, numBands> currentBandEnergy {};

    const int numBins = fftSize / 2 + 1;
    for (int k = 0; k < numBins; ++k)
    {
        const float realL = leftFFTData[(size_t) (2 * k)];
        const float imagL = leftFFTData[(size_t) (2 * k + 1)];
        const float realR = rightFFTData[(size_t) (2 * k)];
        const float imagR = rightFFTData[(size_t) (2 * k + 1)];

        const float magL = fastHypot (realL, imagL);
        const float magR = fastHypot (realR, imagR);
        const float sumMag = magL + magR + 1e-6f;

        // Pan estimation: -1 (Left) to +1 (Right)
        const float rawPan = (magR - magL) / sumMag;
        const float pan = warpPan (rawPan, panExponent);

        // Depth / Diffuseness estimation from Mid/Side components
        const float realMid = (realL + realR) * 0.5f;
        const float imagMid = (imagL + imagR) * 0.5f;
        const float realSide = (realL - realR) * 0.5f;
        const float imagSide = (imagL - imagR) * 0.5f;
        const float magMid = fastHypot (realMid, imagMid);
        const float magSide = fastHypot (realSide, imagSide);
        const float depth = magSide / (magMid + magSide + 1e-6f); // 0 (direct/front) to 1 (diffuse/back)

        // Visual Heatmap accumulation with dynamic log-compression for punchy, crisp visuals
        const auto& bInfo = binMap[(size_t) k];
        const int band = bInfo.band;
        const float avgMag = 0.5f * (magL + magR);

        if (updateHeatmap)
        {
            const float visualMag = juce::jlimit (0.0f, 1.0f, std::log1pf (avgMag * 15.0f) / 4.5f);
            currentBandEnergy[(size_t) band] = juce::jmax (currentBandEnergy[(size_t) band], visualMag);

            const int panSlot = juce::jlimit (0, numSlots - 1, (int) std::round ((pan + 1.0f) * 0.5f * (numSlots - 1)));
            const int depthSlot = juce::jlimit (0, numSlots - 1, (int) std::round (depth * (numSlots - 1)));
            const size_t panIdx = (size_t) (band * numSlots + panSlot);
            const size_t depthIdx = (size_t) (band * numSlots + depthSlot);

            currentAccumulator.pan[panIdx] = juce::jmax (currentAccumulator.pan[panIdx], visualMag);
            currentAccumulator.depth[depthIdx] = juce::jmax (currentAccumulator.depth[depthIdx], visualMag);
        }

        // STFT Spectral Mask calculation with smooth Hermite roll-off
        const float logPos = bInfo.logPos;
        const float freqDist = std::abs (logPos - freqCentre);
        // When freqWidth >= 0.98, full spectrum pass (avoids edge attenuation at max width)
        const float freqMask = (freqWidth >= 0.98f) ? 1.0f
                                                     : smoothMask (freqDist, freqWidth * 0.5f, maskSoftness);

        // Pan mask: pan is in [-1, +1] (total span = 2.0), so normalized width [0..1] represents half-span [0..1]
        const float panDist = std::abs (pan - panCentre);
        const float panHalfSpan = panWidth * 1.0f;
        // When panWidth >= 0.98, full pan pass (avoids edge attenuation at max width)
        const float panMask = (panWidth >= 0.98f) ? 1.0f
                                                   : smoothMask (panDist, panHalfSpan, maskSoftness);

        // Depth mask: smooth blend from full-pass to active mask as depthWidth decreases from 1.0 to 0.90
        // This avoids the hard switch artefact at the bypass threshold.
        float depthMask = 1.0f;
        {
            const float depthDist = std::abs (depth - depthCentre);
            const float activeMask = smoothMask (depthDist, depthWidth * 0.5f, maskSoftness);
            // Crossfade: fully bypassed at depthWidth >= 1.0, fully active at depthWidth <= 0.90
            const float blend = juce::jlimit (0.0f, 1.0f, (1.0f - depthWidth) / 0.10f);
            depthMask = 1.0f + blend * (activeMask - 1.0f);
        }

        // Reverb reduction suppresses high-depth (ambient/diffuse) components
        if (reverbRed > 0.0f)
            depthMask *= juce::jmax (0.0f, 1.0f - reverbRed * 0.6f * depth);

        float mask = freqMask * panMask * depthMask;
        if (invert)
            mask = 1.0f - mask;

        rawMask[(size_t) k] = mask;
    }

    // Step 2: Spectral smoothing (3-tap kernel) + temporal smoothing to eliminate musical noise / fluttering
    for (int k = 0; k < numBins; ++k)
    {
        const float mPrev = k > 0 ? rawMask[(size_t) (k - 1)] : rawMask[0];
        const float mCurr = rawMask[(size_t) k];
        const float mNext = k + 1 < numBins ? rawMask[(size_t) (k + 1)] : mCurr;
        const float smoothSpectralMask = 0.20f * mPrev + 0.60f * mCurr + 0.20f * mNext;

        const float targetGain = (1.0f - extAmount) + extAmount * smoothSpectralMask;
        float& prevGain = smoothedMaskGain[(size_t) k];
        // Fast attack (0.75) for crisp transients; gentle release (0.35) for smooth, natural decay
        const float coeff = targetGain > prevGain ? 0.75f : 0.35f;
        prevGain += coeff * (targetGain - prevGain);
        const float binGain = prevGain;

        leftFFTData[(size_t) (2 * k)]     *= binGain;
        leftFFTData[(size_t) (2 * k + 1)] *= binGain;
        rightFFTData[(size_t) (2 * k)]     *= binGain;
        rightFFTData[(size_t) (2 * k + 1)] *= binGain;
    }

    // Update spectrumLevel array for single-band meter access and swap heatmap
    if (updateHeatmap)
    {
        for (size_t b = 0; b < numBands; ++b)
            spectrumLevel[b].store (currentBandEnergy[b], std::memory_order_relaxed);

        // Triple-buffering: select a buffer that is neither currently ready nor being read by GUI
        const int currentReady = readyGridIndex.load (std::memory_order_relaxed);
        const int readingIdx = uiReadingIndex.load (std::memory_order_relaxed);
        int nextIdx = 0;
        for (int i = 0; i < 3; ++i)
        {
            if (i != currentReady && i != readingIdx)
            {
                nextIdx = i;
                break;
            }
        }
        heatmapGrids[(size_t) nextIdx] = currentAccumulator;
        readyGridIndex.store (nextIdx, std::memory_order_release);
    }

    // Inverse STFT
    fft.performRealOnlyInverseTransform (leftFFTData.data());
    fft.performRealOnlyInverseTransform (rightFFTData.data());

    // Synthesis Hann window & OLA scaling (2/3 for 75% Hann OLA; JUCE IFFT already normalizes by 1/fftSize)
    window.multiplyWithWindowingTable (leftFFTData.data(), fftSize);
    window.multiplyWithWindowingTable (rightFFTData.data(), fftSize);
    const float olaScale = 2.0f / 3.0f;

    // Overlap-add into output circular buffer (aligned with olaReadPos for exact fftSize - 1 latency)
    const int targetPos = olaReadPos;
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

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            const auto playing = position->getIsPlaying() || isNonRealtime();
            const auto wasPlaying = hostPlaying.load();
            hostPlaying.store (playing);

            const auto samplePos = position->getTimeInSamples();
            const auto currentSample = samplePos.hasValue() ? *samplePos : lastHostSample;
            const auto discontinuity = playing && wasPlaying && samplePos.hasValue() && haveHostPosition
                && (currentSample < lastHostSample - (juce::int64) numSamples
                    || currentSample - lastHostSample > (juce::int64) numSamples * 4);

            if ((playing && ! wasPlaying) || discontinuity)
                resetSTFTBuffers();

            lastHostSample = currentSample + numSamples;
            haveHostPosition = true;

            if (auto seconds = position->getTimeInSeconds())
                hostTimeSeconds.store (*seconds, std::memory_order_relaxed);
            if (auto bpmVal = position->getBpm())
                hostBpm.store (*bpmVal, std::memory_order_relaxed);
            if (auto sig = position->getTimeSignature())
            {
                hostTimeSigNum.store (sig->numerator, std::memory_order_relaxed);
                hostTimeSigDenom.store (sig->denominator, std::memory_order_relaxed);
            }
            if (auto bars = position->getBarCount())
                hostBarNumber.store (*bars, std::memory_order_relaxed);
            if (auto ppq = position->getPpqPosition())
                hostPpqPos.store (*ppq, std::memory_order_relaxed);
        }
    }

    const int numChannels = buffer.getNumChannels();
    const auto* leftIn = buffer.getReadPointer (0);
    const auto* rightIn = numChannels > 1 ? buffer.getReadPointer (1) : leftIn;

    auto* leftOut = buffer.getWritePointer (0);
    auto* rightOut = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const auto* gainParam = parameters.getRawParameterValue ("outputVolume");
    const float outputGain = gainParam != nullptr ? gainParam->load() : 1.0f;
    smoothedOutputGain.setTargetValue (outputGain);

    for (int s = 0; s < numSamples; ++s)
    {
        // Push incoming samples to input circular FIFO
        inputFifo[0][(size_t) fifoWritePos] = leftIn[s];
        inputFifo[1][(size_t) fifoWritePos] = rightIn[s];
        fifoWritePos = (fifoWritePos + 1) % fftSize;

        if (++samplesSinceLastHop >= hopSize)
        {
            samplesSinceLastHop = 0;
            processSTFTFrame();
        }

        // Pull processed sample from output overlap-add buffer (sample-accurate gain smoothing)
        const float gain = smoothedOutputGain.getNextValue();
        const float outL = outputOlaBuffer[0][(size_t) olaReadPos] * gain;
        const float outR = outputOlaBuffer[1][(size_t) olaReadPos] * gain;

        outputOlaBuffer[0][(size_t) olaReadPos] = 0.0f;
        outputOlaBuffer[1][(size_t) olaReadPos] = 0.0f;
        olaReadPos = (olaReadPos + 1) % olaBufferSize;

        if (rightOut != nullptr)
        {
            leftOut[s] = outL;
            rightOut[s] = outR;
        }
        else
        {
            // Mono output mixdown
            leftOut[s] = (outL + outR) * 0.5f;
        }
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
