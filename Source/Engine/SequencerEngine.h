#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include "DrumVoice.h"

//==============================================================================
/**
    Il motore audio di DAWFL.

    Gira sul thread audio, che e' "real-time": nel callback niente allocazioni,
    niente lock, niente file. La UI comunica con il motore solo tramite
    variabili atomiche, che si possono leggere e scrivere da thread diversi.

    Struttura (come in FL Studio):
    - i CANALI (strumento, suono, volume, pan, effetti...) sono del progetto
    - i PATTERN (fino a 8) contengono le note: per ogni canale e step
      velocity (0 = spento), probabilita' e ratchet (colpi ripetuti nello step)
    - clock preciso al campione con swing; il cambio pattern durante la
      riproduzione avviene alla fine del giro, per restare a tempo

    Catena audio:  voce -> filtro del canale -> volume/pan -> mix stereo
                                             \-> mandate a riverbero e delay
                   mix + riverbero + delay -> limiter del master -> uscita
*/
class SequencerEngine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int maxSteps         = 32;
    static constexpr int numChannels      = 6;
    static constexpr int numPatterns      = 8;
    static constexpr int closedHatChannel = 3;
    static constexpr int openHatChannel   = 4;
    static constexpr int defaultVelocity  = 100;
    static constexpr int maxRatchet       = 4;

    struct ChannelInfo
    {
        const char*  name;
        juce::uint32 colour;
    };

    static const ChannelInfo& getChannelInfo (int index)
    {
        static const ChannelInfo infos[numChannels] =
        {
            { "Kick",     0xffe8833a },
            { "Snare",    0xffd2555a },
            { "Clap",     0xffd8b14a },
            { "Hi-Hat",   0xff4fb3a9 },
            { "Open Hat", 0xff5b8fd6 },
            { "Tom",      0xff9a78d0 },
        };
        return infos[index];
    }

    //==============================================================================
    /** Impostazioni di un canale (valgono per tutti i pattern). */
    struct Channel
    {
        std::atomic<float> volume   { 0.8f };
        std::atomic<bool>  muted    { false };
        std::atomic<int>   kit      { 0 };       // suono scelto dalla libreria (vedi SoundLibrary)

        // Editor del canale
        std::atomic<float> pan      { 0.0f };    // -1 sinistra .. +1 destra
        std::atomic<float> pitch    { 0.0f };    // semitoni
        std::atomic<float> decay    { 1.0f };    // moltiplicatore
        std::atomic<float> tone     { 0.0f };    // -1 .. +1
        std::atomic<float> drive    { 0.0f };    // 0 .. 1
        std::atomic<float> filter   { 1.0f };    // 0 .. 1 (1 = filtro aperto)
        std::atomic<float> reverb   { 0.0f };    // mandata 0 .. 1
        std::atomic<float> delay    { 0.0f };    // mandata 0 .. 1
        std::atomic<float> humanize { 0.15f };   // 0 .. 1

        std::atomic<int>   soundVersion     { 0 };      // la UI lo incrementa quando cambia pitch/decay/tone/drive
        std::atomic<bool>  previewRequested { false };
        std::atomic<float> activity         { 0.0f };   // per far "lampeggiare" la UI quando suona
        std::atomic<int>   triggerCount     { 0 };      // colpi suonati in totale (diagnostica e test)

        void touchSound()  { soundVersion.fetch_add (1); }
    };

    /** Un pattern: per canale e step velocity, probabilita' e ratchet; e la sua lunghezza. */
    struct Pattern
    {
        using Row = std::array<std::atomic<uint8_t>, maxSteps>;
        std::array<Row, numChannels> velocity;      // 0 = spento, 1..127
        std::array<Row, numChannels> probability;   // 0..100 %
        std::array<Row, numChannels> ratchet;       // 1..4 colpi nello step
        std::atomic<int> length { 16 };
    };

    SequencerEngine()   { resetProject(); }

    //==============================================================================
    /** Progetto nuovo: tutto vuoto e valori di default. */
    void resetProject()
    {
        for (int p = 0; p < numPatterns; ++p)
            clearPattern (p);

        static const float defaultVolumes[numChannels] = { 0.8f, 0.8f, 0.7f, 0.6f, 0.55f, 0.75f };

        for (int c = 0; c < numChannels; ++c)
            resetChannel (c, defaultVolumes[c]);

        bpm.store (120.0);
        swing.store (0.0f);
        masterVolume.store (1.0f);
        currentPattern.store (0);
    }

    void resetChannel (int c, float vol = 0.8f)
    {
        auto& ch = channels[(size_t) c];
        ch.volume.store (vol);
        ch.muted.store (false);
        ch.kit.store (0);
        ch.pan.store (0.0f);
        ch.pitch.store (0.0f);
        ch.decay.store (1.0f);
        ch.tone.store (0.0f);
        ch.drive.store (0.0f);
        ch.filter.store (1.0f);
        ch.reverb.store (0.0f);
        ch.delay.store (0.0f);
        ch.humanize.store (0.15f);
        ch.touchSound();
    }

    //==============================================================================
    // Trasporto

    void setPlaying (bool shouldPlay)
    {
        if (shouldPlay && ! playing.load())
        {
            playingPattern.store (currentPattern.load());
            restartRequested.store (true);
        }

        playing.store (shouldPlay);
    }

    bool isPlaying() const                      { return playing.load(); }

    void setBpm (double newBpm)                 { bpm.store (juce::jlimit (40.0, 240.0, newBpm)); }
    double getBpm() const                       { return bpm.load(); }

    /** Swing 0..1 (0 = dritto, 1 = massimo). Ritarda gli step dispari. */
    void setSwing (float s)                     { swing.store (juce::jlimit (0.0f, 1.0f, s)); }
    float getSwing() const                      { return swing.load(); }

    void setMasterVolume (float v)              { masterVolume.store (juce::jlimit (0.0f, 1.5f, v)); }
    float getMasterVolume() const               { return masterVolume.load(); }

    void setMetronome (bool on)                 { metronome.store (on); }
    bool isMetronomeOn() const                  { return metronome.load(); }

    //==============================================================================
    // Pattern

    /** Sceglie il pattern da modificare. Se sta suonando, la riproduzione ci passa alla fine del giro. */
    void selectPattern (int index)
    {
        currentPattern.store (juce::jlimit (0, numPatterns - 1, index));
        if (! playing.load())
            playingPattern.store (currentPattern.load());
    }

    int getCurrentPattern() const               { return currentPattern.load(); }
    int getPlayingPattern() const               { return playingPattern.load(); }

    Pattern& getPattern (int index)             { return patterns[(size_t) index]; }
    Pattern& current()                          { return patterns[(size_t) currentPattern.load()]; }
    const Pattern& current() const              { return patterns[(size_t) currentPattern.load()]; }

    int getPatternLength() const                { return current().length.load(); }
    void setPatternLength (int len)             { current().length.store (len > 16 ? 32 : 16); }

    int getStepVelocity (int channel, int step) const      { return current().velocity[(size_t) channel][(size_t) step].load(); }
    void setStepVelocity (int channel, int step, int v)    { current().velocity[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (0, 127, v)); }
    bool isStepOn (int channel, int step) const            { return getStepVelocity (channel, step) > 0; }

    int getStepProbability (int channel, int step) const   { return current().probability[(size_t) channel][(size_t) step].load(); }
    void setStepProbability (int channel, int step, int p) { current().probability[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (0, 100, p)); }

    int getStepRatchet (int channel, int step) const       { return current().ratchet[(size_t) channel][(size_t) step].load(); }
    void setStepRatchet (int channel, int step, int r)     { current().ratchet[(size_t) channel][(size_t) step].store ((uint8_t) juce::jlimit (1, maxRatchet, r)); }

    void clearStep (Pattern& p, int c, int s)
    {
        p.velocity[(size_t) c][(size_t) s].store (0);
        p.probability[(size_t) c][(size_t) s].store (100);
        p.ratchet[(size_t) c][(size_t) s].store (1);
    }

    void clearPattern (int index)
    {
        auto& p = patterns[(size_t) index];
        for (int c = 0; c < numChannels; ++c)
            for (int s = 0; s < maxSteps; ++s)
                clearStep (p, c, s);
        p.length.store (16);
    }

    void copyStep (Pattern& src, int srcC, int srcS, Pattern& dst, int dstC, int dstS)
    {
        dst.velocity[(size_t) dstC][(size_t) dstS].store (src.velocity[(size_t) srcC][(size_t) srcS].load());
        dst.probability[(size_t) dstC][(size_t) dstS].store (src.probability[(size_t) srcC][(size_t) srcS].load());
        dst.ratchet[(size_t) dstC][(size_t) dstS].store (src.ratchet[(size_t) srcC][(size_t) srcS].load());
    }

    void copyPattern (int from, int to)
    {
        if (from == to)
            return;

        auto& src = patterns[(size_t) from];
        auto& dst = patterns[(size_t) to];

        for (int c = 0; c < numChannels; ++c)
            for (int s = 0; s < maxSteps; ++s)
                copyStep (src, c, s, dst, c, s);

        dst.length.store (src.length.load());
    }

    bool isPatternEmpty (int index) const
    {
        for (auto& row : patterns[(size_t) index].velocity)
            for (auto& v : row)
                if (v.load() > 0)
                    return false;
        return true;
    }

    //==============================================================================
    // Strumenti per una riga del pattern corrente

    /** Sposta tutti gli step di una riga (con rientro dall'altro lato). */
    void shiftRow (int channel, int amount)
    {
        auto& p = current();
        const int len = p.length.load();
        Pattern::Row v, pr, r;

        for (int s = 0; s < len; ++s)
        {
            const int from = ((s - amount) % len + len) % len;
            v[(size_t) s].store (p.velocity[(size_t) channel][(size_t) from].load());
            pr[(size_t) s].store (p.probability[(size_t) channel][(size_t) from].load());
            r[(size_t) s].store (p.ratchet[(size_t) channel][(size_t) from].load());
        }

        for (int s = 0; s < len; ++s)
        {
            p.velocity[(size_t) channel][(size_t) s].store (v[(size_t) s].load());
            p.probability[(size_t) channel][(size_t) s].store (pr[(size_t) s].load());
            p.ratchet[(size_t) channel][(size_t) s].store (r[(size_t) s].load());
        }
    }

    void reverseRow (int channel)
    {
        auto& p = current();
        const int len = p.length.load();

        for (int a = 0, b = len - 1; a < b; ++a, --b)
        {
            auto swap = [&] (Pattern::Row& row)
            {
                const auto t = row[(size_t) a].load();
                row[(size_t) a].store (row[(size_t) b].load());
                row[(size_t) b].store (t);
            };

            swap (p.velocity[(size_t) channel]);
            swap (p.probability[(size_t) channel]);
            swap (p.ratchet[(size_t) channel]);
        }
    }

    void clearRow (int channel)
    {
        for (int s = 0; s < maxSteps; ++s)
            clearStep (current(), channel, s);
    }

    /** Accende uno step ogni "every" (es. 4 = un colpo per quarto), partendo da "offset". */
    void fillRow (int channel, int every, int offset = 0)
    {
        auto& p = current();
        for (int s = 0; s < p.length.load(); ++s)
            if ((s - offset) % every == 0 && s >= offset)
                p.velocity[(size_t) channel][(size_t) s].store ((uint8_t) defaultVelocity);
    }

    void copyRow (int fromChannel, int toChannel)
    {
        for (int s = 0; s < maxSteps; ++s)
            copyStep (current(), fromChannel, s, current(), toChannel, s);
    }

    //==============================================================================
    // Canali

    Channel& getChannel (int index)             { return channels[(size_t) index]; }

    void previewChannel (int channel)           { channels[(size_t) channel].previewRequested.store (true); }

    void setChannelKit (int channel, int kit)   { channels[(size_t) channel].kit.store (juce::jlimit (0, SoundLibrary::numKits - 1, kit)); }
    int getChannelKit (int channel) const       { return channels[(size_t) channel].kit.load(); }

    void setAllKits (int kit)
    {
        for (int c = 0; c < numChannels; ++c)
            setChannelKit (c, kit);
    }

    //==============================================================================
    // Stato per la UI

    /** Lo step che sta suonando adesso (-1 se fermo). */
    int getCurrentStep() const                  { return playing.load() ? currentStep.load() : -1; }

    /** Picco d'uscita dall'ultima lettura (0..1), per il meter. */
    float takeOutputPeak()                      { return outputPeak.exchange (0.0f); }

    /** Quanto sta lavorando il limiter (0 = niente, 1 = molto), per un indicatore. */
    float getLimiterReduction() const           { return limiterReduction.load(); }

    //==============================================================================
    // Callback audio

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const double sr = device->getCurrentSampleRate();
        sampleRate.store (sr);
        restartRequested.store (true);

        for (int c = 0; c < numChannels; ++c)
        {
            appliedKits[(size_t) c] = -1;           // forza il ricalcolo della ricetta
            appliedVersions[(size_t) c] = -1;
            appliedFilter[(size_t) c] = -1.0f;
            voices[(size_t) c].prepare (sr);
            channelFilters[(size_t) c].reset();
            ratchets[(size_t) c] = {};
        }
        updateVoiceParams (sr);

        // Preparazione degli effetti (qui si puo' allocare: non siamo ancora nel callback)
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.62f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        reverb.setParameters (rp);
        reverb.setSampleRate (sr);
        reverb.reset();

        delayBuffer[0].assign ((size_t) (sr * 2.0) + 1, 0.0f);
        delayBuffer[1].assign ((size_t) (sr * 2.0) + 1, 0.0f);
        delayWrite = 0;
        delayLp[0] = delayLp[1] = 0.0f;

        lookahead = juce::jlimit (1, (int) limiterDelay[0].size() - 1, (int) std::round (0.0015 * sr));
        for (auto& b : limiterDelay) b.fill (0.0f);
        limiterPos = 0;
        limiterGain = 1.0f;
        limiterHold = 0;
        limiterRelease = (float) std::exp (-1.0 / (0.12 * sr));
        limiterAttack  = (float) std::exp (-1.0 / (0.0005 * sr));

        metroPhase = 0.0; metroEnv = 0.0f;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const double sr = sampleRate.load();

        if (restartRequested.exchange (false))
        {
            stepIndex = -1;
            samplesUntilNextStep = 0.0;   // il primo step parte subito
        }

        updateVoiceParams (sr);

        // Anteprime richieste dalla UI (tocco sul nome del canale).
        for (int c = 0; c < numChannels; ++c)
            if (channels[(size_t) c].previewRequested.exchange (false))
                triggerChannel (c, 1.0f, 0.0f);

        // Parametri del mix letti una volta per blocco.
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const float g = ch.muted.load() ? 0.0f : ch.volume.load();
            const float angle = (ch.pan.load() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            gainL[(size_t) c] = g * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            gainR[(size_t) c] = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;
            sendRev[(size_t) c] = g * ch.reverb.load();
            sendDel[(size_t) c] = g * ch.delay.load();
            humanizeAmt[(size_t) c] = ch.humanize.load();
        }

        // Elaborazione a blocchi da massimo 'chunk' campioni (gli effetti lavorano su buffer)
        int done = 0;
        while (done < numSamples)
        {
            const int n = juce::jmin (chunk, numSamples - done);
            renderChunk (sr, n);

            for (int ch = 0; ch < numOutputChannels; ++ch)
                if (auto* out = outputChannelData[ch])
                    juce::FloatVectorOperations::copy (out + done, (ch % 2 == 0 ? mixL : mixR).data(), n);

            done += n;
        }
    }

private:
    static constexpr int chunk = 256;

    //==============================================================================
    void updateVoiceParams (double sr)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            auto& ch = channels[(size_t) c];
            const int kit = ch.kit.load();
            const int version = ch.soundVersion.load();

            if (kit != appliedKits[(size_t) c] || version != appliedVersions[(size_t) c])
            {
                appliedKits[(size_t) c] = kit;
                appliedVersions[(size_t) c] = version;

                SoundShape shape { ch.pitch.load(), ch.decay.load(), ch.tone.load(), ch.drive.load() };
                voices[(size_t) c].setParams (shapeParams (SoundLibrary::get (c, kit), shape), shape.tone);
            }

            const float f = ch.filter.load();
            if (std::abs (f - appliedFilter[(size_t) c]) > 0.0005f)
            {
                appliedFilter[(size_t) c] = f;
                // Da 120 Hz a 20 kHz, scala logaritmica
                channelFilters[(size_t) c].set (sr, 120.0 * std::pow (20000.0 / 120.0, (double) f), 0.75);
            }
        }
    }

    void renderChunk (double sr, int n)
    {
        const bool isOn = playing.load();

        // Durata di un sedicesimo in campioni: (60 / BPM) secondi per quarto, / 4.
        const double samplesPerStep = sr * 60.0 / bpm.load() / 4.0;
        // Swing: gli step pari si allungano e i dispari si accorciano (al massimo +-1/3: la "terzina").
        const double swingAmount = (double) swing.load() / 3.0;
        const float  masterGain  = masterVolume.load() * 1.4f;   // il limiter tiene i picchi sotto controllo
        const bool   metroOn     = metronome.load();

        // Delay a tempo: 3/16 (il classico "dotted eighth"), rimbalzo tra sinistra e destra
        const int delaySamples = juce::jlimit (1, (int) delayBuffer[0].size() - 1, (int) (samplesPerStep * 3.0));
        const float delayFeedback = 0.38f;
        const float delayDamp = 0.35f;

        float peak = 0.0f, maxReduction = 0.0f;

        for (int s = 0; s < n; ++s)
        {
            if (isOn)
            {
                if (samplesUntilNextStep <= 0.0)
                    advanceStep (samplesPerStep, swingAmount, metroOn);

                samplesUntilNextStep -= 1.0;

                // Ratchet: colpi aggiuntivi dentro lo step
                for (int c = 0; c < numChannels; ++c)
                {
                    auto& r = ratchets[(size_t) c];
                    if (r.remaining > 0 && --r.countdown <= 0)
                    {
                        triggerChannel (c, r.velocity, humanizeAmt[(size_t) c] * 0.5f);
                        r.velocity *= 0.85f;
                        r.countdown = r.interval;
                        --r.remaining;
                    }
                }
            }

            float l = 0.0f, r = 0.0f, rev = 0.0f, del = 0.0f;

            for (int c = 0; c < numChannels; ++c)
            {
                auto& voice = voices[(size_t) c];
                if (! voice.isActive())
                    continue;

                float x = voice.render();
                if (appliedFilter[(size_t) c] < 0.999f)
                {
                    float lp, bp, hp;
                    channelFilters[(size_t) c].process (x, lp, bp, hp);
                    x = lp;
                }

                l   += x * gainL[(size_t) c];
                r   += x * gainR[(size_t) c];
                rev += x * sendRev[(size_t) c];
                del += x * sendDel[(size_t) c];
            }

            // Delay ping-pong con un po' di attenuazione degli acuti nei rimbalzi
            {
                const size_t size = delayBuffer[0].size();
                const size_t readPos = (delayWrite + size - (size_t) delaySamples) % size;
                const float dl = delayBuffer[0][readPos];
                const float dr = delayBuffer[1][readPos];

                delayLp[0] += delayDamp * (dr - delayLp[0]);
                delayLp[1] += delayDamp * (dl - delayLp[1]);
                delayBuffer[0][delayWrite] = del + delayLp[0] * delayFeedback;
                delayBuffer[1][delayWrite] = delayLp[1] * delayFeedback;
                delayWrite = (delayWrite + 1) % size;

                l += dl;
                r += dr;
            }

            mixL[(size_t) s] = l;
            mixR[(size_t) s] = r;
            revL[(size_t) s] = rev;
            revR[(size_t) s] = rev;
        }

        reverb.processStereo (revL.data(), revR.data(), n);

        for (int s = 0; s < n; ++s)
        {
            float l = (mixL[(size_t) s] + revL[(size_t) s] * 0.6f) * masterGain;
            float r = (mixR[(size_t) s] + revR[(size_t) s] * 0.6f) * masterGain;

            // Metronomo: va dritto in uscita, fuori dagli effetti e dal limiter
            float click = 0.0f;
            if (metroEnv > 0.0005f)
            {
                click = (float) std::sin (metroPhase) * metroEnv * 0.35f;
                metroPhase += juce::MathConstants<double>::twoPi * metroFreq / sr;
                metroEnv *= metroDecay;
            }

            limit (l, r, maxReduction);

            l = juce::jlimit (-1.0f, 1.0f, l + click);
            r = juce::jlimit (-1.0f, 1.0f, r + click);
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            mixL[(size_t) s] = l;
            mixR[(size_t) s] = r;
        }

        if (peak > outputPeak.load())
            outputPeak.store (peak);

        limiterReduction.store (maxReduction);
    }

    /** Limiter con 1.5 ms di "anticipo": vede arrivare i picchi e abbassa il volume prima che passino. */
    void limit (float& l, float& r, float& maxReduction) noexcept
    {
        constexpr float threshold = 0.89f;   // circa -1 dB

        const float inPeak = juce::jmax (std::abs (l), std::abs (r));
        const float target = inPeak > threshold ? threshold / inPeak : 1.0f;

        if (target < holdTarget || limiterHold <= 0)
        {
            holdTarget = target;
            limiterHold = lookahead;
        }
        --limiterHold;

        const float coefToUse = holdTarget < limiterGain ? limiterAttack : limiterRelease;
        limiterGain = holdTarget + coefToUse * (limiterGain - holdTarget);

        // Uscita ritardata di 'lookahead' campioni
        const int readPos = (limiterPos + (int) limiterDelay[0].size() - lookahead) % (int) limiterDelay[0].size();
        limiterDelay[0][(size_t) limiterPos] = l;
        limiterDelay[1][(size_t) limiterPos] = r;
        l = limiterDelay[0][(size_t) readPos] * limiterGain;
        r = limiterDelay[1][(size_t) readPos] * limiterGain;
        limiterPos = (limiterPos + 1) % (int) limiterDelay[0].size();

        maxReduction = juce::jmax (maxReduction, 1.0f - limiterGain);
    }

    void advanceStep (double samplesPerStep, double swingAmount, bool metroOn)
    {
        auto& playPat = patterns[(size_t) playingPattern.load()];
        const int length = playPat.length.load();
        stepIndex = (stepIndex + 1) % length;

        // Fine del giro: se nel frattempo e' stato scelto un altro pattern, si passa a quello
        if (stepIndex == 0 && playingPattern.load() != currentPattern.load())
            playingPattern.store (currentPattern.load());

        auto& pattern = patterns[(size_t) playingPattern.load()];
        if (stepIndex >= pattern.length.load())
            stepIndex = 0;

        currentStep.store (stepIndex);

        const bool even = (stepIndex % 2) == 0;
        const double stepLength = samplesPerStep * (even ? 1.0 + swingAmount : 1.0 - swingAmount);

        for (int c = 0; c < numChannels; ++c)
        {
            const int vel = pattern.velocity[(size_t) c][(size_t) stepIndex].load();
            if (vel <= 0)
                continue;

            const int prob = pattern.probability[(size_t) c][(size_t) stepIndex].load();
            if (prob < 100 && (int) (rng.nextFloat() * 100.0f) >= prob)
                continue;

            const float v = (float) vel / 127.0f;
            triggerChannel (c, v, humanizeAmt[(size_t) c]);

            const int count = juce::jlimit (1, maxRatchet, (int) pattern.ratchet[(size_t) c][(size_t) stepIndex].load());
            auto& r = ratchets[(size_t) c];
            r.remaining = count - 1;
            r.interval = juce::jmax (1, (int) (stepLength / count));
            r.countdown = r.interval;
            r.velocity = v * 0.85f;
        }

        if (metroOn && stepIndex % 4 == 0)
        {
            metroFreq = stepIndex == 0 ? 1760.0 : 1320.0;   // primo quarto piu' acuto
            metroEnv = 1.0f;
            metroPhase = 0.0;
            metroDecay = (float) std::exp (-1.0 / (0.012 * sampleRate.load()));
        }

        samplesUntilNextStep += stepLength;
    }

    void triggerChannel (int c, float velocity, float humanize)
    {
        // L'hi-hat chiuso "strozza" quello aperto, come su una vera drum machine.
        if (c == closedHatChannel)
            voices[openHatChannel].choke();

        voices[(size_t) c].trigger (velocity, humanize);
        channels[(size_t) c].activity.store (velocity);
        channels[(size_t) c].triggerCount.fetch_add (1);
    }

    //==============================================================================
    // Stato condiviso UI <-> audio
    std::array<Channel, numChannels> channels;
    std::array<Pattern, numPatterns> patterns;
    std::atomic<int>    currentPattern   { 0 };   // quello che si vede e si modifica
    std::atomic<int>    playingPattern   { 0 };   // quello che sta suonando
    std::atomic<bool>   playing          { false };
    std::atomic<bool>   restartRequested { true };
    std::atomic<bool>   metronome        { false };
    std::atomic<double> bpm              { 120.0 };
    std::atomic<float>  swing            { 0.0f };
    std::atomic<float>  masterVolume     { 1.0f };
    std::atomic<int>    currentStep      { -1 };
    std::atomic<float>  outputPeak       { 0.0f };
    std::atomic<float>  limiterReduction { 0.0f };
    std::atomic<double> sampleRate       { 48000.0 };

    // Stato usato SOLO dal thread audio
    struct RatchetState { int remaining = 0, interval = 1, countdown = 0; float velocity = 0.0f; };

    std::array<DrumVoice, numChannels>    voices;
    std::array<SVFilter, numChannels>     channelFilters;
    std::array<RatchetState, numChannels> ratchets {};
    std::array<int, numChannels>   appliedKits {}, appliedVersions {};
    std::array<float, numChannels> appliedFilter {}, gainL {}, gainR {}, sendRev {}, sendDel {}, humanizeAmt {};
    juce::Random rng;
    int    stepIndex            = -1;
    double samplesUntilNextStep = 0.0;

    std::array<float, chunk> mixL {}, mixR {}, revL {}, revR {};
    juce::Reverb reverb;
    std::vector<float> delayBuffer[2];
    size_t delayWrite = 0;
    float  delayLp[2] { 0.0f, 0.0f };

    std::array<std::array<float, 256>, 2> limiterDelay {};
    int   lookahead = 72, limiterPos = 0, limiterHold = 0;
    float limiterGain = 1.0f, holdTarget = 1.0f, limiterAttack = 0.9f, limiterRelease = 0.999f;

    double metroPhase = 0.0, metroFreq = 1320.0;
    float  metroEnv = 0.0f, metroDecay = 0.99f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerEngine)
};
