#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

//==============================================================================
/**
    Un po' di teoria musicale per aiutare chi compone: scale, accordi costruiti sulla scala,
    progressioni di accordi famose. Usata dal piano roll.
*/
namespace theory
{
    struct Scale
    {
        const char* name;
        std::vector<int> intervals;   // semitoni dalla tonica
    };

    inline const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> list =
        {
            { "Nessuna (tutte le note)", {} },
            { "Maggiore",             { 0, 2, 4, 5, 7, 9, 11 } },
            { "Minore",               { 0, 2, 3, 5, 7, 8, 10 } },
            { "Minore armonica",      { 0, 2, 3, 5, 7, 8, 11 } },
            { "Dorica",               { 0, 2, 3, 5, 7, 9, 10 } },
            { "Frigia",               { 0, 1, 3, 5, 7, 8, 10 } },
            { "Misolidia",            { 0, 2, 4, 5, 7, 9, 10 } },
            { "Pentatonica maggiore", { 0, 2, 4, 7, 9 } },
            { "Pentatonica minore",   { 0, 3, 5, 7, 10 } },
            { "Blues",                { 0, 3, 5, 6, 7, 10 } },
        };
        return list;
    }

    inline const char* noteName (int pitchClass)
    {
        static const char* names[] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        return names[((pitchClass % 12) + 12) % 12];
    }

    inline const char* letterName (int pitchClass)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((pitchClass % 12) + 12) % 12];
    }

    /** "La minore (Am)" */
    inline juce::String scaleLabel (int root, int type)
    {
        if (type <= 0 || type >= (int) scales().size())
            return "Nessuna scala";
        const auto& s = scales()[(size_t) type];
        juce::String label = juce::String (noteName (root)) + " " + juce::String (s.name).toLowerCase();
        if (type == 1) label << " (" << letterName (root) << ")";
        if (type == 2) label << " (" << letterName (root) << "m)";
        return label;
    }

    inline bool inScale (int pitch, int root, int type)
    {
        if (type <= 0 || type >= (int) scales().size())
            return true;
        const int pc = ((pitch - root) % 12 + 12) % 12;
        for (int i : scales()[(size_t) type].intervals)
            if (i == pc) return true;
        return false;
    }

    /** Grado della scala della nota (o di quella in scala subito sotto). */
    inline int degreeOf (int pitch, int root, int type, int& octaveOut)
    {
        const auto& iv = scales()[(size_t) juce::jlimit (1, (int) scales().size() - 1, type)].intervals;
        const int rel = pitch - root;
        octaveOut = (int) std::floor (rel / 12.0);
        const int pc = rel - octaveOut * 12;
        int degree = 0;
        for (int d = 0; d < (int) iv.size(); ++d)
            if (iv[(size_t) d] <= pc) degree = d;
        return degree;
    }

    /** La nota del grado indicato (i gradi oltre la scala salgono di ottava). */
    inline int pitchOfDegree (int root, int type, int degree, int octave)
    {
        const auto& iv = scales()[(size_t) juce::jlimit (1, (int) scales().size() - 1, type)].intervals;
        const int n = (int) iv.size();
        const int oct = octave + (int) std::floor ((double) degree / n);
        const int d = ((degree % n) + n) % n;
        return root + oct * 12 + iv[(size_t) d];
    }

    enum ChordType { triad = 0, seventh, add9, sus2, sus4, power, numChordTypes };

    inline const char* chordTypeName (int t)
    {
        static const char* names[] = { "Triade", "Settima", "Add9", "Sus2", "Sus4", "Power (5)" };
        return names[juce::jlimit (0, numChordTypes - 1, t)];
    }

    /**
        L'accordo con la fondamentale sulla nota toccata. Con una scala a 7 note si usano le note della
        scala (accordi "diatonici": maggiori, minori, diminuiti al posto giusto); senza scala, maggiore.
    */
    inline std::vector<int> chordAt (int pitch, int root, int type, int chordType)
    {
        if (chordType == sus2)  return { pitch, pitch + 2, pitch + 7 };
        if (chordType == sus4)  return { pitch, pitch + 5, pitch + 7 };
        if (chordType == power) return { pitch, pitch + 7, pitch + 12 };

        const bool diatonic = type > 0 && type < (int) scales().size() && scales()[(size_t) type].intervals.size() == 7;
        std::vector<int> notes;
        if (diatonic)
        {
            int octave = 0;
            const int degree = degreeOf (pitch, root, type, octave);
            for (int k : { 0, 2, 4 })
                notes.push_back (pitchOfDegree (root, type, degree + k, octave));
            if (chordType == seventh) notes.push_back (pitchOfDegree (root, type, degree + 6, octave));
            if (chordType == add9)    notes.push_back (pitchOfDegree (root, type, degree + 8, octave));
        }
        else
        {
            const bool minor = type > 0 && inScale (pitch + 3, root, type) && ! inScale (pitch + 4, root, type);
            notes = { pitch, pitch + (minor ? 3 : 4), pitch + 7 };
            if (chordType == seventh) notes.push_back (pitch + (minor ? 10 : 10));
            if (chordType == add9)    notes.push_back (pitch + 14);
        }
        return notes;
    }

    struct Progression
    {
        const char* name;
        bool minor;                   // pensata per una scala minore (o maggiore)
        std::vector<int> degrees;     // gradi della scala (0 = tonica)
    };

    inline const std::vector<Progression>& progressions()
    {
        static const std::vector<Progression> list =
        {
            { "I - V - vi - IV  (pop)",              false, { 0, 4, 5, 3 } },
            { "vi - IV - I - V  (emozionale)",       false, { 5, 3, 0, 4 } },
            { "I - vi - IV - V  (anni '50)",         false, { 0, 5, 3, 4 } },
            { "I - IV - V - IV  (rock)",             false, { 0, 3, 4, 3 } },
            { "ii - V - I - I  (jazz)",              false, { 1, 4, 0, 0 } },
            { "i - VI - III - VII  (epica)",         true,  { 0, 5, 2, 6 } },
            { "i - VII - VI - VII  (EDM)",           true,  { 0, 6, 5, 6 } },
            { "i - iv - v - i  (malinconica)",       true,  { 0, 3, 4, 0 } },
            { "i - VI - VII - i  (trance)",          true,  { 0, 5, 6, 0 } },
            { "i - iv - VII - III  (trap / dark)",   true,  { 0, 3, 6, 2 } },
            { "i - i - VI - VII  (techno / house)",  true,  { 0, 0, 5, 6 } },
        };
        return list;
    }
}
