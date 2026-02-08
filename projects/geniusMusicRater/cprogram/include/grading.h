#ifndef GRADING_H
#define GRADING_H

#include "feature_extractor.h"
#include "psychoacoustics.h"

// Ratings in 0–100 scale
typedef struct {
    int harmonic_quality;
    int progression_quality;
    int pleasantness;
    int creativity;
    int overall_grade;
} Ratings;

typedef struct {
    double w_centroid;
    double w_rolloff;
    double w_brightness;
    double w_roughness;
    double w_dissonance;
    double w_loudness;
    double w_drange;
    double w_tempo;
    double w_key;
    double bias;
} CategoryWeights;

typedef struct {
    CategoryWeights harmonic;
    CategoryWeights progression;
    CategoryWeights pleasantness;
    CategoryWeights creativity;
} RatingWeights;

// Genre selector
typedef enum {
    GENRE_DEFAULT = 0,
    GENRE_RAP,
    GENRE_VGM,
    GENRE_POP,
    GENRE_EXPERIMENTAL,
    GENRE_PHONK
} GenreType;

// Pre‑defined weight sets
extern const RatingWeights DEFAULT_WEIGHTS;
extern const RatingWeights RAP_WEIGHTS;
extern const RatingWeights VGM_WEIGHTS;
extern const RatingWeights POP_WEIGHTS;
extern const RatingWeights EXPERIMENTAL_WEIGHTS;
extern const RatingWeights PHONK_WEIGHTS;

// Compute ratings
int compute_ratings(const SpectralFeatures* spec,
                    double tempo_bpm,
                    const char* key,
                    const PsychoacousticFeatures* psy,
                    Ratings* out,
                    const RatingWeights* weights);

#endif