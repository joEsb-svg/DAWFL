#include "BassVoice.h"

namespace bf
{
namespace
{
    inline float semisToRatio(float semis) noexcept { return std::exp2(semis * (1.0f / 12.0f)); }
    inline float wrap01(float x) noexcept           { return x - std::floor(x); }
}

BassVoice::BassVoice(const ParamCache& params, const WavetableBank& b, const SampleBank& sb, std::uint32_t seed)
    : pc(params), bank(b), samples(sb), noiseSeed(seed)
{
}

SubOscillator::Params BassVoice::makeSubParams(float levelMod) const noexcept
{
    SubOscillator::Params p;
    p.wave       = (SubWave) juce::jlimit(0, 3, (int) load(pc.sub.wave));
    p.octave     = (int) load(pc.sub.oct) - 3;                              // scelte "-3,-2,-1,0"
    p.level      = juce::jlimit(0.f, 1.f, load(pc.sub.level) + levelMod);
    p.startPhase = load(pc.sub.phase) * (1.0f / 360.0f);
    p.phaseMode  = load(pc.sub.reset) > 0.5f ? SubPhaseMode::Reset : SubPhaseMode::FreeRun;
    return p;
}

NoiseGenerator::Params BassVoice::makeNoiseParams(float levelMod) const noexcept
{
    NoiseGenerator::Params p;
    p.noiseOn    = load(pc.noise.on) > 0.5f;
    p.noiseType  = load(pc.noise.type) > 0.5f ? NoiseType::Pink : NoiseType::White;
    p.noiseLevel = juce::jlimit(0.f, 1.f, load(pc.noise.level) + levelMod);
    p.noiseWidth = load(pc.noise.width);

    p.sampleOn         = load(pc.sample.on) > 0.5f;
    p.sampleSlot       = (int) load(pc.sample.slot);
    p.sampleLevel      = load(pc.sample.level);
    p.samplePitchSemi  = load(pc.sample.pitch);
    p.samplePitchTrack = load(pc.sample.pitchTrack);
    p.loop             = load(pc.sample.loop) > 0.5f;
    p.loopStart        = load(pc.sample.loopStart);
    p.loopEnd          = load(pc.sample.loopEnd);
    p.decayMs          = load(pc.sample.decay);

    p.filterOn   = load(pc.noiseFilter.on) > 0.5f;
    p.filterType = (ToneFilterType) juce::jlimit(0, 2, (int) load(pc.noiseFilter.type));
    p.cutoffHz   = load(pc.noiseFilter.cutoff);
    p.resonance  = load(pc.noiseFilter.res);
    p.keytrack   = load(pc.noiseFilter.track);
    return p;
}

void BassVoice::prepare(double sampleRate, int maxBlockSize)
{
    sr = sampleRate;

    vm.prepare(sampleRate, noiseSeed ^ 0xA5A5A5A5u);
    vm.setPerformers(performers);
    filterEngine.prepare(sampleRate, kControlBlock);
    sub.prepare(sampleRate);
    for (int i = 0; i < kNumOsc; ++i)
        wtOsc[(size_t) i].prepare(sampleRate, &bank, noiseSeed * 2654435761u + (std::uint32_t) i * 40503u + 1u);
    noiseGen.prepare(sampleRate, maxBlockSize, &samples, noiseSeed);
}

void BassVoice::configureModulation() noexcept
{
    for (int i = 0; i < kNumEnv; ++i)
    {
        const auto& e = pc.env[(size_t) i];
        ModEnvelope::Params p;
        p.attack = load(e.a);  p.hold = load(e.h);  p.decay = load(e.d);  p.sustain = load(e.s);  p.release = load(e.r);
        p.attackCurve = load(e.ac);  p.decayCurve = load(e.dc);  p.releaseCurve = load(e.rc);
        vm.envelope(i).setParams(p);
    }
    for (int i = 0; i < kNumLfo; ++i)
    {
        const auto& l = pc.lfo[(size_t) i];
        ModLfo::Params p;
        p.shape       = (LfoShape) juce::jlimit(0, (int) LfoShape::Count - 1, (int) load(l.shape));
        p.rateHz      = load(l.rate);
        p.sync        = load(l.sync) > 0.5f;
        p.division    = (int) load(l.div);
        p.phaseOffset = load(l.phase);
        p.fadeInMs    = load(l.fade);
        p.retrigger   = load(l.retrig) > 0.5f;
        p.playMode        = (PlayMode) juce::jlimit(0, (int) PlayMode::Count - 1, (int) load(l.pmode));
        p.perfEndHold     = load(l.phold) > 0.5f;
        p.perfRatePerStep = load(l.pstep) > 0.5f;
        vm.lfo(i).setParams(p);
    }
    ModulationMatrix::Table table;
    pc.fillModTable(table);
    vm.matrix().setTable(table);
    vm.setTempo(tempo != nullptr ? (double) tempo->load(std::memory_order_relaxed) : 120.0);
}

void BassVoice::startNote(int midiNote, float vel, juce::SynthesiserSound*, int pitchWheel)
{
    currentNote = midiNote;
    velocity    = vel;

    // Fase reset per attacco consistente (punch del basso). Per un feel "analogico" free-run, randomizzare.
    for (auto& o : wtOsc) o.retrigger();

    // Modulazione: mod wheel globale; pressione/slide sono per-nota => ripartono da 0
    configureModulation();
    vm.midi().setPressure(0.f);
    vm.midi().setSlide(0.f);
    vm.midi().setModWheel(shared != nullptr ? shared->modWheel.load(std::memory_order_relaxed) : 0.f);
    vm.noteOn(midiNote, vel, pitchWheel);

    const float bendSemis = vm.midi().bend() * load(pc.pitchBendRange);
    prevBendSemis = bendSemis;

    filterEngine.reset();

    // Sub: incremento di riferimento = fondamentale della nota (pitch bend incluso). Con Phase Reset attivo
    // anche qui il sub riparte da startPhase => agganciato in fase (esatto sub-armonico) agli oscillatori.
    sub.setParams(makeSubParams(0.f));
    sub.setReferenceIncrement((double) juce::MidiMessage::getMidiNoteInHertz(midiNote)
                              * (double) semisToRatio(bendSemis) / sr);
    sub.noteOn();

    // Noise/Sample: il campione viene "catturato" qui (slot scelto al note-on).
    noiseGen.setParams(makeNoiseParams(0.f));
    noiseGen.noteOn(midiNote, vel);
}

void BassVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        vm.noteOff();
        noiseGen.noteOff();                        // il campione esce dal loop e finisce di suonare
    }
    else
    {
        vm.hardReset();
        clearCurrentNote();
        currentNote = -1;
    }
}

void BassVoice::pitchWheelMoved(int newValue)                { vm.midi().setPitchBend14(newValue); }
void BassVoice::controllerMoved(int cc, int value)           { vm.midi().setController(cc, value); }
void BassVoice::aftertouchChanged(int v)                     { vm.midi().setPressure((float) v * (1.0f / 127.0f)); }
void BassVoice::channelPressureChanged(int v)                { vm.midi().setPressure((float) v * (1.0f / 127.0f)); }

void BassVoice::renderNextBlock(juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (currentNote < 0)
        return;

    // Parametri di envelope/LFO/matrice: una volta per blocco host (atomics -> nessun lock)
    configureModulation();

    float* l = out.getWritePointer(0, startSample);
    float* r = out.getNumChannels() > 1 ? out.getWritePointer(1, startSample) : nullptr;

    // Bus diretto: valido solo se esiste ed e' abbastanza lungo per questo intervallo (altrimenti fallback).
    float* d = (directBus != nullptr && directBus->getNumChannels() >= 1
                && directBus->getNumSamples() >= startSample + numSamples)
                   ? directBus->getWritePointer(0, startSample) : nullptr;

    int done = 0;
    while (done < numSamples)
    {
        const int len = juce::jmin(kControlBlock, numSamples - done);
        renderControlBlock(l + done, r != nullptr ? r + done : nullptr, d != nullptr ? d + done : nullptr, len);
        done += len;

        if (! vm.ampActive())
        {
            clearCurrentNote();
            currentNote = -1;
            break;
        }
    }
}

void BassVoice::renderControlBlock(float* outL, float* outR, float* direct, int len) noexcept
{
    const float fs = (float) sr;

    // ---------------- 1) Modulazione: avanza di `len` campioni e valuta la matrice --------------------
    for (int i = 0; i < kNumMacro; ++i)
        vm.setMacro(i, load(pc.macro[(size_t) i]));
    vm.advance(len);
    const ModulationMatrix& mx = vm.matrix();
    if (monitor != nullptr)
        for (int d = DstPitch; d < DstCount; ++d) monitor->set(d, mx.value(d));
    const float invLen = 1.0f / (float) len;

    // ---------------- 2) Parametri per-blocco (con rampe start -> end) ----------------------------------
    const float baseHz    = (float) juce::MidiMessage::getMidiNoteInHertz(currentNote);
    const float bendSemis = vm.midi().bend() * load(pc.pitchBendRange);
    const auto  pitchR    = mx.ramp(DstPitch);
    const float pitchStart = prevBendSemis + pitchR.start * modscale::kPitchSemis;
    const float pitchEnd   = bendSemis     + pitchR.end   * modscale::kPitchSemis;
    prevBendSemis = bendSemis;

    struct OscState { bool on; float gl, dgl, gr, dgr; };
    std::array<OscState, kNumOsc> osc;
    for (int i = 0; i < kNumOsc; ++i)
    {
        const auto& p = pc.osc[(size_t) i];
        const float base = load(p.semi) + load(p.fine) * 0.01f;
        const auto  pr   = mx.ramp(DstOsc1Pitch + i);
        const float inc0 = baseHz * semisToRatio(base + pitchStart + pr.start * modscale::kOscPitchSemis) / fs;
        const float inc1 = baseHz * semisToRatio(base + pitchEnd   + pr.end   * modscale::kOscPitchSemis) / fs;
        const auto  wt   = mx.rampLinear(DstOsc1Wt + i,    load(p.wtPos), 1.f, 0.f, 1.f);
        const auto  lv   = mx.rampLinear(DstOsc1Level + i, load(p.level), 1.f, 0.f, 1.f);
        const float ang  = (load(p.pan) + 1.f) * juce::MathConstants<float>::pi * 0.25f;
        const float cg   = std::cos(ang) * 1.41421356f, sg = std::sin(ang) * 1.41421356f;
        osc[(size_t) i] = { load(p.on) > 0.5f, cg * lv.start, cg * (lv.end - lv.start) * invLen,
                            sg * lv.start, sg * (lv.end - lv.start) * invLen };

        // Oscillatore wavetable a parametri costanti nel sub-frame (16 campioni): pitch = media della rampa
        // (la fase e' continua, quindi nessun click), posizione WT quantizzata a 1/1024 per non rifare il morph inutilmente.
        if (osc[(size_t) i].on)
        {
            WavetableOscillator::Params wp;
            wp.wtPosition   = std::round(0.5f * (wt.start + wt.end) * 1024.f) * (1.f / 1024.f);
            wp.warpMode     = (WarpMode) juce::jlimit(0, 3, (int) load(p.warp));
            wp.warpAmount   = load(p.wamt);
            wp.warpAux      = load(p.waux);
            wp.unisonVoices = (int) (load(p.uni) + 0.5f);
            wp.detuneCents  = load(p.det);
            wp.stereoSpread = load(p.spread);
            wp.blend        = load(p.blend);
            auto& w = wtOsc[(size_t) i];
            w.setParams(wp);
            w.setFrequency(0.5f * (inc0 + inc1) * fs);
            std::fill_n(oscL[(size_t) i].begin(), len, 0.f);
            std::fill_n(oscR[(size_t) i].begin(), len, 0.f);
            w.renderAdd(oscL[(size_t) i].data(), oscR[(size_t) i].data(), len, 1.0f);
        }
    }

    // Sub: frequenza = fondamentale della nota con lo STESSO pitch (bend + mod) degli oscillatori => phase tracking.
    const bool subOn     = load(pc.sub.on) > 0.5f;
    const bool subDirect = load(pc.sub.direct) > 0.5f;
    sub.setParams(makeSubParams(mx.value(DstSubLevel)));
    // Follow Pitch Mod: se disattivato il sub segue solo il pitch bend (resta "ancorato" alla nota).
    const float subSemi = load(pc.sub.followMod) > 0.5f ? pitchEnd : bendSemis;
    sub.setReferenceIncrement((double) baseHz * (double) semisToRatio(subSemi) / sr);

    noiseGen.setParams(makeNoiseParams(mx.value(DstNoiseLevel)));

    // Dual Filter Engine: parametri del blocco (il motore interpola cutoff/morph lungo il blocco)
    DualFilterEngine::Params fp;
    for (int i = 0; i < kNumFilter; ++i)
    {
        const auto& p = pc.filter[(size_t) i];
        auto& f = fp.f[(size_t) i];
        f.type = (FilterType) juce::jlimit(0, (int) FilterType::Vowel, (int) load(p.type));
        const bool comb = f.type == FilterType::Comb;
        const float key = comb ? 0.f : ((float) (currentNote - 60) / 12.f) * load(p.keytrack);   // il comb usa pitchTrack
        f.on         = load(p.on) > 0.5f;
        f.cutoffHz   = juce::jlimit(20.f, 0.45f * fs,
                                    load(p.cutoff) * std::exp2(key + mx.value(DstF1Cutoff + i) * modscale::kCutoffOct));
        f.resonance  = mx.applyLinear(DstF1Res + i, load(p.res), 1.f, 0.f, 1.f);
        f.drive      = mx.applyLinear(DstF1Drive + i, load(p.drive), 1.f, 0.f, 1.f);
        f.feedback   = mx.applyLinear(DstF1Fb + i, load(p.fb), 1.f, -1.f, 1.f);
        f.pitchTrack = load(p.keytrack);
        f.tuneSemis  = load(p.tune);
        f.vowel      = mx.applyLinear(DstF1Vowel + i, load(p.vowel), 1.f, 0.f, 1.f);
        f.formantShift = load(p.fshift);
    }
    fp.routing = (FilterRouting) juce::jlimit(0, 2, (int) load(pc.filterRouting));
    fp.morph   = mx.applyLinear(DstFilterMorph, load(pc.filterMorph), 1.f, 0.f, 1.f);
    fp.noteHz  = baseHz * semisToRatio(pitchEnd);
    filterEngine.setParams(fp);

    const float velGain = 0.25f + 0.75f * velocity;
    const auto  ampR    = mx.ramp(DstAmp);                          // modulazione d'ampiezza (rampa per-campione)
    const float* ampEnv = vm.envBuffer(0);                           // Env1 per-campione

    // ---------------- 4a) Sorgenti -> buffer di blocco ------------------------------
    for (int n = 0; n < len; ++n)
    {
        float sl = 0.f, sr_ = 0.f;

        for (int i = 0; i < kNumOsc; ++i)
        {
            auto& o = osc[(size_t) i];
            o.gl += o.dgl;  o.gr += o.dgr;                  // rampe sample-accurate dei livelli
            if (o.on)
            {
                sl  += oscL[(size_t) i][(size_t) n] * o.gl;
                sr_ += oscR[(size_t) i][(size_t) n] * o.gr;
            }
        }

        // Noise + Sample (stereo)
        {
            float nl, nr;
            noiseGen.tick(nl, nr);
            sl += nl; sr_ += nr;
        }

        // Sub: Direct-Out lo tiene fuori dalla catena filtri
        const float subS = subOn ? sub.tick() : 0.f;
        subBuf[(size_t) n] = subS;
        if (! subDirect) { sl += subS; sr_ += subS; }

        bufL[(size_t) n] = sl;
        bufR[(size_t) n] = sr_;
    }

    // ---------------- 4b) Dual Filter Engine (SIMD, in-place) -----------------------
    filterEngine.process(bufL.data(), bufR.data(), len);

    // ---------------- 4c) Envelope + uscita ------------------------------------------
    for (int n = 0; n < len; ++n)
    {
        const float yl = bufL[(size_t) n], yr = bufR[(size_t) n], subS = subBuf[(size_t) n];

        // --- Ampiezza: Env1 per-campione (AHDSR) x velocity x modulazione DstAmp ------
        const float g = ampEnv[n] * velGain * std::max(0.f, 1.f + ampR.at(n, len));
        outL[n] += yl * g;
        if (outR != nullptr) outR[n] += yr * g;

        if (subDirect)
        {
            const float ds = subS * g;                      // envelope + velocity, ma niente filtri/FX
            if (direct != nullptr)  direct[n] += ds;        // sommato dal processor dopo gli FX master
            else                    { outL[n] += ds; if (outR != nullptr) outR[n] += ds; }   // fallback senza bus
        }
    }
}
}
