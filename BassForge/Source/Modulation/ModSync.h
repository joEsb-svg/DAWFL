#pragma once

namespace bf
{
    // Sync al tempo host: durata in BATTITI di un ciclo LFO (o di UNO STEP del Performer).
    inline constexpr int kNumSyncDivisions = 12;
    inline constexpr float kSyncBeats[kNumSyncDivisions] = {
        16.f, 8.f, 4.f, 2.f, 4.f / 3.f, 1.f, 2.f / 3.f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f };
    inline constexpr const char* kSyncNames[kNumSyncDivisions] = {
        "4 bar", "2 bar", "1 bar", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
}
