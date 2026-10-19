#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace bf
{
    // Un unico "suono" che risponde a tutti i canali e a tutte le note.
    struct SynthSound : public juce::SynthesiserSound
    {
        bool appliesToNote(int) override    { return true; }
        bool appliesToChannel(int) override { return true; }
    };
}
