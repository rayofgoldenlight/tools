#ifndef RHYTHM_H
#define RHYTHM_H

#include <stddef.h>

typedef struct {
    double tempo_bpm;          // Detected main tempo (beats per minute)
    double tempo_confidence;   // Confidence of tempo estimation [0-1]
    double beat_strength;      // Average onset clarity or beat strength
    double pulse_clarity;      // How steady/clear the beat pulse is [0-1]
    double syncopation;        // Level of off-beat complexity
    double swing_ratio;        // If swing is detected, ratio (e.g. 2:1 for triplet feel)
} RhythmFeatures;

/**
 * Compute rhythmic features of a mono PCM signal.
 *
 * @param mono        - pointer to mono float samples
 * @param frames      - number of frames in PCM buffer
 * @param sample_rate - sampling rate (e.g. 44100 Hz)
 * @param out         - pointer to struct to fill with rhythm features
 * @return 0 on success, nonzero on error
 */
int compute_rhythm_features(const float* mono,
                            size_t frames,
                            int sample_rate,
                            RhythmFeatures* out);

#endif // RHYTHM_H