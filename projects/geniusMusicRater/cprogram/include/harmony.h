#ifndef HARMONY_H
#define HARMONY_H

#include <stddef.h>

// Basic chord label
typedef struct {
    char name[16];     // e.g. "Cmaj", "G7", "Am"
    double time_sec;   // estimated beat/time position
} ChordLabel;

// Harmony features
typedef struct {
    char global_key[16];       // global/most probable key ("C", "Am", etc.)
    double key_stability;      // 0..1 how consistent piece is in one key
    double modulation_count;   // number of detected key changes
    double harmonic_motion;    // average Tonnetz distance between chords
    double tension;            // avg harmonic tension (0..1 scale)
    int chord_count;
    ChordLabel* chords;        // dynamic array of chords (beat-synchronous)
} HarmonyFeatures;

/**
 * Extract harmony from PCM.
 *
 * mono        - mono PCM float samples
 * frames      - # samples
 * sample_rate - sample rate
 * out         - struct to fill
 * returns 0 = success, nonzero = error
 */
int compute_harmony_features(const float* mono,
                             size_t frames,
                             int sample_rate,
                             HarmonyFeatures* out);

// Free chord array
void free_harmony_features(HarmonyFeatures* hf);

#endif // HARMONY_H