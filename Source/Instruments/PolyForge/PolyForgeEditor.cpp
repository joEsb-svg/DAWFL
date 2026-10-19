#include "PolyForgeEditor.h"

PolyForgeEditor::PolyForgeEditor (PolyForgeAudioProcessor& p)
    : ForgeEditorBase (p, p.apvts, "POLY", "FORGE", juce::Colour (0xffb07cff), { "OSC", "FILTRO", "ENV", "LFO", "FX" })
{
    using namespace pf::ids;
    namespace col = bf::ui::col;

    // --- OSC
    auto& s1 = addSection (0, "Oscillatore 1", 1.2f);
    combo (s1, osc1Wave, "Onda");
    knob (s1, osc1Lvl, "Livello");
    knob (s1, osc1Uni, "Unison");
    knob (s1, osc1Det, "Detune");
    knob (s1, osc1Pw, "PW (quadra)");

    auto& s2 = addSection (0, "Oscillatore 2", 1.5f);
    combo (s2, osc2Wave, "Onda");
    knob (s2, osc2Lvl, "Livello");
    knob (s2, osc2Semi, "Semitoni");
    knob (s2, osc2Fine, "Fine");
    knob (s2, osc2Uni, "Unison");
    knob (s2, osc2Det, "Detune");

    auto& s3 = addSection (0, "Sub e rumore", 0.75f);
    knob (s3, subLvl, "Sub");
    knob (s3, noiseLvl, "Rumore");
    knob (s3, spread, "Stereo");

    // --- FILTRO
    auto& f = addSection (1, "Filtro", 1.2f);
    combo (f, fltType, "Tipo");
    knob (f, cutoff, "Cutoff");
    knob (f, reso, "Risonanza");
    knob (f, fltDrive, "Drive");
    knob (f, keyTrack, "Keytrack");
    knob (f, fltVel, "Velocity");

    auto& e = addSection (1, "Inviluppo del filtro", 1.3f);
    knob (e, fltEnv, "Quantita'");
    knob (e, fltA, "Attacco");
    knob (e, fltD, "Decay");
    knob (e, fltS, "Sustain");
    knob (e, fltR, "Rilascio");
    adsr (e, fltA, fltD, fltS, fltR, col::orange);

    // --- ENV
    auto& a = addSection (2, "Volume (ADSR)", 1.6f);
    knob (a, ampA, "Attacco");
    knob (a, ampD, "Decay");
    knob (a, ampS, "Sustain");
    knob (a, ampR, "Rilascio");
    knob (a, velSens, "Velocity");
    adsr (a, ampA, ampD, ampS, ampR, col::cyan);

    auto& v = addSection (2, "Voce", 0.6f);
    combo (v, mode, "Modo");
    knob (v, glide, "Glide");

    // --- LFO
    auto& l = addSection (3, "LFO", 1.0f);
    combo (l, lfoWave, "Forma");
    knob (l, lfoRate, "Velocita'");
    toggle (l, lfoSync, "A tempo");
    combo (l, lfoDiv, "Divisione");

    auto& d = addSection (3, "Cosa muove", 1.0f);
    knob (d, lfoPitch, "Vibrato");
    knob (d, lfoCut, "Filtro");
    knob (d, lfoAmp, "Volume");
    knob (d, lfoPwm, "PW");

    // --- FX
    auto& c = addSection (4, "Chorus", 0.5f);
    knob (c, chorus, "Mix");
    auto& dl = addSection (4, "Delay", 1.0f);
    knob (dl, dlyMix, "Mix");
    combo (dl, dlyTime, "Tempo");
    knob (dl, dlyFb, "Feedback");
    auto& r = addSection (4, "Riverbero", 0.8f);
    knob (r, revMix, "Mix");
    knob (r, revSize, "Stanza");
    auto& o = addSection (4, "Uscita", 0.5f);
    knob (o, master, "Volume");

    showTab (0);
}
