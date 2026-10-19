#pragma once

#include <JuceHeader.h>
#include "LibraryIndex.h"

//==============================================================================
/** Un campione audio caricato in memoria (mono), pronto per il thread audio. */
struct SampleData : public juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::String name;
    juce::AudioBuffer<float> audio;   // un canale
    double sampleRate = 44100.0;
    int    rootNote = 60;             // nota MIDI a cui e' registrato (le note del piano roll sono relative a questa)
    bool   gated = false;             // nel piano roll la nota sfuma alla sua fine (strumenti "tenuti")
    float  release = 0.15f;           // secondi della sfumatura
};

//==============================================================================
/**
    I campioni importati dall'utente.

    - Vengono copiati nella cartella privata dell'app (.../DAWFL/Samples), cosi' i progetti
      continuano a funzionare anche se il file originale viene spostato o cancellato.
    - Restano in memoria per tutta la sessione: il thread audio li usa tramite puntatori
      semplici, quindi non vanno mai liberati mentre potrebbero suonare.
*/
class SampleStore
{
public:
    static constexpr double maxSeconds = 12.0;

    SampleStore()   { formats.registerBasicFormats(); }

    static juce::File getFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("DAWFL").getChildFile ("Samples");
    }

    /** Copia un file scelto dall'utente (anche un content:// di Android) nella cartella dei campioni. */
    juce::String importFromUrl (const juce::URL& url, juce::String& error)
    {
        auto name = cleanFileName (url);
        if (name.isEmpty())
            name = "campione.wav";

        auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
        if (in == nullptr)
        {
            error = "Non riesco a leggere il file.";
            return {};
        }

        auto folder = getFolder();
        folder.createDirectory();
        auto dest = folder.getChildFile (name);

        // Nome gia' usato da un altro campione: aggiungi un numero
        for (int i = 2; dest.existsAsFile(); ++i)
            dest = folder.getChildFile (juce::File (name).getFileNameWithoutExtension() + " " + juce::String (i)
                                        + juce::File (name).getFileExtension());

        {
            juce::FileOutputStream out (dest);
            if (! out.openedOk() || out.writeFromInputStream (*in, 50 * 1024 * 1024) <= 0)
            {
                dest.deleteFile();
                error = "Non riesco a copiare il file.";
                return {};
            }
        }

        // Senza estensione (succede con alcuni file di Android): la deduce dal contenuto
        if (dest.getFileExtension().isEmpty())
        {
            std::unique_ptr<juce::AudioFormatReader> probe (formats.createReaderFor (std::make_unique<juce::FileInputStream> (dest)));
            if (probe != nullptr)
            {
                const auto ext = probe->getFormatName().containsIgnoreCase ("wav")  ? ".wav"
                               : probe->getFormatName().containsIgnoreCase ("aiff") ? ".aiff"
                               : probe->getFormatName().containsIgnoreCase ("flac") ? ".flac"
                               : probe->getFormatName().containsIgnoreCase ("ogg")  ? ".ogg" : ".mp3";
                probe.reset();
                auto renamed = dest.withFileExtension (ext);
                if (dest.moveFileTo (renamed))
                    dest = renamed;
            }
        }

        if (load (dest.getFileName()) == nullptr)
        {
            dest.deleteFile();
            error = "Formato non supportato. Usa WAV, AIFF, FLAC, OGG o MP3.";
            return {};
        }

        return dest.getFileName();
    }

    //==============================================================================
    // Libreria di JLoop (suoni inclusi nell'app). Nei progetti sono salvati come "lib:<id>".

    static constexpr const char* libraryPrefix = "lib:";

    static bool isLibrary (const juce::String& name)     { return name.startsWith (libraryPrefix); }
    static juce::String libraryName (const juce::String& id)   { return libraryPrefix + id; }

    static const LibraryIndex::Entry* findLibraryEntry (const juce::String& name)
    {
        const auto id = isLibrary (name) ? name.substring (4) : name;
        for (auto& e : LibraryIndex::entries)
            if (id == e.id)
                return &e;
        return nullptr;
    }

    /** Il campione con quel nome (caricandolo dal disco o dalla libreria la prima volta), o nullptr. */
    SampleData* load (const juce::String& fileName)
    {
        if (fileName.isEmpty())
            return nullptr;

        for (auto* s : loaded)
            if (s->name == fileName)
                return s;

        if (isLibrary (fileName))
            return loadLibrary (fileName);

        auto file = getFolder().getChildFile (fileName);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)   // estensione assente o sbagliata: prova a riconoscere il contenuto
            reader.reset (formats.createReaderFor (std::make_unique<juce::FileInputStream> (file)));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        const auto length = (int) juce::jmin ((juce::int64) (maxSeconds * reader->sampleRate), reader->lengthInSamples);
        juce::AudioBuffer<float> temp ((int) reader->numChannels, length);
        reader->read (&temp, 0, length, 0, true, true);

        SampleData::Ptr data = new SampleData();
        data->name = fileName;
        data->sampleRate = reader->sampleRate;
        data->audio.setSize (1, length);
        data->audio.clear();

        // Mono: media dei canali
        for (int ch = 0; ch < temp.getNumChannels(); ++ch)
            data->audio.addFrom (0, 0, temp, ch, 0, length, 1.0f / (float) temp.getNumChannels());

        // Normalizza il picco a -1 dB, cosi' tutti i campioni hanno un volume simile
        const float peak = data->audio.getMagnitude (0, 0, length);
        if (peak > 0.0001f)
            data->audio.applyGain (0.89f / peak);

        loaded.add (data);
        return data.get();
    }

    juce::StringArray listSamples() const
    {
        juce::StringArray names;
        for (auto& f : getFolder().findChildFiles (juce::File::findFiles, false))
            if (formats.findFormatForFileExtension (f.getFileExtension()) != nullptr)
                names.add (f.getFileName());
        names.sortNatural();
        return names;
    }

    static juce::String displayName (const juce::String& fileName)
    {
        if (isLibrary (fileName))
            if (auto* e = findLibraryEntry (fileName))
                return juce::String (juce::CharPointer_UTF8 (e->name));

        return juce::File (fileName).getFileNameWithoutExtension();
    }

private:
    SampleData* loadLibrary (const juce::String& name)
    {
        auto* entry = findLibraryEntry (name);
        if (entry == nullptr)
            return nullptr;

        int size = 0;
        const char* data = BinaryData::getNamedResource (entry->resource, size);
        if (data == nullptr || size <= 0)
            return nullptr;

        std::unique_ptr<juce::AudioFormatReader> reader (
            formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false)));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        const auto length = (int) juce::jmin ((juce::int64) (maxSeconds * reader->sampleRate), reader->lengthInSamples);
        SampleData::Ptr sample = new SampleData();
        sample->name = name;
        sample->sampleRate = reader->sampleRate;
        sample->audio.setSize (1, length);
        reader->read (&sample->audio, 0, length, 0, true, false);
        sample->rootNote = entry->rootNote;
        sample->gated = entry->gated;
        sample->release = entry->release;

        loaded.add (sample);
        return sample.get();
    }

    /** Ricava un nome di file leggibile anche dagli URI di Android (es. "primary%3AMusic%2Fkick.wav"). */
    static juce::String cleanFileName (const juce::URL& url)
    {
       #if JUCE_ANDROID
        // Il nome "vero" del documento scelto (es. "Kick 01.wav"), non l'identificativo dell'URI
        if (auto doc = juce::AndroidDocument::fromDocument (url); doc.hasValue())
            if (auto docName = doc.getInfo().getName(); docName.isNotEmpty())
                return juce::File::createLegalFileName (docName.trim());
       #endif

        auto name = juce::URL::removeEscapeChars (url.getFileName());
        name = name.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf (":", false, false);
        return juce::File::createLegalFileName (name.trim());
    }

    juce::AudioFormatManager formats;
    juce::ReferenceCountedArray<SampleData> loaded;
};
