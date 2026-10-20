#pragma once

#include <JuceHeader.h>
#include "SequencerEngine.h"
#include "ProjectIO.h"
#include "SampleStore.h"
#include "../Instruments/Instruments.h"

//==============================================================================
/**
    Esporta il progetto in un file audio (WAV o OGG), in un thread a parte.

    Il progetto viene copiato (JSON) in un secondo motore, con una copia di ogni strumento:
    l'audio dal vivo non viene toccato e si puo' continuare a usare l'app.
    Il calcolo e' "offline": piu' veloce del tempo reale, sempre identico, senza scatti.
*/
class AudioExporter : public juce::Thread
{
public:
    enum class Format { wav, ogg };

    struct Settings
    {
        bool   song = false;          // true = la playlist intera, false = il pattern selezionato
        int    loops = 4;             // quante volte ripetere il pattern
        Format format = Format::wav;
        double sampleRate = 44100.0;
        double tailSeconds = 2.5;     // coda di riverbero e delay dopo la fine
        bool   analyse = false;       // true = niente file: si calcola il mastering automatico
    };

    /** Da creare sul thread della UI: prepara la copia del progetto. */
    AudioExporter (const juce::var& projectJson, SampleStore& store, const Settings& s, const juce::File& destination)
        : juce::Thread ("JLoop export"), settings (s), file (destination)
    {
        engine = std::make_unique<SequencerEngine>();
        ProjectIO::fromJson (*engine, projectJson);
        instruments = std::make_unique<Instruments::Host> (*engine);
        instruments->resolve();   // copie degli strumenti con lo stesso stato (preset, parametri)

        // I campioni (libreria e utente) si caricano qui, sul thread della UI
        for (int c = 0; c < engine->getNumChannels(); ++c)
        {
            auto& ch = engine->getChannel (c);
            ch.sample.store (engine->isSynth (c) || ch.sampleName.isEmpty() ? nullptr : store.load (ch.sampleName));
        }

        engine->setMetronome (false);
        engine->setSongMode (settings.song);
        engine->setSongPosition (0);

        if (settings.analyse)
        {
            // L'analisi ascolta il mix senza mastering; parte dallo stile e dal volume scelti
            auto& m = engine->getMastering();
            analysisResult.styleIndex.store (m.styleIndex.load());
            analysisResult.loudness.store (m.loudness.load());
            analysisResult.warmth.store (m.warmth.load());
            analysisResult.ceiling.store (m.ceiling.load());
            analysisResult.glue.store (m.glue.load());
            analysisResult.width.store (m.width.load());
            m.enabled.store (false);
        }
    }

    /** Dopo un'analisi riuscita: copia i controlli calcolati nel mastering del progetto. */
    void applyAnalysisTo (mastering::Params& target) const
    {
        target.gainDb.store (analysisResult.gainDb.load());
        target.lowDb.store (analysisResult.lowDb.load());
        target.presenceDb.store (analysisResult.presenceDb.load());
        target.airDb.store (analysisResult.airDb.load());
        target.glue.store (analysisResult.glue.load());
        target.width.store (analysisResult.width.load());
        target.measuredLufs = analysisResult.measuredLufs;
        target.measuredPeakDb = analysisResult.measuredPeakDb;
        target.resultLufs = analysisResult.resultLufs;
        target.analysedLoudness = analysisResult.analysedLoudness;
        target.analysed = analysisResult.analysed;
    }

    bool isAnalysis() const     { return settings.analyse; }

    ~AudioExporter() override   { stopThread (10000); }

    /** Durata (senza coda) in secondi, per mostrarla prima di esportare. */
    double getMusicSeconds() const
    {
        const double secondsPerStep = 60.0 / engine->getBpm() / 4.0;
        return getNumSteps() * secondsPerStep;
    }

    int getNumSteps() const
    {
        return settings.song ? engine->getSongLengthSteps()
                             : engine->getPatternLength() * juce::jmax (1, settings.loops);
    }

    float getProgress() const           { return progress.load(); }
    bool isFinished() const             { return finished.load(); }
    juce::String getError() const       { const juce::ScopedLock sl (lock); return error; }
    juce::File getFile() const          { return file; }

    void run() override
    {
        const auto result = render();
        {
            const juce::ScopedLock sl (lock);
            error = result;
        }
        if (result.isNotEmpty() && ! settings.analyse)
            file.deleteFile();
        finished.store (true);
    }

private:
    juce::String render()
    {
        const int steps = getNumSteps();
        if (steps <= 0)
            return settings.song ? "La playlist e' vuota." : "Il pattern e' vuoto.";

        const double sr = settings.sampleRate;
        const auto musicSamples = (juce::int64) std::llround (steps * sr * 60.0 / engine->getBpm() / 4.0);
        const auto tailSamples  = (juce::int64) (settings.tailSeconds * sr);
        const auto total = musicSamples + tailSamples;

        if (settings.analyse)
            return renderAnalysis (sr, musicSamples, total);

        file.getParentDirectory().createDirectory();
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (out.get())->openedOk())
            return "Non riesco a creare il file.";

        juce::WavAudioFormat wav;
        juce::OggVorbisAudioFormat ogg;
        juce::AudioFormat& format = settings.format == Format::wav ? static_cast<juce::AudioFormat&> (wav) : ogg;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                             .withNumChannels (2)
                                                             .withBitsPerSample (16)
                                                             .withQualityOptionIndex (settings.format == Format::ogg ? 6 : 0);   // OGG 192 kbps
        auto writer = format.createWriterFor (out, options);
        if (writer == nullptr)
            return "Formato non disponibile.";

        engine->prepare (sr);
        engine->setPlaying (true);

        constexpr int block = 512;
        juce::AudioBuffer<float> buffer (2, block);
        juce::int64 done = 0;
        bool stopped = false;

        while (done < total)
        {
            if (threadShouldExit())
                return "Esportazione annullata.";

            // Alla fine della musica si ferma esattamente li': poi solo la coda degli effetti
            int n = (int) juce::jmin ((juce::int64) block, total - done);
            if (! stopped && done < musicSamples)
                n = (int) juce::jmin ((juce::int64) n, musicSamples - done);
            if (! stopped && done >= musicSamples)
            {
                engine->setPlaying (false);
                stopped = true;
            }

            buffer.clear();
            engine->process (buffer.getArrayOfWritePointers(), 2, n);

            // Sfumatura negli ultimi 50 ms della coda
            const auto fadeStart = total - (juce::int64) (0.05 * sr);
            for (int i = 0; i < n; ++i)
                if (done + i > fadeStart)
                {
                    const float g = (float) (total - (done + i)) / (float) (total - fadeStart);
                    buffer.setSample (0, i, buffer.getSample (0, i) * g);
                    buffer.setSample (1, i, buffer.getSample (1, i) * g);
                }

            if (! writer->writeFromAudioSampleBuffer (buffer, 0, n))
                return "Errore durante la scrittura (spazio esaurito?).";

            done += n;
            progress.store ((float) ((double) done / (double) total));
        }

        writer.reset();   // chiude il file
        return {};
    }

    /** Calcola il mix in memoria (al massimo 8 minuti), poi il mastering automatico. */
    juce::String renderAnalysis (double sr, juce::int64 musicSamples, juce::int64 total)
    {
        total = juce::jmin (total, (juce::int64) (8 * 60 * sr));
        std::vector<float> L ((size_t) total), R ((size_t) total);

        engine->prepare (sr);
        engine->setPlaying (true);
        constexpr int block = 512;
        juce::int64 done = 0;
        while (done < total)
        {
            if (threadShouldExit())
                return "Analisi annullata.";
            int n = (int) juce::jmin ((juce::int64) block, total - done);
            if (done < musicSamples)
                n = (int) juce::jmin ((juce::int64) n, musicSamples - done);
            else
                engine->setPlaying (false);
            float* chans[2] = { L.data() + done, R.data() + done };
            engine->process (chans, 2, n);
            done += n;
            progress.store (0.6f * (float) ((double) done / (double) total));
        }

        mastering::analyse (L, R, sr, analysisResult, [this] { progress.store (juce::jmin (0.98f, progress.load() + 0.08f)); return threadShouldExit(); });
        if (! analysisResult.analysed)
            return "Analisi annullata.";
        progress.store (1.0f);
        return {};
    }

    Settings settings;
    mastering::Params analysisResult;
    juce::File file;
    std::unique_ptr<SequencerEngine> engine;
    std::unique_ptr<Instruments::Host> instruments;   // distrutto prima del motore
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false };
    juce::CriticalSection lock;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioExporter)
};
