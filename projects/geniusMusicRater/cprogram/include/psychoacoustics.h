#ifndef PSYCHOACOUSTICS_H
#define PSYCHOACOUSTICS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double roughness;      // [0..1] relative
    double dissonance;     // [0..1] relative
    double loudness_lu;    // LUFS-like loudness estimate
    double dynamic_range;  // dB difference between loud & quiet percentiles
} PsychoacousticFeatures;

// Compute psychoacoustic features for a mono PCM buffer.
// Returns 0 on success.
int compute_psychoacoustics(const float* mono, size_t frames, int sr, PsychoacousticFeatures* out);

#ifdef __cplusplus
}
#endif

#endif