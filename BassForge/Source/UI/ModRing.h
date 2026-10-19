#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <functional>
#include <vector>
#include "CustomLookAndFeel.h"
#include "Parameters/ParameterIDs.h"
#include "Modulation/ModMonitor.h"

namespace bf::ui
{
    using Apvts = juce::AudioProcessorValueTreeState;

    /** (valore base del parametro, somma di modulazione normalizzata) -> valore modulato, nelle unita' dello slider. */
    using ModMapper = std::function<double(double base, double mod)>;

    namespace mapper
    {
        inline ModMapper linear(double scale = 1.0)  { return [scale](double b, double m) { return b + m * scale; }; }
        inline ModMapper octaves(double oct)         { return [oct](double b, double m) { return b * std::exp2(m * oct); }; }
        inline ModMapper semis(double st)            { return [st](double b, double m) { return b + m * st; }; }
    }

    class ModSlider;

    /**
     * Modello (message thread) degli anelli di modulazione:
     *  - range STATICO per destinazione, calcolato dalla matrice (somma delle ampiezze degli slot, con la polarita'
     *    delle sorgenti: unipolari 0..1, bipolari -1..1) => l'arco arancione attorno alla manopola;
     *  - valore LIVE, letto dal ModMonitor scritto dalle voci => il puntino che si muove.
     * update() va chiamato da un timer (~30 Hz); ridisegna solo le manopole visibili che sono cambiate.
     */
    class ModRingModel
    {
    public:
        ModRingModel(juce::AudioProcessorValueTreeState& apvts, const ModMonitor& mon) : monitor(mon)
        {
            for (int i = 0; i < kNumSlots; ++i)
                slots[(size_t) i] = { apvts.getRawParameterValue(ids::slot(i, "src")),
                                      apvts.getRawParameterValue(ids::slot(i, "dst")),
                                      apvts.getRawParameterValue(ids::slot(i, "amt")) };
        }

        bool  hasRange(int d) const noexcept { return d > DstNone && d < DstCount && used[(size_t) d]; }
        float lo(int d) const noexcept       { return lo_[(size_t) d]; }
        float hi(int d) const noexcept       { return hi_[(size_t) d]; }
        float live(int d) const noexcept     { return live_[(size_t) d]; }

        void add(ModSlider* s)    { subscribers.push_back(s); }
        void remove(ModSlider* s) { subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), s), subscribers.end()); }

        void update();

    private:
        static bool bipolar(int src) noexcept
        {
            return (src >= SrcLfo1 && src <= SrcLfo4) || src == SrcKeytrack || src == SrcPitchBend || src == SrcRandom;
        }

        struct Slot { std::atomic<float>* src; std::atomic<float>* dst; std::atomic<float>* amt; };
        std::array<Slot, kNumSlots> slots{};
        const ModMonitor& monitor;
        std::array<float, DstCount> lo_{}, hi_{}, live_{}, prevLive_{}, prevLo_{}, prevHi_{};
        std::array<bool, DstCount> used{}, prevUsed_{};
        std::vector<ModSlider*> subscribers;
    };

    /** Manopola rotativa con anello di modulazione (range + valore live) attorno all'arco del valore. */
    class ModSlider : public juce::Slider
    {
    public:
        ModSlider() { setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); }
        ~ModSlider() override { if (model != nullptr) model->remove(this); }

        void setModulation(ModRingModel* m, int destination, ModMapper mp)
        {
            if (model != nullptr) model->remove(this);
            model = m; dest = destination; map = std::move(mp);
            if (model != nullptr && dest > DstNone) model->add(this);
        }
        int getDest() const noexcept { return dest; }

        void paint(juce::Graphics& g) override
        {
            juce::Slider::paint(g);
            if (model == nullptr || dest <= DstNone || ! model->hasRange(dest) || ! map) return;

            const auto b = getLookAndFeel().getSliderLayout(*this).sliderBounds.toFloat();
            const auto geo = CustomLookAndFeel::knobGeometry(b);
            const auto rp = getRotaryParameters();
            const float a0 = rp.startAngleRadians, a1 = rp.endAngleRadians;
            const float ringR = geo.radius + 3.0f;
            const double base = getValue();
            auto prop = [&](double v) { return (float) valueToProportionOfLength(juce::jlimit(getMinimum(), getMaximum(), v)); };
            auto angle = [&](float p) { return a0 + p * (a1 - a0); };

            float pLo = prop(map(base, (double) model->lo(dest))), pHi = prop(map(base, (double) model->hi(dest)));
            if (pLo > pHi) std::swap(pLo, pHi);

            juce::Path full; full.addCentredArc(geo.centre.x, geo.centre.y, ringR, ringR, 0, a0, a1, true);
            g.setColour(col::orangeDim.withAlpha(0.45f));
            g.strokePath(full, juce::PathStrokeType(1.0f));

            if (pHi - pLo > 0.002f)
            {
                juce::Path rng; rng.addCentredArc(geo.centre.x, geo.centre.y, ringR, ringR, 0, angle(pLo), angle(pHi), true);
                g.setColour(col::orange.withAlpha(0.22f));
                g.strokePath(rng, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(col::orange);
                g.strokePath(rng, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const float pl = prop(map(base, (double) model->live(dest)));
            const float ang = angle(pl);
            const float dx = geo.centre.x + std::sin(ang) * ringR, dy = geo.centre.y - std::cos(ang) * ringR;
            g.setColour(col::orange.withAlpha(0.35f)); g.fillEllipse(dx - 5.0f, dy - 5.0f, 10.0f, 10.0f);
            g.setColour(juce::Colours::white);          g.fillEllipse(dx - 3.0f, dy - 3.0f, 6.0f, 6.0f);
        }

    private:
        ModRingModel* model = nullptr;
        int dest = DstNone;
        ModMapper map;
    };

    inline void ModRingModel::update()
    {
        lo_.fill(0.0f); hi_.fill(0.0f); used.fill(false);
        for (const auto& s : slots)
        {
            const int src = (int) (s.src->load(std::memory_order_relaxed) + 0.5f);
            const int dst = (int) (s.dst->load(std::memory_order_relaxed) + 0.5f);
            const float a = s.amt->load(std::memory_order_relaxed);
            if (src <= SrcNone || src >= SrcCount || dst <= DstNone || dst >= DstCount || a == 0.0f) continue;
            const float mn = bipolar(src) ? -1.0f : 0.0f, mx = 1.0f;
            lo_[(size_t) dst] += std::min(a * mn, a * mx);
            hi_[(size_t) dst] += std::max(a * mn, a * mx);
            used[(size_t) dst] = true;
        }
        for (int d = 0; d < DstCount; ++d)
        {
            lo_[(size_t) d] = juce::jlimit(-modscale::kMaxSum, modscale::kMaxSum, lo_[(size_t) d]);
            hi_[(size_t) d] = juce::jlimit(-modscale::kMaxSum, modscale::kMaxSum, hi_[(size_t) d]);
            live_[(size_t) d] = monitor.get(d);
        }
        for (auto* s : subscribers)
        {
            const int d = s->getDest();
            if (! s->isShowing()) continue;
            const bool liveMoved = std::abs(live_[(size_t) d] - prevLive_[(size_t) d]) > 0.001f;
            const bool rangeChanged = used[(size_t) d] != prevUsed_[(size_t) d]
                                   || lo_[(size_t) d] != prevLo_[(size_t) d] || hi_[(size_t) d] != prevHi_[(size_t) d];
            if ((used[(size_t) d] && liveMoved) || rangeChanged) s->repaint();
        }
        prevLive_ = live_; prevUsed_ = used; prevLo_ = lo_; prevHi_ = hi_;
    }
}
