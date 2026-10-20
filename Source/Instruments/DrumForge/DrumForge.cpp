#include "../../Engine/Performance.h"
#include "DrumForge.h"
#include "DrumForgeEditor.h"

namespace df
{
    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
        auto db  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; };
        auto st  = [] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " st"; };
        auto hz  = [] (float v, int) { return v >= 19500.0f ? String ("Aperto")
                                            : v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                           : String (roundToInt (v)) + " Hz"; };
        auto pan = [] (float v, int) { const int x = roundToInt (v * 100.0f);
                                       return x == 0 ? String ("Centro") : x < 0 ? "S " + String (-x) : "D " + String (x); };

        auto flt = [&p] (const String& id, const String& name, NormalisableRange<float> r, float def,
                         std::function<String (float, int)> text)
        {
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                                AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
        };

        for (int i = 0; i < numPads; ++i)
        {
            const auto n = "Pad " + String (i + 1) + " ";
            NormalisableRange<float> cut (60.0f, 20000.0f);
            cut.setSkewForCentre (1500.0f);
            NormalisableRange<float> tune (-24.0f, 24.0f, 0.1f);

            flt (padId (i, "Tune"),   n + "Intonazione", tune, 0.0f, st);
            flt (padId (i, "Decay"),  n + "Decadimento", { 0.03f, 1.0f }, 1.0f, pct);
            flt (padId (i, "Level"),  n + "Volume", { -30.0f, 6.0f }, 0.0f, db);
            flt (padId (i, "Pan"),    n + "Panorama", { -1.0f, 1.0f }, 0.0f, pan);
            flt (padId (i, "Cutoff"), n + "Filtro", cut, 20000.0f, hz);
            flt (padId (i, "Reverb"), n + "Riverbero", { 0.0f, 1.0f }, 0.0f, pct);
            flt (padId (i, "Vel"),    n + "Velocity", { 0.0f, 1.0f }, 0.6f, pct);
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { padId (i, "Choke"), 1 }, n + "Choke",
                                                                 StringArray { "Nessuno", "Gruppo 1", "Gruppo 2", "Gruppo 3", "Gruppo 4" }, 0));
        }

        flt (ids::master,  "Volume", { -24.0f, 6.0f }, 0.0f, db);
        flt (ids::drive,   "Saturazione", { 0.0f, 1.0f }, 0.0f, pct);
        flt (ids::revSize, "Stanza", { 0.0f, 1.0f }, 0.5f, pct);
        flt (ids::revMix,  "Riverbero", { 0.0f, 1.0f }, 0.7f, pct);

        return { p.begin(), p.end() };
    }

    //==========================================================================
    // Kit di fabbrica. Disposizione comune dei pad (come le drum machine classiche):
    //  1 Cassa   2 Cassa 2   3 Rullante   4 Clap
    //  5 Rim     6 Hat chiuso 7 Hat 2     8 Hat aperto   (6-7-8 nello stesso gruppo choke)
    //  9 Tom basso 10 Tom medio 11 Tom alto 12 Crash
    // 13 Ride    14..16 percussioni ed effetti
    // Campi: { campione, nome, intonazione, decadimento, volume dB, pan, filtro Hz, choke, riverbero }

    const std::vector<KitDef>& factoryKits()
    {
        static const std::vector<KitDef> kits =
        {
            { "Acustico", {{
                { "ac_kick", "Cassa" }, { "ac_kick_tight", "Cassa secca" }, { "ac_snare", "Rullante", 0, 1, 0, 0, 20000, 0, 0.15f },
                { "hand_clap", "Mani", 0, 1, -3, 0, 20000, 0, 0.2f },
                { "ac_sidestick", "Side stick", 0, 1, -2 }, { "ac_hat_closed", "Hi-hat chiuso", 0, 1, -3, 0.15f, 20000, 1 },
                { "ac_hat_pedal", "Hi-hat pedale", 0, 1, -4, 0.15f, 20000, 1 }, { "ac_hat_open", "Hi-hat aperto", 0, 1, -4, 0.15f, 20000, 1 },
                { "ac_tom_floor", "Tom basso", 0, 1, 0, 0.3f }, { "ac_tom_mid", "Tom medio", 0, 1, 0, 0.0f }, { "ac_tom_high", "Tom alto", 0, 1, 0, -0.3f },
                { "ac_crash", "Crash", 0, 1, -4, -0.35f }, { "ac_ride", "Ride", 0, 1, -4, 0.35f }, { "ac_ride_bell", "Campana ride", 0, 1, -5, 0.35f },
                { "ac_splash", "Splash", 0, 1, -5, -0.2f }, { "ac_china", "China", 0, 1, -6, 0.2f } }} },

            { "Acustico secco", {{
                { "ac_kick_tight", "Cassa" }, { "ac_kick", "Cassa corta", 0, 0.35f }, { "ac_snare_rim", "Rullante", 0, 0.5f, -1 },
                { "ac_brush", "Spazzola", 0, 1, -2 },
                { "ac_sidestick", "Side stick", 0, 0.6f, -2 }, { "ac_hat_closed", "Hi-hat chiuso", 0, 0.5f, -3, 0.15f, 20000, 1 },
                { "ac_hat_pedal", "Hi-hat pedale", 0, 0.6f, -4, 0.15f, 20000, 1 }, { "ac_hat_open", "Hi-hat aperto", 0, 0.45f, -4, 0.15f, 20000, 1 },
                { "ac_tom_floor", "Tom basso", 0, 0.45f, 0, 0.3f }, { "ac_tom_mid", "Tom medio", 0, 0.45f }, { "ac_tom_high", "Tom alto", 0, 0.45f, 0, -0.3f },
                { "ac_splash", "Splash", 0, 0.6f, -5, -0.3f }, { "ac_ride", "Ride", 0, 0.6f, -5, 0.3f }, { "tambourine", "Tamburello", 0, 1, -5, 0.25f },
                { "shaker", "Shaker", 0, 1, -5, -0.25f }, { "cowbell", "Campanaccio", 0, 1, -6 } }} },

            { "808", {{
                { "d808_kick", "Kick 808" }, { "d808_kick", "Kick corto", 0, 0.35f }, { "d808_snare", "Snare 808" }, { "d808_clap", "Clap 808" },
                { "d808_rim", "Rimshot" }, { "d808_ch", "Hi-hat chiuso", 0, 1, -2, 0, 20000, 1 }, { "d808_maracas", "Maracas", 0, 1, -3, 0.2f },
                { "d808_oh", "Hi-hat aperto", 0, 1, -2, 0, 20000, 1 },
                { "d808_tom_lo", "Tom basso", 0, 1, 0, 0.3f }, { "d808_tom_mid", "Tom medio" }, { "d808_tom_hi", "Tom alto", 0, 1, 0, -0.3f },
                { "d808_cymbal", "Piatto", 0, 1, -4 }, { "d808_cowbell", "Cowbell", 0, 1, -5 }, { "d808_clave", "Clave", 0, 1, -5, 0.25f },
                { "d808_conga_lo", "Conga bassa", 0, 1, -2, -0.3f }, { "d808_conga_hi", "Conga alta", 0, 1, -2, 0.3f } }} },

            { "909", {{
                { "d909_kick", "Kick 909" }, { "house_kick", "Kick 2" }, { "d909_snare", "Snare 909" }, { "d909_clap", "Clap 909" },
                { "d909_rim", "Rimshot" }, { "d909_ch", "Hi-hat chiuso", 0, 1, -2, 0, 20000, 1 }, { "d909_ch", "Hi-hat corto", 2, 0.4f, -3, 0, 20000, 1 },
                { "d909_oh", "Hi-hat aperto", 0, 1, -2, 0, 20000, 1 },
                { "d909_tom_lo", "Tom basso", 0, 1, 0, 0.3f }, { "d909_tom_mid", "Tom medio" }, { "d909_tom_hi", "Tom alto", 0, 1, 0, -0.3f },
                { "d909_crash", "Crash", 0, 1, -4 }, { "d909_ride", "Ride", 0, 1, -4 }, { "techno_perc", "Percussione", 0, 1, -5 },
                { "electro_cowbell", "Cowbell", 0, 1, -6 }, { "techno_noise", "Noise", 0, 1, -6 } }} },

            { "House", {{
                { "house_kick", "Kick" }, { "d909_kick", "Kick 909" }, { "edm_snare", "Snare", 0, 1, -2 }, { "house_clap", "Clap", 0, 1, 0, 0, 20000, 0, 0.2f },
                { "house_rim", "Rimshot", 0, 1, -2 }, { "d909_ch", "Hi-hat chiuso", 0, 1, -3, 0.1f, 20000, 1 }, { "house_shaker", "Shaker", 0, 1, -3, -0.2f },
                { "house_hat_open", "Hi-hat aperto", 0, 1, -3, 0.1f, 20000, 1 },
                { "d909_tom_lo", "Tom basso", 0, 1, 0, 0.3f }, { "d909_tom_mid", "Tom medio" }, { "d909_tom_hi", "Tom alto", 0, 1, 0, -0.3f },
                { "d909_crash", "Crash", 0, 1, -4 }, { "d909_ride", "Ride", 0, 1, -5 }, { "tambourine", "Tamburello", 0, 1, -5, 0.3f },
                { "conga_high", "Conga", 0, 1, -3, -0.3f }, { "house_stab", "Stab", 0, 1, -4, 0, 20000, 0, 0.3f } }} },

            { "Techno", {{
                { "techno_kick", "Kick" }, { "techno_rumble", "Rumble" }, { "d909_snare", "Snare", 0, 0.6f, -2 }, { "techno_clap", "Clap", 0, 1, 0, 0, 20000, 0, 0.3f },
                { "d909_rim", "Rimshot", 0, 1, -3 }, { "techno_hat", "Hi-hat", 0, 1, -3, 0, 20000, 1 }, { "d909_ch", "Hi-hat 909", 0, 0.5f, -3, 0, 20000, 1 },
                { "d909_oh", "Hi-hat aperto", 0, 1, -3, 0, 20000, 1 },
                { "d909_tom_lo", "Tom basso", -2, 1, 0, 0.3f }, { "d909_tom_mid", "Tom medio", -2 }, { "d909_tom_hi", "Tom alto", -2, 1, 0, -0.3f },
                { "d909_crash", "Crash", 0, 1, -5 }, { "techno_ride", "Ride", 0, 1, -4 }, { "techno_perc", "Metallo", 0, 1, -4, 0.3f, 20000, 0, 0.3f },
                { "techno_noise", "Noise", 0, 1, -5 }, { "techno_dubstab", "Stab dub", 0, 1, -5, 0, 20000, 0, 0.4f } }} },

            { "Trap", {{
                { "trap_kick", "Kick" }, { "trap_808", "808" }, { "trap_snare", "Snare" }, { "trap_clap", "Clap", 0, 1, 0, 0, 20000, 0, 0.15f },
                { "trap_rim", "Rim", 0, 1, -2 }, { "trap_hat", "Hi-hat", 0, 1, -3, 0, 20000, 1 }, { "trap_hat", "Hi-hat alto", 4, 0.6f, -4, 0, 20000, 1 },
                { "trap_hat_open", "Hi-hat aperto", 0, 1, -3, 0, 20000, 1 },
                { "d808_tom_lo", "Tom basso", 0, 1, 0, 0.3f }, { "d808_tom_mid", "Tom medio" }, { "d808_tom_hi", "Tom alto", 0, 1, 0, -0.3f },
                { "d808_cymbal", "Piatto", 0, 1, -5 }, { "trap_bell", "Campana", 0, 1, -6, 0, 20000, 0, 0.3f }, { "d808_cowbell", "Cowbell", 0, 1, -6 },
                { "finger_snap", "Schiocco", 0, 1, -3, 0, 20000, 0, 0.25f }, { "fx_impact", "Impatto", 0, 1, -6 } }} },

            { "Drum and Bass", {{
                { "dnb_kick", "Kick" }, { "d909_kick", "Kick 2", 2 }, { "dnb_snare", "Snare", 0, 1, 0, 0, 20000, 0, 0.15f }, { "edm_clap", "Clap", 0, 1, -2 },
                { "ac_sidestick", "Side stick", 0, 1, -3 }, { "dnb_hat", "Hi-hat", 0, 1, -3, 0.15f, 20000, 1 }, { "ac_hat_pedal", "Hi-hat pedale", 0, 1, -4, 0.15f, 20000, 1 },
                { "ac_hat_open", "Hi-hat aperto", 0, 0.7f, -4, 0.15f, 20000, 1 },
                { "ac_tom_floor", "Tom basso", 0, 0.7f, 0, 0.3f }, { "ac_tom_mid", "Tom medio", 0, 0.7f }, { "ac_tom_high", "Tom alto", 0, 0.7f, 0, -0.3f },
                { "ac_crash", "Crash", 0, 1, -5 }, { "dnb_ride", "Ride", 0, 1, -4 }, { "shaker", "Shaker", 0, 1, -5, 0.3f },
                { "dnb_neuro", "Neuro", 0, 1, -5 }, { "fx_zap", "Zap", 0, 1, -6 } }} },

            { "Dubstep", {{
                { "dub_kick", "Kick" }, { "edm_kick", "Kick 2" }, { "dub_snare", "Snare", 0, 1, 0, 0, 20000, 0, 0.25f }, { "trap_clap", "Clap", 0, 1, -2, 0, 20000, 0, 0.25f },
                { "trap_rim", "Rim", 0, 1, -3 }, { "edm_hat", "Hi-hat", 0, 1, -3, 0, 20000, 1 }, { "trap_hat", "Hi-hat 2", 0, 1, -4, 0, 20000, 1 },
                { "trap_hat_open", "Hi-hat aperto", 0, 1, -4, 0, 20000, 1 },
                { "d909_tom_lo", "Tom basso", -3, 1, 0, 0.3f }, { "d909_tom_mid", "Tom medio", -3 }, { "d909_tom_hi", "Tom alto", -3, 1, 0, -0.3f },
                { "d909_crash", "Crash", 0, 1, -4 }, { "techno_ride", "Ride", 0, 1, -5 }, { "dub_laser", "Laser", 0, 1, -5 },
                { "dub_impact", "Impatto", 0, 1, -4 }, { "fx_reverse_cymbal", "Piatto rev.", 0, 1, -6 } }} },

            { "EDM", {{
                { "edm_kick", "Kick" }, { "house_kick", "Kick 2" }, { "edm_snare", "Snare", 0, 1, 0, 0, 20000, 0, 0.2f }, { "edm_clap", "Clap", 0, 1, 0, 0, 20000, 0, 0.25f },
                { "house_rim", "Rim", 0, 1, -3 }, { "edm_hat", "Hi-hat", 0, 1, -3, 0, 20000, 1 }, { "house_shaker", "Shaker", 0, 1, -4, 0, 20000, 1 },
                { "house_hat_open", "Hi-hat aperto", 0, 1, -3, 0, 20000, 1 },
                { "d909_tom_lo", "Tom basso", 0, 1, 0, 0.3f }, { "d909_tom_mid", "Tom medio" }, { "d909_tom_hi", "Tom alto", 0, 1, 0, -0.3f },
                { "d909_crash", "Crash", 0, 1, -3 }, { "d909_ride", "Ride", 0, 1, -5 }, { "edm_impact", "Impatto", 0, 1, -3 },
                { "fx_uplifter", "Uplifter", 0, 1, -6 }, { "fx_downlifter", "Downlifter", 0, 1, -6 } }} },

            { "Lo-Fi", {{
                { "lofi_kick", "Kick" }, { "ac_kick_tight", "Kick 2", -2, 0.6f, 0, 0, 2500 }, { "lofi_snare", "Snare", 0, 1, 0, 0, 20000, 0, 0.15f },
                { "hand_clap", "Mani", 0, 1, -3, 0, 4000, 0, 0.2f },
                { "lofi_rim", "Rim", 0, 1, -2 }, { "lofi_hat", "Hi-hat", 0, 1, -3, 0.15f, 20000, 1 }, { "lofi_shaker", "Shaker", 0, 1, -3, -0.15f, 20000, 1 },
                { "ac_hat_open", "Hi-hat aperto", -1, 0.5f, -5, 0.15f, 5000, 1 },
                { "ac_tom_floor", "Tom basso", -2, 0.6f, -1, 0.3f, 3000 }, { "ac_tom_mid", "Tom medio", -2, 0.6f, -1, 0.0f, 3000 },
                { "ac_tom_high", "Tom alto", -2, 0.6f, -1, -0.3f, 3000 },
                { "ac_ride", "Ride", -1, 0.7f, -6, 0.3f, 6000 }, { "ac_brush", "Spazzola", 0, 1, -3, 0, 6000 }, { "finger_snap", "Schiocco", 0, 1, -4, 0, 6000, 0, 0.3f },
                { "woodblock", "Woodblock", -3, 1, -5, -0.3f, 5000 }, { "fx_vinyl", "Vinile", 0, 1, -8 } }} },

            { "Percussioni", {{
                { "conga_low", "Conga bassa", 0, 1, 0, -0.3f }, { "conga_high", "Conga alta", 0, 1, 0, -0.1f }, { "conga_slap", "Conga slap", 0, 1, 0, -0.2f },
                { "bongo", "Bongo", 0, 1, 0, 0.25f },
                { "djembe", "Djembe", 0, 1, 0, 0.1f }, { "shaker", "Shaker", 0, 1, -3, 0.3f }, { "tambourine", "Tamburello", 0, 1, -3, -0.3f },
                { "cabasa", "Cabasa", 0, 1, -3, 0.2f },
                { "guiro", "Guiro", 0, 1, -3, -0.2f }, { "claves", "Claves", 0, 1, -4, 0.35f }, { "woodblock", "Woodblock", 0, 1, -4, -0.35f },
                { "cowbell", "Campanaccio", 0, 1, -5 }, { "triangle", "Triangolo", 0, 1, -6, 0.3f },
                { "finger_snap", "Schiocco", 0, 1, -3 }, { "hand_clap", "Mani", 0, 1, -3 }, { "d808_conga_hi", "Conga 808", 0, 1, -3 } }} },
        };
        return kits;
    }

    //==========================================================================
    int Presets::numFactory()                       { return (int) factoryKits().size(); }
    juce::String Presets::factoryName (int i)       { return factoryKits()[(size_t) juce::jlimit (0, numFactory() - 1, i)].name; }

    juce::String Presets::currentName() const
    {
        return processor.apvts.state.getProperty ("presetName", "Kit").toString();
    }

    void Presets::loadFactory (int i)
    {
        auto& apvts = processor.apvts;
        const auto& kit = factoryKits()[(size_t) juce::jlimit (0, numFactory() - 1, i)];

        auto set = [&apvts] (const juce::String& id, float value)
        {
            if (auto* prm = apvts.getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        };

        for (auto* prm : processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                r->setValueNotifyingHost (r->getDefaultValue());

        for (int p = 0; p < numPads; ++p)
        {
            const auto& pad = kit.pads[(size_t) p];
            set (padId (p, "Tune"), pad.tune);
            set (padId (p, "Decay"), pad.decay);
            set (padId (p, "Level"), pad.level);
            set (padId (p, "Pan"), pad.pan);
            set (padId (p, "Cutoff"), pad.cutoff);
            set (padId (p, "Choke"), (float) pad.choke);
            set (padId (p, "Reverb"), pad.reverb);
            apvts.state.setProperty ("sample" + juce::String (p), SampleStore::libraryName (pad.sample), nullptr);
            apvts.state.setProperty ("name" + juce::String (p), juce::String (juce::CharPointer_UTF8 (pad.label)), nullptr);
        }

        apvts.state.setProperty ("presetName", kit.name, nullptr);
        processor.refreshSamples();
    }

    juce::File Presets::userDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DrumForge").getChildFile ("Kits");
    }

    juce::Array<juce::File> Presets::userPresets() const
    {
        auto files = userDir().findChildFiles (juce::File::findFiles, false, "*.dfkit");
        files.sort();
        return files;
    }

    bool Presets::save (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
            return false;
        userDir().createDirectory();
        processor.apvts.state.setProperty ("presetName", clean, nullptr);
        if (auto xml = processor.apvts.copyState().createXml())
            return xml->writeTo (userDir().getChildFile (clean + ".dfkit"));
        return false;
    }

    bool Presets::load (const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName (processor.apvts.state.getType()))
            return false;
        processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        processor.apvts.state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
        processor.refreshSamples();
        return true;
    }

    //==========================================================================
    // Motore

    void Engine::prepare (double sampleRate)
    {
        sr = sampleRate;
        chokeCoef = (float) std::exp (-1.0 / (0.006 * sr));    // ~6 ms per zittire un pad
        stopAll();
    }

    void Engine::stopAll()
    {
        for (auto& v : voices)
            v.sample = nullptr;
    }

    int Engine::activeVoices() const
    {
        int n = 0;
        for (auto& v : voices)
            if (v.sample != nullptr) ++n;
        return n;
    }

    void Engine::trigger (int pad, float velocity, const PadParams& p)
    {
        if (p.sample == nullptr || p.sample->audio.getNumSamples() < 2)
            return;

        // Choke: lo stesso gruppo si zittisce (hi-hat chiuso / aperto)
        if (p.choke > 0)
            for (auto& v : voices)
                if (v.sample != nullptr && v.choke == p.choke)
                    v.choking = true;

        // Voce libera, o la piu' vecchia (in "Risparmio" meta' delle voci)
        const size_t limit = perf::lowPower() ? voices.size() / 2 : voices.size();
        Voice* voice = nullptr;
        for (size_t i = 0; i < limit; ++i)
            if (voices[i].sample == nullptr) { voice = &voices[i]; break; }
        if (voice == nullptr)
        {
            voice = &voices[0];
            for (size_t i = 0; i < limit; ++i)
                if (voices[i].age < voice->age) voice = &voices[i];
        }

        auto& v = *voice;
        v = Voice();
        v.sample = p.sample;
        v.pad = pad;
        v.choke = p.choke;
        v.age = ++ageCounter;
        v.inc = p.sample->sampleRate / sr * (double) p.rate;

        const float vel = 1.0f - p.velSens * (1.0f - juce::jlimit (0.0f, 1.0f, velocity));
        v.gainL = p.gainL * vel;
        v.gainR = p.gainR * vel;
        v.send = p.send * vel;

        // Decadimento: 100% = il campione intero; meno = sfuma sempre prima
        if (p.decay < 0.995f)
        {
            const double length = (double) p.sample->audio.getNumSamples() / (p.sample->sampleRate * (double) p.rate);
            const double tau = 0.004 + length * 0.4 * std::pow ((double) p.decay, 1.5) / (1.0 - (double) p.decay);
            v.envCoef = (float) std::exp (-1.0 / (tau * sr));
        }

        if (p.cutoff < 19500.0f)
        {
            v.filtered = true;
            v.g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) p.cutoff, sr * 0.45) / sr);
            v.k = 1.2f;
            v.a1 = 1.0f / (1.0f + v.g * (v.g + v.k));
            v.a2 = v.g * v.a1;
            v.a3 = v.g * v.a2;
        }
    }

    void Engine::render (float* L, float* R, float* send, int n)
    {
        for (auto& v : voices)
        {
            if (v.sample == nullptr)
                continue;

            const float* data = v.sample->audio.getReadPointer (0);
            const int last = v.sample->audio.getNumSamples() - 1;

            for (int i = 0; i < n; ++i)
            {
                const int i0 = (int) v.pos;
                if (i0 >= last)
                {
                    v.sample = nullptr;
                    break;
                }

                const float fr = (float) (v.pos - (double) i0);
                float s = data[i0] + fr * (data[i0 + 1] - data[i0]);
                v.pos += v.inc;

                if (v.filtered)
                {
                    const float v3 = s - v.s2;
                    const float v1 = v.a1 * v.s1 + v.a2 * v3;
                    const float v2 = v.s2 + v.a2 * v.s1 + v.a3 * v3;
                    v.s1 = 2.0f * v1 - v.s1;
                    v.s2 = 2.0f * v2 - v.s2;
                    s = v2;
                }

                v.env *= v.envCoef;
                if (v.choking)
                    v.chokeGain *= chokeCoef;
                const float a = s * v.env * v.chokeGain;

                L[i] += a * v.gainL;
                R[i] += a * v.gainR;
                send[i] += a * v.send;

                if (v.env * v.chokeGain < 0.0001f)
                {
                    v.sample = nullptr;
                    break;
                }
            }
        }
    }
}

//==============================================================================
DrumForgeAudioProcessor::DrumForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DrumForge", df::createLayout())
{
    for (int p = 0; p < df::numPads; ++p)
    {
        auto get = [this, p] (const char* what) { return apvts.getRawParameterValue (df::padId (p, what)); };
        raw[(size_t) p] = { get ("Tune"), get ("Decay"), get ("Level"), get ("Pan"), get ("Cutoff"), get ("Choke"), get ("Vel"), get ("Reverb") };
        padSamples[(size_t) p].store (nullptr);
        padActivity[(size_t) p].store (0.0f);
        pendingVelocity[(size_t) p].store (0.9f);
    }
    masterParam   = apvts.getRawParameterValue (df::ids::master);
    driveParam    = apvts.getRawParameterValue (df::ids::drive);
    revSizeParam  = apvts.getRawParameterValue (df::ids::revSize);
    revLevelParam = apvts.getRawParameterValue (df::ids::revMix);

    presets.loadFactory (0);
}

bool DrumForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void DrumForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    engine.prepare (sampleRate);
    sendBuffer.setSize (1, juce::jmax (256, samplesPerBlock), false, true, false);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
}

df::Engine::PadParams DrumForgeAudioProcessor::padParams (int pad) const
{
    const auto& r = raw[(size_t) pad];
    df::Engine::PadParams p;
    p.sample = padSamples[(size_t) pad].load();
    p.rate = std::exp2 (r.tune->load() / 12.0f);
    p.decay = r.decay->load();
    const float g = juce::Decibels::decibelsToGain (r.level->load());
    const float pan = r.pan->load();
    p.gainL = g * juce::jmin (1.0f, 1.0f - pan);
    p.gainR = g * juce::jmin (1.0f, 1.0f + pan);
    p.cutoff = r.cutoff->load();
    p.choke = (int) r.choke->load();
    p.velSens = r.vel->load();
    p.send = r.reverb->load();
    return p;
}

void DrumForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    if (sendBuffer.getNumSamples() < numSamples)
        return;   // non succede: prepareToPlay riceve la dimensione massima

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float* send = sendBuffer.getWritePointer (0);
    juce::FloatVectorOperations::clear (send, numSamples);

    // Pad toccati sull'interfaccia
    if (const auto hits = pendingHits.exchange (0); hits != 0)
        for (int p = 0; p < df::numPads; ++p)
            if ((hits >> p) & 1u)
            {
                engine.trigger (p, pendingVelocity[(size_t) p].load(), padParams (p));
                padActivity[(size_t) p].store (1.0f);
            }

    // Mono: le voci scrivono a destra in un buffer temporaneo che poi si somma
    float monoRight[512];
    int done = 0;
    auto renderTo = [&] (int end)
    {
        while (done < end)
        {
            const int n = juce::jmin (end - done, 512);
            if (R != nullptr)
                engine.render (L + done, R + done, send + done, n);
            else
            {
                juce::FloatVectorOperations::clear (monoRight, n);
                engine.render (L + done, monoRight, send + done, n);
                juce::FloatVectorOperations::add (L + done, monoRight, n);
                juce::FloatVectorOperations::multiply (L + done, 0.5f, n);
            }
            done += n;
        }
    };

    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (0, numSamples, meta.samplePosition));
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int pad = msg.getNoteNumber() - df::firstNote;
            if (pad >= 0 && pad < df::numPads)
            {
                engine.trigger (pad, msg.getFloatVelocity(), padParams (pad));
                padActivity[(size_t) pad].store (juce::jmax (0.3f, msg.getFloatVelocity()));
            }
        }
        else if (msg.isAllSoundOff())
            engine.stopAll();
        // I colpi di batteria non si fermano al note-off: suonano fino alla fine (o al choke)
    }
    renderTo (numSamples);

    // Riverbero sulla mandata
    const float level = revLevelParam->load();
    if (level > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.65f * revSizeParam->load();
        rp.damping = 0.5f;
        rp.width = 1.0f;
        rp.wetLevel = 0.5f * level;
        rp.dryLevel = 0.0f;
        if (rp.roomSize != lastRev.roomSize || rp.wetLevel != lastRev.wetLevel)
        {
            reverb.setParameters (rp);
            lastRev = rp;
        }

        // processStereo lavora sul posto: copia la mandata nei due canali temporanei
        float wetL[512], wetR[512];
        for (int start = 0; start < numSamples; start += 512)
        {
            const int n = juce::jmin (512, numSamples - start);
            juce::FloatVectorOperations::copy (wetL, send + start, n);
            juce::FloatVectorOperations::copy (wetR, send + start, n);
            reverb.processStereo (wetL, wetR, n);
            juce::FloatVectorOperations::add (L + start, wetL, n);
            if (R != nullptr)
                juce::FloatVectorOperations::add (R + start, wetR, n);
        }
    }

    // Saturazione e volume
    const float drive = driveParam->load();
    // Margine: piu' colpi insieme (cassa + hat + piatto) non devono saturare il mix
    const float master = 0.55f * juce::Decibels::decibelsToGain (masterParam->load());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        if (drive > 0.001f)
        {
            const float pre = 1.0f + drive * 5.0f;
            const float post = 1.0f / std::tanh (pre * 0.7f) * 0.7f;
            for (int i = 0; i < numSamples; ++i)
                d[i] = std::tanh (d[i] * pre) * post;
        }
        juce::FloatVectorOperations::multiply (d, master, numSamples);
    }
}

//==============================================================================
bool DrumForgeAudioProcessor::setPadSample (int pad, const juce::String& sampleName, const juce::String& label)
{
    if (pad < 0 || pad >= df::numPads)
        return false;

    auto* data = store.load (sampleName);
    if (data == nullptr && sampleName.isNotEmpty())
        return false;

    apvts.state.setProperty ("sample" + juce::String (pad), sampleName, nullptr);
    apvts.state.setProperty ("name" + juce::String (pad), label.isNotEmpty() ? label : SampleStore::displayName (sampleName), nullptr);
    padSamples[(size_t) pad].store (data);
    return true;
}

juce::String DrumForgeAudioProcessor::getPadSample (int pad) const
{
    return apvts.state.getProperty ("sample" + juce::String (pad)).toString();
}

juce::String DrumForgeAudioProcessor::getPadName (int pad) const
{
    auto name = apvts.state.getProperty ("name" + juce::String (pad)).toString();
    if (name.isEmpty())
        name = SampleStore::displayName (getPadSample (pad));
    return name.isNotEmpty() ? name : "Pad " + juce::String (pad + 1);
}

void DrumForgeAudioProcessor::setPadName (int pad, const juce::String& name)
{
    apvts.state.setProperty ("name" + juce::String (pad), name, nullptr);
}

void DrumForgeAudioProcessor::hitPad (int pad, float velocity)
{
    if (pad < 0 || pad >= df::numPads)
        return;
    if (onPadHit && onPadHit (pad, velocity))
        return;
    pendingVelocity[(size_t) pad].store (velocity);
    pendingHits.fetch_or (1u << pad);
}

float DrumForgeAudioProcessor::getPadActivity (int pad) const
{
    return padActivity[(size_t) juce::jlimit (0, df::numPads - 1, pad)].load();
}

void DrumForgeAudioProcessor::decayPadActivity (float factor)
{
    for (auto& a : padActivity)
        a.store (a.load() * factor);
}

void DrumForgeAudioProcessor::refreshSamples()
{
    for (int p = 0; p < df::numPads; ++p)
        padSamples[(size_t) p].store (store.load (getPadSample (p)));
}

juce::AudioProcessorEditor* DrumForgeAudioProcessor::createEditor()
{
    return new DrumForgeEditor (*this);
}

void DrumForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            refreshSamples();
        }
}
