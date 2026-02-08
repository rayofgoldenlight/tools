#ifndef GENIUS_GRADING_H
#define GENIUS_GRADING_H
#define GENIUS_MAX_EXPLAIN 10
#define GENIUS_MAX_TEXT    80

#include "feature_extractor.h"
#include "psychoacoustics.h"
#include "rhythm.h"
#include "harmony.h"
#include "melody.h"
#include "structure.h"
#include "production.h"
#include <math.h>

// Already defined before...
typedef struct {
    double duration_sec;
    double rms;
    double peak;
    double dc_offset;
    double zcr;

    SpectralFeatures spectral;
    RhythmFeatures   rhythm;
    HarmonyFeatures  harmony;
    MelodyFeatures   melody;
    int melody_valid;
    StructureFeatures structure;
    int structure_valid;
    PsychoacousticFeatures psy;
    ProductionFeatures prod;
    int prod_valid;
} GeniusInputs;

typedef struct {
    int harmony_score;
    int progression_score;
    int melody_score;
    int rhythm_score;
    int structure_score;
    int timbre_score;
    int creativity_score;

    int overall_score;   
    int is_genius;       
    double confidence;   

    // Step 7: Originality/Complexity extras
    int originality_score;
    int complexity_score;
    int genre_distance_score;

    // Step 8: Emotion / Tension–Release
    int emotion_score;

    // Step 9: Explanations
    int pos_count;
    int neg_count;
    char positives[GENIUS_MAX_EXPLAIN][GENIUS_MAX_TEXT];
    char negatives[GENIUS_MAX_EXPLAIN][GENIUS_MAX_TEXT];


} GeniusResult;

// ---------- Normalization Helpers (Step 2) ----------
static inline double clampd(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int scale_to_100(double x, double min, double max) {
    if (isnan(x) || max <= min) return 50; // fallback neutral
    double norm = (x - min) / (max - min);
    norm = clampd(norm, 0.0, 1.0);
    return (int)round(norm * 100.0);
}

static inline int inverse_scale_to_100(double x, double min, double max) {
    // high x → low score
    int val = scale_to_100(x, min, max);
    return 100 - val;
}

// Genre selector
typedef enum {
    GENIUS_GENRE_DEFAULT = 0,
    GENIUS_GENRE_RAP,
    GENIUS_GENRE_VGM,
    GENIUS_GENRE_POP,
    GENIUS_GENRE_EXPERIMENTAL,
    GENIUS_GENRE_PHONK
} GeniusGenre;

// Per-genre category weights
typedef struct {
    double w_harmony;
    double w_progression;
    double w_melody;
    double w_rhythm;
    double w_structure;
    double w_timbre;
    double w_creativity;
    double bias;
} GeniusWeights;

// Predefined profiles
extern const GeniusWeights GENIUS_DEFAULT_WEIGHTS;
extern const GeniusWeights GENIUS_RAP_WEIGHTS;
extern const GeniusWeights GENIUS_VGM_WEIGHTS;
extern const GeniusWeights GENIUS_POP_WEIGHTS;
extern const GeniusWeights GENIUS_EXPERIMENTAL_WEIGHTS;
extern const GeniusWeights GENIUS_PHONK_WEIGHTS;

// Compute rating with genre profile
int compute_genius_rating(const GeniusInputs* in,
                          GeniusResult* out,
                          GeniusGenre genre);

#endif