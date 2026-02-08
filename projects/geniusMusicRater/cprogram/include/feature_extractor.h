#ifndef FEATURE_EXTRACTOR_H
#define FEATURE_EXTRACTOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEATURE_MFCC_COUNT 13

typedef struct {
    double centroid;   // Hz
    double rolloff;    // Hz (85% energy)
    double brightness; // ratio [0,1] energy above ~1500 Hz
    double mfcc[FEATURE_MFCC_COUNT]; // averaged over frames
} SpectralFeatures;

// Compute spectral features from mono PCM at sample rate sr.
// Returns 0 on success.
int compute_spectral_features(const float* mono, size_t frames, int sr, SpectralFeatures* out);

// Estimate tempo in BPM using onset envelope + autocorrelation.
// Returns 0 on success; out_bpm set to 0 if uncertain.
int estimate_tempo_bpm(const float* mono, size_t frames, int sr, double* out_bpm);

// Estimate musical key (e.g., "C major", "A minor") using chroma + Krumhansl profiles.
// out_key must have space for at least 8 chars. Returns 0 on success.
int estimate_key(const float* mono, size_t frames, int sr, char out_key[8]);

#ifdef __cplusplus
}
#endif

#endif