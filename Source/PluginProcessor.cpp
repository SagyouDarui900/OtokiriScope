#include "PluginProcessor.h"
#include "PluginEditor.h"

OtokirisAudioProcessor::OtokirisAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& level : spectrum)
        level.store (0.0f);
}

float OtokirisAudioProcessor::getSpectrumLevel (int index) const noexcept
{
    return index >= 0 && index < (int) spectrum.size() ? spectrum[(size_t) index].load() : 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout OtokirisAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "outputVolume", "Output Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "positionScale", "Position Scale", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "colourScale", "Colour Scale", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "positionCentre", "Position Centre", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "positionWidth", "Position Width", juce::NormalisableRange<float> (0.05f, 1.0f), 0.55f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "frequencyCentre", "Frequency Centre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "frequencyWidth", "Frequency Width", juce::NormalisableRange<float> (0.02f, 1.0f), 0.67f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "depthCentre", "Depth Centre", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "depthWidth", "Depth Width", juce::NormalisableRange<float> (0.02f, 1.0f), 0.47f));
    parameters.push_back (std::make_unique<juce::AudioParameterBool>(
        "positionLock", "Position Lock", false));
    parameters.push_back (std::make_unique<juce::AudioParameterBool>(
        "frequencyLock", "Frequency Lock", false));
    parameters.push_back (std::make_unique<juce::AudioParameterBool>(
        "invertArea", "Invert Area", false));
    parameters.push_back (std::make_unique<juce::AudioParameterBool>(
        "noVisual", "No Visual", false));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "extractionAmount", "Extraction Amount", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "maskSoftness", "Mask Softness", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f));
    parameters.push_back (std::make_unique<juce::AudioParameterFloat>(
        "reverbReduction", "Reverb Reduction", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    return { parameters.begin(), parameters.end() };
}

void OtokirisAudioProcessor::updateFilterCoefficients (float sampleRate, float lowFreq, float highFreq)
{
    constexpr float threshold = 0.5f;
    if (std::abs (sampleRate - lastFilterSampleRate) < 0.1f
        && std::abs (lowFreq - lastFilterLow) < threshold
        && std::abs (highFreq - lastFilterHigh) < threshold)
    {
        return;
    }

    lastFilterSampleRate = sampleRate;
    lastFilterLow = lowFreq;
    lastFilterHigh = highFreq;

    auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, lowFreq, 0.7071f);
    auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, highFreq, 0.7071f);

    for (size_t ch = 0; ch < 2; ++ch)
    {
        highPassFilters[ch].coefficients = hpCoeffs;
        lowPassFilters[ch].coefficients = lpCoeffs;
    }
}

void OtokirisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    const auto sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysisSampleRate.store (sr);
    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);
    analysisFifoIndex = 0;
    analysisSampleCounter = 0;
    haveHostPosition = false;
    hostPlaying.store (false);
    for (auto& level : spectrum)
        level.store (0.0f);

    lastFilterSampleRate = -1.0f;
    lastFilterLow = -1.0f;
    lastFilterHigh = -1.0f;
    updateFilterCoefficients ((float) sr, 20.0f, (float) (sr * 0.49));

    for (auto& filter : highPassFilters)
        filter.reset();
    for (auto& filter : lowPassFilters)
        filter.reset();
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

void OtokirisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    if (buffer.getNumSamples() <= 0)
        return;

    bool shouldAnalyse = true;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            const auto playing = position->getIsPlaying();
            const auto wasPlaying = hostPlaying.load();
            hostPlaying.store (playing);

            const auto samplePosition = position->getTimeInSamples();
            const auto currentSample = samplePosition.hasValue() ? *samplePosition : lastHostSample;
            const auto discontinuity = samplePosition.hasValue() && haveHostPosition
                && (currentSample < lastHostSample
                    || currentSample - lastHostSample > (juce::int64) buffer.getNumSamples() * 4);
            if (! haveHostPosition || discontinuity || (playing && ! wasPlaying))
            {
                analysisFifo.fill (0.0f);
                fftData.fill (0.0f);
                analysisFifoIndex = 0;
            }
            lastHostSample = currentSample + buffer.getNumSamples();
            haveHostPosition = true;
            shouldAnalyse = playing;
        }
    }

    if (shouldAnalyse && buffer.getNumChannels() > 0)
    {
        const auto* leftInput = buffer.getReadPointer (0);
        const auto* rightInput = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : leftInput;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto mono = 0.5f * (leftInput[sample] + rightInput[sample]);
            analysisFifo[(size_t) analysisFifoIndex++] = mono;
            if (analysisFifoIndex < fftSize)
                continue;

            std::copy (analysisFifo.begin(), analysisFifo.end(), fftData.begin());
            window.multiplyWithWindowingTable (fftData.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            const auto nyquist = analysisSampleRate.load() * 0.5;
            for (size_t band = 0; band < spectrum.size(); ++band)
            {
                const auto normalisedBand = (float) band / (float) (spectrum.size() - 1);
                const auto frequency = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0,
                                                        (double) normalisedBand);
                const auto bin = juce::jlimit (1, fftSize / 2 - 1,
                                               (int) std::round (frequency / analysisSampleRate.load()
                                                                 * fftSize));
                const auto magnitude = juce::jlimit (0.0f, 1.0f,
                                                     fftData[(size_t) bin] / 48.0f);
                spectrum[band].store (magnitude);
            }
            analysisFifoIndex = fftSize / 2;
            std::move (analysisFifo.begin() + fftSize / 2, analysisFifo.end(),
                       analysisFifo.begin());
        }
    }

    const auto* gainParameter = parameters.getRawParameterValue ("outputVolume");
    const auto outputGain = gainParameter != nullptr ? gainParameter->load() : 1.0f;
    const auto channelsToProcess = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

    const auto sampleRate = (float) analysisSampleRate.load();
    const auto* freqCentreParam = parameters.getRawParameterValue ("frequencyCentre");
    const auto* freqWidthParam = parameters.getRawParameterValue ("frequencyWidth");
    const auto freqCentre = freqCentreParam != nullptr ? freqCentreParam->load() : 0.5f;
    const auto freqWidth = freqWidthParam != nullptr ? freqWidthParam->load() : 1.0f;
    const auto lowNormalised = juce::jlimit (0.0f, 1.0f, freqCentre - freqWidth * 0.5f);
    const auto highNormalised = juce::jlimit (0.0f, 1.0f, freqCentre + freqWidth * 0.5f);
    const auto nyquist = sampleRate * 0.5;
    const auto lowFrequency = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) lowNormalised);
    const auto highFrequency = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) highNormalised);
    const auto filterLow = juce::jlimit (10.0, nyquist * 0.95, lowFrequency);
    const auto filterHigh = juce::jlimit (filterLow + 20.0, nyquist * 0.98, highFrequency);

    const auto* extractionAmountParam = parameters.getRawParameterValue ("extractionAmount");
    const auto* maskSoftnessParam = parameters.getRawParameterValue ("maskSoftness");
    const auto* reverbReductionParam = parameters.getRawParameterValue ("reverbReduction");
    const auto extractionAmount = extractionAmountParam != nullptr ? extractionAmountParam->load() : 1.0f;
    const auto maskSoftness = maskSoftnessParam != nullptr ? maskSoftnessParam->load() : 0.15f;
    const auto reverbReduction = reverbReductionParam != nullptr ? reverbReductionParam->load() : 0.0f;
    const auto bandGain = juce::jlimit (0.0f, 1.0f,
                                        extractionAmount * (1.0f - reverbReduction * 0.35f));

    updateFilterCoefficients (sampleRate, (float) filterLow, (float) filterHigh);

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            samples[sample] = highPassFilters[(size_t) channel].processSample (samples[sample]);
            samples[sample] = lowPassFilters[(size_t) channel].processSample (samples[sample]);
            samples[sample] *= bandGain;
        }
    }

    for (int channel = 0; channel < channelsToProcess; ++channel)
        buffer.applyGain (channel, 0, buffer.getNumSamples(), outputGain);

    if (channelsToProcess >= 2)
    {
        const auto* panCentreParam = parameters.getRawParameterValue ("positionCentre");
        const auto* panWidthParam = parameters.getRawParameterValue ("positionWidth");
        const auto* invertParam = parameters.getRawParameterValue ("invertArea");
        const auto* depthCentreParam = parameters.getRawParameterValue ("depthCentre");
        const auto* depthWidthParam = parameters.getRawParameterValue ("depthWidth");

        const auto panCentre = panCentreParam != nullptr ? panCentreParam->load() : 0.0f;
        const auto panWidth = panWidthParam != nullptr ? panWidthParam->load() : 1.0f;
        const auto invert = invertParam != nullptr && invertParam->load() > 0.5f;
        const auto depthCentre = depthCentreParam != nullptr ? depthCentreParam->load() : 0.5f;
        const auto depthWidth = depthWidthParam != nullptr ? depthWidthParam->load() : 1.0f;

        const auto softness = juce::jmax (0.001f, maskSoftness);
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto l = left[sample];
            const auto r = right[sample];
            const auto leftLevel = std::abs (l);
            const auto rightLevel = std::abs (r);
            const auto total = leftLevel + rightLevel + 1.0e-6f;
            const auto pan = (rightLevel - leftLevel) / total;

            // Pan mask
            const auto panDist = std::abs (pan - panCentre);
            const auto panMask = juce::jlimit (0.0f, 1.0f, (panWidth * 0.5f - panDist) / softness + 0.5f);

            // Depth mask: Diffuseness / Front-Back based on mid/side ratio
            const auto midLevel = std::abs (l + r) * 0.5f;
            const auto sideLevel = std::abs (l - r) * 0.5f;
            const auto depthVal = sideLevel / (midLevel + sideLevel + 1.0e-6f); // 0 (dry/front) to 1 (diffuse/back)
            const auto depthDist = std::abs (depthVal - depthCentre);
            const auto depthMask = juce::jlimit (0.0f, 1.0f, (depthWidth * 0.5f - depthDist) / softness + 0.5f);

            auto combinedMask = panMask * depthMask;
            if (invert)
                combinedMask = 1.0f - combinedMask;

            const auto finalGain = (1.0f - extractionAmount) + extractionAmount * combinedMask;
            left[sample] *= finalGain;
            right[sample] *= finalGain;
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
