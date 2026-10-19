#pragma once
#include "MultibandOTT.h"
#include "HyperDrive.h"
#include "DimensionExpander.h"
#include "MasterDelay.h"
#include "MasterReverb.h"

namespace bf
{
    struct FxParams
    {
        OttParams       ott;
        DriveParams     drive;
        DimensionParams dim;
        DelayParams     delay;
        ReverbParams    reverb;
        int             order = 0;       // permutazione dei primi 3 stadi (vedi kOrderNames)
    };

    /**
     * Catena FX master in cascata:   [OTT | Hyper-Drive | Dimension]  ->  Delay  ->  Reverb
     * I tre stadi iniziali si possono riordinare (6 permutazioni); ogni stadio ha il proprio on/off senza click
     * (dry/wet con rampa) e i moduli spenti costano ~0.
     *
     * LATENZA: costante e pari a kLatencySamples (36) in ogni configurazione (il FIR dell'oversampling dell'Hyper-Drive
     * e' compensato anche da bypass / 1x / 2x). Il plugin la dichiara all'host e ritarda di tanto il Sub Direct-Out.
     *
     * process() lavora in-place su L/R di lunghezza qualsiasi. Nessuna allocazione dopo prepare().
     */
    class MasterFXChain
    {
    public:
        static constexpr int kLatencySamples = HyperDrive::kLatency;
        static constexpr int kNumOrders = 6;
        static constexpr const char* kOrderNames[kNumOrders] = {
            "OTT > Drive > Dim", "Drive > OTT > Dim", "OTT > Dim > Drive",
            "Dim > OTT > Drive", "Drive > Dim > OTT", "Dim > Drive > OTT" };

        void prepare(double sampleRate)
        {
            ott.prepare(sampleRate); drive.prepare(sampleRate); dim.prepare(sampleRate);
            delay.prepare(sampleRate); reverb.prepare(sampleRate);
        }

        void reset() noexcept { ott.reset(); drive.reset(); dim.reset(); delay.reset(); reverb.reset(); }

        void setParams(const FxParams& p) noexcept
        {
            order = std::min(std::max(p.order, 0), kNumOrders - 1);
            ott.setParams(p.ott); drive.setParams(p.drive); dim.setParams(p.dim);
            delay.setParams(p.delay); reverb.setParams(p.reverb);
        }

        void process(float* L, float* R, int n) noexcept
        {
            static constexpr int kOrder[kNumOrders][3] = { {0,1,2}, {1,0,2}, {0,2,1}, {2,0,1}, {1,2,0}, {2,1,0} };
            for (int s = 0; s < 3; ++s)
                switch (kOrder[order][s])
                {
                    case 0:  ott.process(L, R, n); break;
                    case 1:  drive.process(L, R, n); break;
                    default: dim.process(L, R, n); break;
                }
            delay.process(L, R, n);
            reverb.process(L, R, n);
            for (int i = 0; i < n; ++i) { L[i] = sanitize(L[i]); R[i] = sanitize(R[i]); }      // rete di sicurezza
        }

        MultibandOTT& getOtt() noexcept { return ott; }

    private:
        MultibandOTT ott; HyperDrive drive; DimensionExpander dim; MasterDelay delay; MasterReverb reverb;
        int order = 0;
    };
}
