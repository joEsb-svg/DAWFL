#pragma once

//==============================================================================
/**
    La "ricetta" di un suono di batteria sintetizzato.
    Ogni suono e' la somma di quattro ingredienti, poi saturati:

    - TONO:   una sinusoide che scende di pitch (corpo di kick, tom, snare)
    - CLICK:  un colpetto di rumore brevissimo (l'attacco del battente)
    - RUMORE: rumore bianco filtrato (cordiera del rullante, clap, piatti)
    - METALLO: sei onde quadre "stonate" tra loro (il suono metallico degli hi-hat 808)

    Tempi in secondi (costante di decadimento), frequenze in Hz.
*/
struct DrumParams
{
    // Tono
    float toneLevel = 0.0f, toneStart = 200.0f, toneEnd = 50.0f, pitchTau = 0.03f, toneTau = 0.15f;
    // Click
    float clickLevel = 0.0f, clickTau = 0.003f;
    // Rumore e metallo (passano negli stessi filtri)
    float noiseLevel = 0.0f, metalLevel = 0.0f, metalPitch = 1.7f, noiseTau = 0.1f;
    float highPass = 0.0f;              // 0 = disattivo
    float bandPass = 0.0f, bandQ = 0.8f; // 0 = disattivo
    // Clap: numero di "battiti" iniziali, distanza e durata di ognuno
    int   bursts = 0;
    float burstSpacing = 0.01f, burstTau = 0.0045f;
    // Uscita
    float drive = 2.0f, gain = 1.0f;
};

//==============================================================================
namespace SoundLibrary
{
    static constexpr int numKits = 6;
    static constexpr int numInstruments = 6;   // stesso ordine dei canali del channel rack

    inline const char* getKitName (int kit)
    {
        static const char* names[numKits] = { "Classic", "Rock", "Drum & Bass", "Dubstep", "Techno", "Progressive" };
        return names[kit];
    }

    /** Ritorna il suono [strumento][kit]. Strumenti: Kick, Snare, Clap, Hi-Hat, Open Hat, Tom. */
    inline const DrumParams& get (int instrument, int kit)
    {
        auto tone  = [] (float lvl, float start, float end, float pTau, float tTau)
        {
            DrumParams p; p.toneLevel = lvl; p.toneStart = start; p.toneEnd = end; p.pitchTau = pTau; p.toneTau = tTau; return p;
        };

        auto withClick = [] (DrumParams p, float lvl, float tau)        { p.clickLevel = lvl; p.clickTau = tau; return p; };
        auto withNoise = [] (DrumParams p, float lvl, float tau, float hp, float bp = 0.0f, float q = 0.8f)
        {
            p.noiseLevel = lvl; p.noiseTau = tau; p.highPass = hp; p.bandPass = bp; p.bandQ = q; return p;
        };
        auto withMetal = [] (DrumParams p, float lvl, float pitch)      { p.metalLevel = lvl; p.metalPitch = pitch; return p; };
        auto withClap  = [] (DrumParams p, int n, float spacing, float tau) { p.bursts = n; p.burstSpacing = spacing; p.burstTau = tau; return p; };
        auto out       = [] (DrumParams p, float drive, float gain)     { p.drive = drive; p.gain = gain; return p; };

        static const DrumParams table[numInstruments][numKits] =
        {
            // ---------------------------------------------------------------- KICK
            {
                out (withClick (tone (1.0f, 200, 50, 0.032f, 0.17f), 0.5f, 0.003f), 4.0f, 0.90f),   // Classic (808-ish)
                out (withNoise (withClick (tone (1.0f, 140, 62, 0.020f, 0.11f), 0.9f, 0.004f),
                                0.15f, 0.03f, 2000), 2.2f, 1.00f),                                   // Rock: kick acustica, battente
                out (withClick (tone (1.0f, 260, 55, 0.015f, 0.10f), 0.7f, 0.003f), 5.0f, 0.90f),   // DnB: corta e secca
                out (withClick (tone (1.0f, 220, 42, 0.045f, 0.34f), 0.4f, 0.004f), 6.0f, 0.85f),   // Dubstep: enorme
                out (withClick (tone (1.0f, 165, 48, 0.050f, 0.27f), 0.4f, 0.002f), 7.0f, 0.82f),   // Techno: distorta, "rumble"
                out (withClick (tone (1.0f, 180, 52, 0.030f, 0.20f), 0.3f, 0.003f), 3.0f, 0.95f),   // Progressive: morbida e piena
            },
            // ---------------------------------------------------------------- SNARE
            {
                out (withNoise (tone (0.70f, 255, 185, 0.020f, 0.055f), 0.85f, 0.13f, 1800), 1.6f, 1.0f),
                out (withNoise (tone (0.80f, 220, 180, 0.020f, 0.080f), 1.00f, 0.20f, 1200, 4000, 0.5f), 2.2f, 1.0f),
                out (withNoise (tone (0.90f, 330, 230, 0.010f, 0.040f), 0.90f, 0.09f, 2500), 3.0f, 0.9f),
                out (withNoise (tone (1.00f, 260, 170, 0.020f, 0.090f), 1.10f, 0.28f, 900),  3.5f, 0.85f),
                out (withNoise (tone (0.50f, 300, 200, 0.010f, 0.030f), 0.90f, 0.07f, 3000), 1.8f, 1.0f),
                out (withNoise (tone (0.60f, 240, 190, 0.020f, 0.050f), 0.90f, 0.16f, 2000, 6000, 0.7f), 1.5f, 1.0f),
            },
            // ---------------------------------------------------------------- CLAP
            {
                out (withClap (withNoise ({}, 1.0f, 0.11f, 0, 1150, 1.4f), 3, 0.010f, 0.0045f), 3.2f, 1.0f),
                out (withClap (withNoise ({}, 1.0f, 0.18f, 0, 1500, 1.0f), 4, 0.012f, 0.0050f), 2.6f, 1.0f),
                out (withClap (withNoise ({}, 1.0f, 0.07f, 0, 1800, 1.2f), 3, 0.007f, 0.0040f), 3.5f, 1.0f),
                out (withClap (withNoise ({}, 1.0f, 0.30f, 0, 1000, 1.6f), 4, 0.014f, 0.0050f), 4.0f, 0.9f),
                out (withClap (withNoise ({}, 1.0f, 0.14f, 0, 1300, 2.0f), 3, 0.009f, 0.0040f), 3.5f, 1.0f),
                out (withClap (withNoise ({}, 1.0f, 0.22f, 0, 2200, 0.9f), 3, 0.011f, 0.0050f), 3.0f, 1.0f),
            },
            // ---------------------------------------------------------------- HI-HAT (chiuso)
            {
                out (withMetal (withNoise ({}, 0.35f, 0.045f, 7200, 10000, 0.9f), 0.7f, 1.7f), 2.2f, 1.0f),
                out (withMetal (withNoise ({}, 0.80f, 0.060f, 6000, 9000,  0.7f), 0.4f, 1.5f), 2.0f, 1.0f),
                out (withMetal (withNoise ({}, 0.50f, 0.030f, 8000, 12000, 0.9f), 0.6f, 1.9f), 2.4f, 1.0f),
                out (withMetal (withNoise ({}, 0.30f, 0.050f, 7000, 9000,  1.0f), 0.8f, 2.2f), 2.6f, 1.0f),
                out (withMetal (withNoise ({}, 0.20f, 0.035f, 7500, 10500, 1.0f), 1.0f, 1.5f), 2.4f, 1.0f),
                out (withMetal (withNoise ({}, 0.60f, 0.050f, 9000, 13000, 0.8f), 0.5f, 1.8f), 2.0f, 1.0f),
            },
            // ---------------------------------------------------------------- OPEN HAT
            {
                out (withMetal (withNoise ({}, 0.35f, 0.32f, 7200, 10000, 0.9f), 0.7f, 1.7f), 2.2f, 1.0f),
                out (withMetal (withNoise ({}, 0.90f, 0.50f, 5000, 8000,  0.6f), 0.4f, 1.4f), 2.0f, 1.0f),   // quasi un crash
                out (withMetal (withNoise ({}, 0.50f, 0.20f, 8000, 12000, 0.9f), 0.6f, 1.9f), 2.4f, 1.0f),
                out (withMetal (withNoise ({}, 0.30f, 0.40f, 7000, 9000,  1.0f), 0.8f, 2.2f), 2.6f, 1.0f),
                out (withMetal (withNoise ({}, 0.20f, 0.25f, 7500, 10500, 1.0f), 1.0f, 1.5f), 2.4f, 1.0f),
                out (withMetal (withNoise ({}, 0.60f, 0.45f, 9000, 13000, 0.8f), 0.5f, 1.8f), 2.0f, 1.0f),
            },
            // ---------------------------------------------------------------- TOM / PERC
            {
                out (withClick (tone (1.0f, 175, 105, 0.060f, 0.16f), 0.25f, 0.002f), 3.0f, 0.90f),
                out (withNoise (withClick (tone (1.0f, 160, 110, 0.040f, 0.25f), 0.4f, 0.003f), 0.1f, 0.05f, 1500), 2.0f, 1.0f),
                out (withClick (tone (1.0f, 220, 150, 0.030f, 0.12f), 0.4f, 0.002f), 3.0f, 0.90f),
                out (withClick (tone (1.0f, 140,  70, 0.080f, 0.30f), 0.3f, 0.003f), 5.0f, 0.85f),
                out (withNoise (withClick (tone (0.8f, 420, 380, 0.010f, 0.05f), 0.6f, 0.002f), 0.3f, 0.03f, 3000), 2.0f, 1.0f), // rimshot
                out (withClick (tone (1.0f, 200, 130, 0.050f, 0.20f), 0.25f, 0.002f), 2.0f, 1.0f),
            },
        };

        return table[instrument][kit];
    }
}
