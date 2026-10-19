#pragma once
#include "ParameterIDs.h"
#include <array>
#include <atomic>
#include "Modulation/ModulationMatrix.h"
#include "FX/MasterFXChain.h"

namespace bf
{
    // Puntatori atomici ai valori grezzi dell'APVTS, risolti UNA volta nel costruttore.
    // Nel thread audio si legge solo con ->load(): nessuna ricerca per stringa, nessuna allocazione.
    struct ParamCache
    {
        using P = std::atomic<float>*;

        struct Osc    { P on, wtPos, semi, fine, level, pan, uni, det, spread, blend, warp, wamt, waux; };
        struct Sub    { P on, wave, oct, level, phase, reset, direct, followMod; };
        struct Noise  { P on, type, level, width; };
        struct Sample { P on, slot, level, pitch, pitchTrack, loop, loopStart, loopEnd, decay; };
        struct NFilt  { P on, type, cutoff, res, track; };
        struct Filter { P on, type, cutoff, res, drive, keytrack, fb, tune, vowel, fshift; };
        struct Env    { P a, h, d, s, r, ac, dc, rc; };
        struct Lfo    { P rate, shape, retrig, sync, div, phase, fade, pmode, phold, pstep; };
        struct Slot   { P src, dst, amount; };

        std::array<Osc, kNumOsc>       osc{};
        Sub                            sub{};
        Noise                          noise{};
        Sample                         sample{};
        NFilt                          noiseFilter{};
        std::array<Filter, kNumFilter> filter{};
        P                              filterRouting{}, filterMorph{};
        std::array<Env, kNumEnv>       env{};
        std::array<Lfo, kNumLfo>       lfo{};
        std::array<P, kNumMacro>       macro{};
        std::array<Slot, kNumSlots>    slot{};
        P                              pitchBendRange{};

        struct FxOtt { P on, depth, time, up, down, low, mid, high, x1, x2, mix; };
        struct FxDrv { P on, type, amount, bias, stages, os, out, mix; };
        struct FxDim { P on, amount, width, size, rate, mono, mix; };
        struct FxDly { P on, time, sync, div, fb, ping, damp, hpf, mix; };
        struct FxRev { P on, size, damp, width, pre, hpf, mix; };
        FxOtt ott{}; FxDrv drv{}; FxDim dim{}; FxDly dly{}; FxRev rev{};
        P fxOrder{}, masterGain{}, masterLimiter{};

        /** Riempie FxParams dai parametri (solo letture atomic: nessuna allocazione). */
        void fillFxParams(FxParams& f, double bpm) const noexcept
        {
            auto L = [](P p) { return p->load(std::memory_order_relaxed); };
            auto B = [&](P p) { return L(p) > 0.5f; };
            auto I = [&](P p) { return (int) (L(p) + 0.5f); };
            f.ott = { B(ott.on), L(ott.depth), L(ott.time), L(ott.up), L(ott.down), L(ott.low), L(ott.mid), L(ott.high),
                      L(ott.x1), L(ott.x2), L(ott.mix) };
            f.drive.on = B(drv.on);  f.drive.type = (DriveType) std::min(std::max(I(drv.type), 0), 2);
            f.drive.amount = L(drv.amount);  f.drive.bias = L(drv.bias);  f.drive.stages = I(drv.stages) + 1;
            const int os = I(drv.os);  f.drive.oversample = os == 0 ? 1 : (os == 1 ? 2 : 4);
            f.drive.outDb = L(drv.out);  f.drive.mix = L(drv.mix);
            f.dim = { B(dim.on), L(dim.amount), L(dim.width), L(dim.size), L(dim.rate), L(dim.mono), L(dim.mix) };
            f.delay.on = B(dly.on);  f.delay.timeMs = L(dly.time);  f.delay.sync = B(dly.sync);  f.delay.division = I(dly.div);
            f.delay.feedback = L(dly.fb);  f.delay.pingPong = B(dly.ping);  f.delay.dampHz = L(dly.damp);
            f.delay.hpfHz = L(dly.hpf);  f.delay.mix = L(dly.mix);  f.delay.bpm = bpm;
            f.reverb = { B(rev.on), L(rev.size), L(rev.damp), L(rev.width), L(rev.pre), L(rev.hpf), L(rev.mix) };
            f.order = I(fxOrder);
        }

        /** Legge i kNumSlots slot della matrice dai parametri (3 atomic per slot): nessuna allocazione. */
        void fillModTable(ModulationMatrix::Table& t) const noexcept
        {
            t.clear();
            for (int i = 0; i < kNumSlots; ++i)
            {
                const auto& s = slot[(size_t) i];
                t.add((int) s.src->load(std::memory_order_relaxed), (int) s.dst->load(std::memory_order_relaxed),
                      s.amount->load(std::memory_order_relaxed));      // add() scarta None / indici non validi
            }
        }

        void bind(juce::AudioProcessorValueTreeState& apvts)
        {
            auto get = [&](const juce::String& id) -> P
            {
                auto* p = apvts.getRawParameterValue(id);
                jassert(p != nullptr);
                return p;
            };

            for (int i = 0; i < kNumOsc; ++i)
                osc[(size_t) i] = { get(ids::osc(i, "on")),   get(ids::osc(i, "wt")),
                                    get(ids::osc(i, "semi")), get(ids::osc(i, "fine")),
                                    get(ids::osc(i, "level")), get(ids::osc(i, "pan")),
                                    get(ids::osc(i, "uni")), get(ids::osc(i, "det")), get(ids::osc(i, "spread")),
                                    get(ids::osc(i, "blend")), get(ids::osc(i, "warp")), get(ids::osc(i, "wamt")),
                                    get(ids::osc(i, "waux")) };

            sub   = { get(ids::subOn), get(ids::subWave), get(ids::subOct), get(ids::subLevel),
                      get(ids::subPhase), get(ids::subReset), get(ids::subDirect), get(ids::subFollowMod) };
            noise = { get(ids::noiseOn), get(ids::noiseType), get(ids::noiseLevel), get(ids::noiseWidth) };
            sample = { get(ids::smpOn), get(ids::smpSlot), get(ids::smpLevel), get(ids::smpPitch),
                       get(ids::smpPitchTrack), get(ids::smpLoop), get(ids::smpLoopStart),
                       get(ids::smpLoopEnd), get(ids::smpDecay) };
            noiseFilter = { get(ids::nfltOn), get(ids::nfltType), get(ids::nfltCutoff),
                            get(ids::nfltRes), get(ids::nfltTrack) };

            for (int i = 0; i < kNumFilter; ++i)
                filter[(size_t) i] = { get(ids::filt(i, "on")),  get(ids::filt(i, "type")),
                                       get(ids::filt(i, "cutoff")), get(ids::filt(i, "res")),
                                       get(ids::filt(i, "drive")),  get(ids::filt(i, "keytrack")),
                                       get(ids::filt(i, "fb")),     get(ids::filt(i, "tune")),
                                       get(ids::filt(i, "vowel")),  get(ids::filt(i, "fshift")) };
            filterRouting = get(ids::filterRouting);
            filterMorph = get(ids::filterMorph);

            for (int i = 0; i < kNumEnv; ++i)
                env[(size_t) i] = { get(ids::env(i, "a")),  get(ids::env(i, "h")),  get(ids::env(i, "d")),
                                    get(ids::env(i, "s")),  get(ids::env(i, "r")),
                                    get(ids::env(i, "ac")), get(ids::env(i, "dc")), get(ids::env(i, "rc")) };

            for (int i = 0; i < kNumLfo; ++i)
                lfo[(size_t) i] = { get(ids::lfo(i, "rate")),  get(ids::lfo(i, "shape")), get(ids::lfo(i, "retrig")),
                                    get(ids::lfo(i, "sync")),  get(ids::lfo(i, "div")),   get(ids::lfo(i, "phase")),
                                    get(ids::lfo(i, "fade")),  get(ids::lfo(i, "pmode")), get(ids::lfo(i, "phold")),
                                    get(ids::lfo(i, "pstep")) };

            for (int i = 0; i < kNumMacro; ++i)
                macro[(size_t) i] = get(ids::macro(i));

            for (int i = 0; i < kNumSlots; ++i)
                slot[(size_t) i] = { get(ids::slot(i, "src")), get(ids::slot(i, "dst")), get(ids::slot(i, "amt")) };

            pitchBendRange = get(ids::pitchBendRange);

            ott = { get(ids::ottOn), get(ids::ottDepth), get(ids::ottTime), get(ids::ottUp), get(ids::ottDown),
                    get(ids::ottLow), get(ids::ottMid), get(ids::ottHigh), get(ids::ottX1), get(ids::ottX2), get(ids::ottMix) };
            drv = { get(ids::drvOn), get(ids::drvType), get(ids::drvAmount), get(ids::drvBias), get(ids::drvStages),
                    get(ids::drvOs), get(ids::drvOut), get(ids::drvMix) };
            dim = { get(ids::dimOn), get(ids::dimAmount), get(ids::dimWidth), get(ids::dimSize), get(ids::dimRate),
                    get(ids::dimMono), get(ids::dimMix) };
            dly = { get(ids::dlyOn), get(ids::dlyTime), get(ids::dlySync), get(ids::dlyDiv), get(ids::dlyFb),
                    get(ids::dlyPing), get(ids::dlyDamp), get(ids::dlyHpf), get(ids::dlyMix) };
            rev = { get(ids::revOn), get(ids::revSize), get(ids::revDamp), get(ids::revWidth), get(ids::revPre),
                    get(ids::revHpf), get(ids::revMix) };
            fxOrder = get(ids::fxOrder);
            masterGain = get(ids::masterGain);
            masterLimiter = get(ids::masterLimiter);
        }
    };

    inline float load(const std::atomic<float>* p) noexcept { return p->load(std::memory_order_relaxed); }
}
