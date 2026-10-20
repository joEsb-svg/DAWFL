#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PerformerState.h"

BassForgeAudioProcessor::BassForgeAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", bf::createParameterLayout())
{
    // Tutta l'allocazione avviene qui / in prepareToPlay, MAI in processBlock.
    paramCache.bind(apvts);
    wavetables.build();
    sampleBank.buildBuiltIns();
    directBus.setSize(1, 4096, false, true, false);                 // ridimensionato in prepareToPlay

    synth.clearVoices();
    for (int i = 0; i < bf::kNumVoices; ++i)
    {
        // seed diverso per voce => rumori decorrelati tra le voci
        auto* v = new bf::BassVoice(paramCache, wavetables, sampleBank, 0x9E3779B1u * (std::uint32_t) (i + 1));
        v->setDirectBus(&directBus);
        v->setPerformerBank(&performers);
        v->setTempoSource(&hostBpm);
        v->setSharedMidi(&sharedMidi);
        v->setModMonitor(&modMonitor);
        synth.addVoice(v);                                          // il Synthesiser ne prende l'ownership
    }

    synth.clearSounds();
    synth.addSound(new bf::SynthSound());
    synth.setNoteStealingEnabled(true);
}

void BassForgeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    // Capienza del bus diretto: alloca qui (mai nel thread audio). Se l'host superasse samplesPerBlock,
    // le voci ripiegano automaticamente sul routing post-filtri.
    directBus.setSize(1, juce::jmax(samplesPerBlock, 4096), false, true, false);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<bf::BassVoice*>(synth.getVoice(i)))
            v->prepare(sampleRate, samplesPerBlock);

    masterFx.prepare(sampleRate);
    directDelay.prepare(bf::MasterFXChain::kLatencySamples);
    directDelay.setDelay(bf::MasterFXChain::kLatencySamples);
    directDelay.reset();
    limiter.prepare(sampleRate);
    setLatencySamples(kTotalLatency);                            // FIR di oversampling (36) + lookahead del limiter (32), costante
    lastMasterGain = juce::Decibels::decibelsToGain(bf::load(paramCache.masterGain), -60.0f);
}

void BassForgeAudioProcessor::reset()
{
    synth.allNotesOff (0, false);
    masterFx.reset();
    directDelay.reset();
    limiter.reset();
}

bool BassForgeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BassForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();                                   // sintetizzatore: nessun input

    // Bus diretto azzerato prima del rendering (le voci ci sommano il Sub in Direct-Out)
    const int directSamples = juce::jmin(numSamples, directBus.getNumSamples());
    directBus.clear(0, 0, directSamples);

    // Tempo host (per LFO/Performer in sync) e mod wheel globale: letti dalle voci con atomic relaxed
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store((float) *bpm, std::memory_order_relaxed);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController() && m.getControllerNumber() == 1)
            sharedMidi.modWheel.store((float) m.getControllerValue() * (1.0f / 127.0f), std::memory_order_relaxed);
    }

    // MIDI -> voci (note on/off, pitch bend, aftertouch, CC) e rendering sample-accurate
    synth.renderNextBlock(buffer, midi, 0, numSamples);

    // FX master (il Sub in Direct-Out ne e' escluso)
    paramCache.fillFxParams(fxParams, (double) hostBpm.load(std::memory_order_relaxed));
    masterFx.setParams(fxParams);
    if (buffer.getNumChannels() >= 2)
        masterFx.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);

    // Sub Direct-Out: ritardato della stessa latenza della catena FX (resta in fase con il resto del suono)
    if (numSamples <= directBus.getNumSamples())
    {
        float* d = directBus.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i) d[i] = directDelay.process(d[i]);
    }

    // Sub Direct-Out: mono, centrato, sommato su L e R dopo gli FX e prima del master gain
    if (numSamples <= directBus.getNumSamples())
        for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
            buffer.addFrom(ch, 0, directBus, 0, 0, numSamples);

    // GUI: anelli di modulazione a zero se nessuna voce suona; oscilloscopio (mix mono post-FX, prima del master gain)
    {
        bool anyActive = false;
        for (int i = 0; i < synth.getNumVoices() && ! anyActive; ++i) anyActive = synth.getVoice(i)->isVoiceActive();
        if (! anyActive) modMonitor.clear();

        const int n = juce::jmin(numSamples, (int) scopeTmp.size());
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : l;
        for (int i = 0; i < n; ++i) scopeTmp[(size_t) i] = 0.5f * (l[i] + r[i]);
        scope.push(scopeTmp.data(), n);
    }

    // Master gain con rampa lineare (evita zipper noise)
    const float target = juce::Decibels::decibelsToGain(bf::load(paramCache.masterGain), -60.0f);
    buffer.applyGainRamp(0, numSamples, lastMasterGain, target);
    lastMasterGain = target;

    // Limiter finale: dopo il master gain e dopo il Sub Direct-Out => nessun picco oltre -0.3 dBFS in uscita.
    // Resta sempre in linea (anche spento) per non cambiare la latenza dichiarata all'host.
    if (buffer.getNumChannels() >= 2)
        limiter.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples, kLimiterCeiling,
                        bf::load(paramCache.masterLimiter) > 0.5f);
}

juce::AudioProcessorEditor* BassForgeAudioProcessor::createEditor()
{
    return new BassForgeAudioProcessorEditor(*this);
}

void BassForgeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild(state.getChildWithName("PERFORMERS"), nullptr);
    state.appendChild(bf::performersToTree(performers), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BassForgeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto state = juce::ValueTree::fromXml(*xml);
            bf::performersFromTree(performers, state.getChildWithName("PERFORMERS"));
            apvts.replaceState(state);
        }
}

// Entry point del plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassForgeAudioProcessor();
}
