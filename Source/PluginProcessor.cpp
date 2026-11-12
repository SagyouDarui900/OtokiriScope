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

void OtokirisAudioProcessor::prepareToPlay (double sampleRate, int)
{
    analysisSampleRate.store (sampleRate > 0.0 ? sampleRate : 44100.0);
    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);
    analysisFifoIndex = 0;
    analysisSampleCounter = 0;
    haveHostPosition = false;
    hostPlaying.store (false);
    for (auto& level : spectrum)
        level.store (0.0f);
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
    const auto frequencyCentre = parameters.getRawParameterValue ("frequencyCentre");
    const auto frequencyWidth = parameters.getRawParameterValue ("frequencyWidth");
    const auto centre = frequencyCentre != nullptr ? frequencyCentre->load() : 0.5f;
    const auto width = frequencyWidth != nullptr ? frequencyWidth->load() : 1.0f;
    const auto lowNormalised = juce::jlimit (0.0f, 1.0f, centre - width * 0.5f);
    const auto highNormalised = juce::jlimit (0.0f, 1.0f, centre + width * 0.5f);
    const auto nyquist = sampleRate * 0.5;
    const auto lowFrequency = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) lowNormalised);
    const auto highFrequency = 20.0 * std::pow (juce::jmax (20.0, nyquist) / 20.0, (double) highNormalised);
    const auto filterLow = juce::jlimit (10.0, nyquist * 0.95, lowFrequency);
    const auto filterHigh = juce::jlimit (filterLow + 20.0, nyquist * 0.98, highFrequency);
    const auto extractionAmountParameter = parameters.getRawParameterValue ("extractionAmount");
    const auto maskSoftnessParameter = parameters.getRawParameterValue ("maskSoftness");
    const auto reverbReductionParameter = parameters.getRawParameterValue ("reverbReduction");
    const auto extractionAmount = extractionAmountParameter != nullptr ? extractionAmountParameter->load() : 1.0f;
    const auto maskSoftness = maskSoftnessParameter != nullptr ? maskSoftnessParameter->load() : 0.15f;
    const auto reverbReduction = reverbReductionParameter != nullptr ? reverbReductionParameter->load() : 0.0f;
    const auto bandGain = juce::jlimit (0.0f, 1.0f,
                                        extractionAmount * (1.0f - reverbReduction * 0.35f));
    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        highPassFilters[(size_t) channel].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
            sampleRate, (float) filterLow, 0.7071f);
        lowPassFilters[(size_t) channel].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
            sampleRate, (float) filterHigh, 0.7071f);
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
        const auto* centreParameter = parameters.getRawParameterValue ("positionCentre");
        const auto* widthParameter = parameters.getRawParameterValue ("positionWidth");
        const auto* invertParameter = parameters.getRawParameterValue ("invertArea");
        const auto* depthCentreParameter = parameters.getRawParameterValue ("depthCentre");
        const auto* depthWidthParameter = parameters.getRawParameterValue ("depthWidth");
        const auto centre = centreParameter != nullptr ? centreParameter->load() : 0.0f;
        const auto width = widthParameter != nullptr ? widthParameter->load() : 1.0f;
        const auto invert = invertParameter != nullptr && invertParameter->load() > 0.5f;
        juce::ignoreUnused (depthCentreParameter, depthWidthParameter);
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto leftLevel = std::abs (left[sample]);
            const auto rightLevel = std::abs (right[sample]);
            const auto total = leftLevel + rightLevel + 1.0e-6f;
            const auto pan = (rightLevel - leftLevel) / total;
            const auto distance = std::abs (pan - centre);
            const auto selected = distance <= width * 0.5f;
            const auto edgeDistance = std::abs (distance - width * 0.5f);
            const auto softness = juce::jmax (0.001f, maskSoftness);
            auto mask = juce::jlimit (0.0f, 1.0f, (width * 0.5f - distance) / softness);
            if (invert)
                mask = 1.0f - mask;
            mask = 1.0f - extractionAmount + extractionAmount * mask;
            juce::ignoreUnused (edgeDistance);
            left[sample] *= mask;
            right[sample] *= mask;
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
