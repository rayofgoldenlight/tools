#include "grading.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static int clampi(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// ---------------- Genre Weight Sets ----------------

// Default (general)
const RatingWeights DEFAULT_WEIGHTS = {
    .harmonic    = {.w_centroid=-0.005, .w_roughness=-40, .w_dissonance=-50, .w_key=15, .bias=70},
    .progression = {.w_tempo=0.05, .w_drange=1.5, .w_key=10, .bias=50},
    .pleasantness= {.w_roughness=-50, .w_dissonance=-60, .w_loudness=1.2, .bias=65},
    .creativity  = {.w_brightness=25, .w_drange=1.0, .w_tempo=0.03, .bias=55}
};


// Rap: emphasize loudness, groove (tempo), moderate harmonic
const RatingWeights RAP_WEIGHTS = {
    .harmonic    = {.w_centroid=-0.003,.w_key=10,.bias=60},
    .progression = {.w_tempo=0.07,.w_drange=1.0,.bias=55},
    .pleasantness= {.w_roughness=-40,.w_dissonance=-40,.w_loudness=2.0,.bias=70},
    .creativity  = {.w_brightness=20,.w_drange=0.8,.bias=60}
};

// VGM: emphasize pleasantness + creativity, dynamics important
const RatingWeights VGM_WEIGHTS = {
    .harmonic    = {.w_key=20,.bias=65},
    .progression = {.w_tempo=0.04,.w_drange=2.0,.w_key=10,.bias=55},
    .pleasantness= {.w_roughness=-60,.w_dissonance=-60,.bias=75},
    .creativity  = {.w_brightness=30,.w_drange=2.5,.bias=60}
};

// Pop: emphasize pleasantness, clarity, catchy tempo
const RatingWeights POP_WEIGHTS = {
    .harmonic    = {.w_centroid=-0.004,.w_key=20,.bias=70},
    .progression = {.w_tempo=0.06,.w_drange=1.2,.w_key=15,.bias=60},
    .pleasantness= {.w_roughness=-70,.w_dissonance=-70,.w_loudness=1.5,.bias=80},
    .creativity  = {.w_brightness=15,.w_drange=1.0,.bias=50}
};

// Experimental: reward extremes, brightness, dynamics
const RatingWeights EXPERIMENTAL_WEIGHTS = {
    .harmonic    = {.w_centroid=0.002,.bias=50}, // neutral harmonic eval
    .progression = {.w_tempo=0.01,.bias=55},
    .pleasantness= {.w_roughness=-20,.w_dissonance=-20,.bias=55}, // tolerate more noise
    .creativity  = {.w_brightness=40,.w_drange=3.0,.bias=80}
};

// Phonk: emphasize bass, groove, darkness; roughness tolerable
const RatingWeights PHONK_WEIGHTS = {
    .harmonic    = {.w_centroid=-0.002,.w_key=10,.bias=65},
    .progression = {.w_tempo=0.08,.bias=60},
    .pleasantness= {.w_roughness=-30,.w_dissonance=-35,.bias=65},
    .creativity  = {.w_brightness=10,.w_drange=1.5,.bias=55}
};

// ---------------------------------------------------

static double gauss_bonus(double x, double mu, double sigma, double max_bonus) {
    double z = (x - mu) / sigma;
    return max_bonus * exp(-0.5 * z * z);
}

static int score_category(const CategoryWeights* w,
                          const SpectralFeatures* spec,
                          double tempo_bpm,
                          const char* key,
                          const PsychoacousticFeatures* psy) {
    double sum = w->bias;

    if (spec) {
        sum += w->w_centroid   * (spec->centroid / 1000.0);
        sum += w->w_rolloff    * (spec->rolloff  / 1000.0);
        sum += w->w_brightness * spec->brightness;
    }
    if (psy) {
        sum += w->w_roughness  * psy->roughness;
        sum += w->w_dissonance * psy->dissonance;
        // sum += w->w_loudness   * (psy->loudness_lu / 10.0)
        sum += w->w_drange     * psy->dynamic_range;
        if (w == &DEFAULT_WEIGHTS.pleasantness ||
            w == &POP_WEIGHTS.pleasantness ||
            w == &RAP_WEIGHTS.pleasantness ||
            w == &VGM_WEIGHTS.pleasantness ||
            w == &PHONK_WEIGHTS.pleasantness) {
            sum += gauss_bonus(psy->loudness_lu, -14.0, 4.0, 12.0);
        }
        
    }
    sum += w->w_tempo * (tempo_bpm / 100.0);
    if (key && strcmp(key,"unknown") != 0) sum += w->w_key;

    int score = (int)round(sum);
    return clampi(score, 0, 100);
}

int compute_ratings(const SpectralFeatures* spec,
                    double tempo_bpm,
                    const char* key,
                    const PsychoacousticFeatures* psy,
                    Ratings* out,
                    const RatingWeights* weights) {
    if (!out || !weights) return -1;

    out->harmonic_quality    = score_category(&weights->harmonic, spec, tempo_bpm, key, psy);
    out->progression_quality = score_category(&weights->progression, spec, tempo_bpm, key, psy);
    out->pleasantness        = score_category(&weights->pleasantness, spec, tempo_bpm, key, psy);
    out->creativity          = score_category(&weights->creativity, spec, tempo_bpm, key, psy);

    out->overall_grade = (out->harmonic_quality +
                          out->progression_quality +
                          out->pleasantness +
                          out->creativity) / 4;
    return 0;
}