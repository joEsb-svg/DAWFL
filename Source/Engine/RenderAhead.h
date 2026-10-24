#pragma once

#include <JuceHeader.h>
#include <atomic>

//==============================================================================
/**
    "Riserva audio": l'audio viene calcolato in anticipo da un thread dedicato e messo in una riserva
    (qualche decina di millisecondi). La scheda audio prende i campioni dalla riserva.

    Perche': sul telefono il tempo per calcolare un blocco varia molto (il sistema sposta il thread tra
    core veloci e lenti, cambia la frequenza della CPU...). In media c'e' tempo d'avanzo, ma basta un blocco
    in ritardo per sentire uno scatto. Con la riserva i picchi vengono assorbiti: conta solo la media.
    In cambio l'audio esce qualche millisecondo dopo (impostabile, 0 = disattivata).
*/
class RenderAhead : public juce::AudioIODeviceCallback,
                    private juce::Thread
{
public:
    explicit RenderAhead (juce::AudioIODeviceCallback& source) : juce::Thread ("JLoop audio"), engine (source) {}
    ~RenderAhead() override     { stopWorker(); }

    /** Millisecondi di riserva (0 = calcolo diretto nel callback della scheda audio). */
    void setReserveMs (int ms)  { reserveMs.store (juce::jlimit (0, 200, ms)); }
    int getReserveMs() const    { return reserveMs.load(); }

    /** Volte in cui la riserva si e' svuotata (scatto udibile), dall'ultima lettura. */
    int takeUnderruns()         { return underruns.exchange (0); }

    //==========================================================================
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        stopWorker();
        engine.audioDeviceAboutToStart (device);
        sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
        fifo.reset();
        ring.clear();
        // La riserva si aggiunge al blocco della scheda audio (che viene prelevato tutto in una volta)
        const int deviceBlock = device != nullptr ? device->getCurrentBufferSizeSamples() : 256;
        lead = reserveMs.load() > 0 ? juce::jmin (capacity - 2 * chunk, (int) (sampleRate * reserveMs.load() / 1000.0) + deviceBlock) : 0;
        if (lead > 0)
            startRealtimeThread (juce::Thread::RealtimeOptions{}.withPriority (9).withApproximateAudioProcessingTime (chunk, sampleRate));
    }

    void audioDeviceStopped() override
    {
        stopWorker();
        engine.audioDeviceStopped();
    }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& ctx) override
    {
        if (lead <= 0 || ! isThreadRunning())
        {
            engine.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, ctx);
            return;
        }

        // Dalla riserva; se non basta (il calcolo e' rimasto indietro) il resto e' silenzio
        int got = 0;
        {
            const auto scope = fifo.read (numSamples);
            auto copy = [&] (int start, int size, int dest)
            {
                for (int ch = 0; ch < numOut; ++ch)
                    if (auto* o = out[ch])
                        juce::FloatVectorOperations::copy (o + dest, ring.getReadPointer (ch % 2, start), size);
            };
            if (scope.blockSize1 > 0) copy (scope.startIndex1, scope.blockSize1, 0);
            if (scope.blockSize2 > 0) copy (scope.startIndex2, scope.blockSize2, scope.blockSize1);
            got = scope.blockSize1 + scope.blockSize2;
        }
        if (got < numSamples)
        {
            for (int ch = 0; ch < numOut; ++ch)
                if (auto* o = out[ch])
                    juce::FloatVectorOperations::clear (o + got, numSamples - got);
            if (primed.load())
                underruns.fetch_add (1);
        }
        wake.signal();
    }

private:
    void run() override
    {
        std::vector<float> l ((size_t) chunk), r ((size_t) chunk);
        float* chans[2] { l.data(), r.data() };
        juce::AudioIODeviceCallbackContext ctx;
        primed.store (false);

        while (! threadShouldExit())
        {
            // Riempie fino alla riserva voluta, un blocco alla volta
            while (fifo.getNumReady() < lead && fifo.getFreeSpace() >= chunk && ! threadShouldExit())
            {
                engine.audioDeviceIOCallbackWithContext (nullptr, 0, chans, 2, chunk, ctx);
                const auto scope = fifo.write (chunk);
                auto put = [&] (int start, int size, int src)
                {
                    ring.copyFrom (0, start, l.data() + src, size);
                    ring.copyFrom (1, start, r.data() + src, size);
                };
                if (scope.blockSize1 > 0) put (scope.startIndex1, scope.blockSize1, 0);
                if (scope.blockSize2 > 0) put (scope.startIndex2, scope.blockSize2, scope.blockSize1);
            }
            primed.store (true);
            wake.wait (5);
        }
    }

    void stopWorker()
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (1000);
        primed.store (false);
    }

    static constexpr int chunk = 256, capacity = 16384;
    juce::AudioIODeviceCallback& engine;
    juce::AbstractFifo fifo { capacity };
    juce::AudioBuffer<float> ring { 2, capacity };
    juce::WaitableEvent wake;
    std::atomic<int> reserveMs { 0 }, underruns { 0 };
    std::atomic<bool> primed { false };
    double sampleRate = 48000.0;
    int lead = 0;
};
