#ifndef MELODY_H
#define MELODY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double median_f0;                /* Hz */
    double mean_f0;                  /* Hz */
    double f0_confidence;            /* fraction voiced frames 0..1 */
    double pitch_range_semitones;    /* max - min (semitones) */
    int contour_count;               /* number of contiguous voiced contours */
    double avg_contour_length_sec;
    double longest_contour_sec;
    double avg_interval_semitones;   /* signed average interval between adjacent voiced frames */
    double avg_abs_interval_semitones;
    double melodic_entropy;          /* 0..1 (normalized) */
    double motif_repetition_rate;    /* 0..1 fraction of motif occurrences that are repeats */
    int motif_count;                 /* number of unique motifs found */
    double hook_strength;            /* 0..1 heuristic combining repetition + length + energy */
} MelodyFeatures;

/* Returns 0 on success (features filled). Non-zero only on invalid input.
 * The function is conservative: if no voiced material is found it still returns 0
 * and fills the features with 0/NaN-safe values. 
 */
int compute_melody_features(const float* mono,
                            size_t frames,
                            int sample_rate,
                            MelodyFeatures* out);

#ifdef __cplusplus
}
#endif

#endif /* MELODY_H */