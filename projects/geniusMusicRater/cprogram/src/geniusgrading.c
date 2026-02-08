#include "geniusgrading.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

// Gaussian bonus helper
static int gaussian_score(double x, double mu, double sigma, int peak) {
    if (isnan(x)) return 50;
    double z = (x - mu) / sigma;
    double val = peak * exp(-0.5 * z * z);
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    return (int)round(val);
}

// ---------- CATEGORY SCORERS ----------

// 1. Harmony
static int compute_harmony_score(const GeniusInputs* in) {
    int stability = scale_to_100(in->harmony.key_stability, 0.0, 1.0);
    int tension   = gaussian_score(in->harmony.tension, 0.5, 0.2, 100);
    int has_key   = (strcmp(in->harmony.global_key, "unknown") ? 15 : 0);
    int base = (stability + tension) / 2 + has_key;
    return clampd(base, 0, 100);
}

// 2. Progression
static int compute_progression_score(const GeniusInputs* in) {
    int motion = scale_to_100(in->harmony.harmonic_motion, 0.0, 1.0);
    int mod_bonus = (in->harmony.modulation_count > 0 && in->harmony.modulation_count <= 3) ? 15 : 0;
    int tempo_score = scale_to_100(in->rhythm.tempo_bpm, 60, 180);
    int base = (motion + tempo_score) / 2 + mod_bonus;
    return clampd(base, 0, 100);
}

// 3. Melody
static int compute_melody_score(const GeniusInputs* in) {
    if (!in->melody_valid) return 50;
    int conf  = scale_to_100(in->melody.f0_confidence, 0.0, 1.0);
    int range = gaussian_score(in->melody.pitch_range_semitones, 12.0, 5.0, 100);
    int motif = gaussian_score(in->melody.motif_repetition_rate, 0.5, 0.15, 100);
    int hook  = scale_to_100(in->melody.hook_strength, 0.0, 1.0);
    int base = (conf + range + motif + hook) / 4;
    return base;
}

// 4. Rhythm
static int compute_rhythm_score(const GeniusInputs* in) {
    int conf   = scale_to_100(in->rhythm.tempo_confidence, 0.0, 1.0);
    int beat   = scale_to_100(in->rhythm.beat_strength, 0.0, 1.0);
    int clarity= scale_to_100(in->rhythm.pulse_clarity, 0.0, 1.0);
    int sync   = gaussian_score(in->rhythm.syncopation, 0.5, 0.2, 100);
    double s_ratio_dev = fabs(in->rhythm.swing_ratio - 1.0); // deviation from straight feel
    int swing = inverse_scale_to_100(s_ratio_dev, 0.0, 0.5);
    int base = (conf + beat + clarity + sync + swing) / 5;
    return base;
}

// 5. Structure
static int compute_structure_score(const GeniusInputs* in) {
    if (!in->structure_valid || in->structure.section_count == 0) return 50;
    int sec_bonus = gaussian_score((double)in->structure.section_count, 5.0, 2.0, 100);
    int arc       = scale_to_100(in->structure.arc_complexity, 0.0, 1.0);
    int rep       = gaussian_score(in->structure.repetition_ratio, 0.5, 0.2, 100);
    int chorus    = 0;
    for (size_t i=0; i<in->structure.section_count; i++) {
        if (strcmp(in->structure.sections[i].label, "chorus")==0) { chorus=15; break; }
    }
    int base = (sec_bonus + arc + rep) / 3 + chorus;
    return clampd(base, 0, 100);
}

// 6. Timbre / Pleasantness
static int compute_timbre_score(const GeniusInputs* in) {
    int rough = inverse_scale_to_100(in->psy.roughness, 0.0, 0.5);
    int diss  = inverse_scale_to_100(in->psy.dissonance, 0.0, 0.5);
    int loud  = gaussian_score(in->psy.loudness_lu, -14.0, 4.0, 100);
    int drng  = gaussian_score(in->psy.dynamic_range, 9.0, 3.0, 100);
    int stereo= scale_to_100(in->prod.stereo_width, 0.0, 1.0);
    int bal   = scale_to_100(in->prod.spectral_balance, 0.0, 1.0);
    int base = (rough + diss + loud + drng + stereo + bal) / 6;
    return base;
}

// 7. Creativity
static int compute_creativity_score(const GeniusInputs* in) {
    if (!in->melody_valid) return 50;
    int entropy = scale_to_100(in->melody.melodic_entropy, 0.0, 5.0);
    int interval= gaussian_score(in->melody.avg_abs_interval_semitones, 4.0, 2.0, 100);
    int sync    = gaussian_score(in->rhythm.syncopation, 0.5, 0.2, 100);
    int mask    = inverse_scale_to_100(in->prod.masking_index, 0.0, 1.0);
    int base = (entropy + interval + sync + mask) / 4;
    return base;
}

// ---------------- Genre Weight Profiles ----------------

// Default: balanced
const GeniusWeights GENIUS_DEFAULT_WEIGHTS = {
    .w_harmony=1,.w_progression=1,.w_melody=1,
    .w_rhythm=1,.w_structure=1,.w_timbre=1,
    .w_creativity=1,.bias=0
};

// Rap: creativity + rhythm higher
const GeniusWeights GENIUS_RAP_WEIGHTS = {
    .w_harmony=0.8,.w_progression=0.8,.w_melody=0.9,
    .w_rhythm=1.3,.w_structure=0.7,.w_timbre=1.0,
    .w_creativity=1.5,.bias=5
};

// VGM: structure + timbre + melody higher
const GeniusWeights GENIUS_VGM_WEIGHTS = {
    .w_harmony=0.9,.w_progression=0.9,.w_melody=1.2,
    .w_rhythm=0.9,.w_structure=1.3,.w_timbre=1.2,
    .w_creativity=1.0,.bias=0
};

// Pop: melody + timbre dominant
const GeniusWeights GENIUS_POP_WEIGHTS = {
    .w_harmony=0.9,.w_progression=1.0,.w_melody=1.4,
    .w_rhythm=1.0,.w_structure=0.9,.w_timbre=1.4,
    .w_creativity=1.0,.bias=3
};

// Experimental: creativity max
const GeniusWeights GENIUS_EXPERIMENTAL_WEIGHTS = {
    .w_harmony=0.7,.w_progression=0.7,.w_melody=0.8,
    .w_rhythm=0.9,.w_structure=0.8,.w_timbre=0.7,
    .w_creativity=2.0,.bias=0
};

// Phonk: rhythm, timbre, progression strongest
const GeniusWeights GENIUS_PHONK_WEIGHTS = {
    .w_harmony=0.8,.w_progression=1.2,.w_melody=0.9,
    .w_rhythm=1.4,.w_structure=0.8,.w_timbre=1.3,
    .w_creativity=1.0,.bias=2
};

// ---------- Step 5: penalties & gating ----------

// Returns total penalty in points [0..100]
static int compute_penalties(const GeniusInputs* in) {
    int penalty = 0;

    // Clipping + low dynamic range
    if (in->peak >= 0.99 && in->psy.dynamic_range < 3.0) {
        penalty += 10;
    }

    // DC offset
    if (fabs(in->dc_offset) > 0.05) {
        penalty += 5;
    }

    // Tempo unreliable
    if (in->rhythm.tempo_confidence < 0.3) {
        penalty += 7;
    }

    // Melody extraction failed
    if (!in->melody_valid) {
        penalty += 8;
    }

    // Production features missing
    if (!in->prod_valid) {
        penalty += 5;
    }

    return penalty;
}

// ---------- Step 7: originality & complexity ----------

// Info Rate proxy: arc_complexity + melodic entropy + syncopation
static int compute_originality_score(const GeniusInputs* in) {
    int arc  = scale_to_100(in->structure.arc_complexity, 0.0, 1.0);
    int ent  = scale_to_100(in->melody.melodic_entropy, 0.0, 5.0);
    int sync = scale_to_100(in->rhythm.syncopation, 0.0, 1.0);
    return (arc + ent + sync) / 3;
}

// Compression Complexity proxy: motif count + entropy + chord motion
static int compute_complexity_score(const GeniusInputs* in) {
    int motifs = scale_to_100(in->melody.motif_count, 0, 50); // assume <50 motifs
    int ent    = scale_to_100(in->melody.melodic_entropy, 0.0, 5.0);
    int motion = scale_to_100(in->harmony.harmonic_motion, 0.0, 1.0);
    return (motifs + ent + motion) / 3;
}

// Genre Distance proxy: reward being "out of range" moderately
static int compute_genre_distance_score(const GeniusInputs* in, GeniusGenre genre) {
    double centroid_tempo = 120.0; // default/pop baseline
    if (genre == GENIUS_GENRE_RAP) centroid_tempo = 90;
    else if (genre == GENIUS_GENRE_VGM) centroid_tempo = 110;
    else if (genre == GENIUS_GENRE_PHONK) centroid_tempo = 100;
    else if (genre == GENIUS_GENRE_EXPERIMENTAL) centroid_tempo = 0; // no expectation

    double diff = 0.0;
    if (centroid_tempo > 0)
        diff = fabs(in->rhythm.tempo_bpm - centroid_tempo);

    int novelty = scale_to_100(diff, 0.0, 60.0); // >60 BPM off seen as too far
    if (genre == GENIUS_GENRE_EXPERIMENTAL) novelty = 80; // always high novelty

    return novelty;
}

// ---------- Step 8: Emotion / Tension–Release ----------

static int compute_emotion_score(const GeniusInputs* in) {
    // Harmony tension sweet spot ~0.5
    int tension = gaussian_score(in->harmony.tension, 0.5, 0.2, 100);

    // Rhythmic propulsion
    int pulse   = scale_to_100(in->rhythm.pulse_clarity, 0.0, 1.0);
    int beat    = scale_to_100(in->rhythm.beat_strength, 0.0, 1.0);

    // Dynamic contrast (psychoacoustic dynamic range)
    int dyn     = gaussian_score(in->psy.dynamic_range, 9.0, 3.0, 100);

    // Structural arc complexity adds shape
    int arc     = scale_to_100(in->structure.arc_complexity, 0.0, 1.0);

    int base = (tension + pulse + beat + dyn + arc) / 5;
    return base;
}

// ---------- Step 9: Explanations ----------

static void fill_explanations(const GeniusInputs* in, GeniusResult* out, int penalty) {
    out->pos_count = 0;
    out->neg_count = 0;

    // Positive contributors
    if (out->melody_score >= 85 && out->pos_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->positives[out->pos_count++], GENIUS_MAX_TEXT,
                 "Strong melody (score %d)", out->melody_score);

    if (out->creativity_score >= 85 && out->pos_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->positives[out->pos_count++], GENIUS_MAX_TEXT,
                 "High creativity (score %d)", out->creativity_score);

    if (out->emotion_score >= 85 && out->pos_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->positives[out->pos_count++], GENIUS_MAX_TEXT,
                 "High emotional impact (score %d)", out->emotion_score);

    if (out->originality_score >= 80 && out->pos_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->positives[out->pos_count++], GENIUS_MAX_TEXT,
                 "Original structure/motifs (originality %d)", out->originality_score);

    // Negative contributors
    if (penalty > 0 && out->neg_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->negatives[out->neg_count++], GENIUS_MAX_TEXT,
                 "Technical penalties applied (%d)", penalty);

    if (out->timbre_score < 60 && out->neg_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->negatives[out->neg_count++], GENIUS_MAX_TEXT,
                 "Weak production/timbre (score %d)", out->timbre_score);

    if (out->rhythm_score < 60 && out->neg_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->negatives[out->neg_count++], GENIUS_MAX_TEXT,
                 "Unstable rhythm/pulse (score %d)", out->rhythm_score);

    if (out->structure_score < 50 && out->neg_count < GENIUS_MAX_EXPLAIN)
        snprintf(out->negatives[out->neg_count++], GENIUS_MAX_TEXT,
                 "Poor or unclear structure (score %d)", out->structure_score);
}

// ---------------------------------------------------------

// Updated top-level scorer
int compute_genius_rating(const GeniusInputs* in, GeniusResult* out, GeniusGenre genre) {
    if (!in || !out) return -1;

    // compute categories
    out->harmony_score    = compute_harmony_score(in);
    out->progression_score= compute_progression_score(in);
    out->melody_score     = compute_melody_score(in);
    out->rhythm_score     = compute_rhythm_score(in);
    out->structure_score  = compute_structure_score(in);
    out->timbre_score     = compute_timbre_score(in);
    out->creativity_score = compute_creativity_score(in);

    // select weight profile
    const GeniusWeights* W = &GENIUS_DEFAULT_WEIGHTS;
    switch (genre) {
        case GENIUS_GENRE_RAP:          W = &GENIUS_RAP_WEIGHTS; break;
        case GENIUS_GENRE_VGM:          W = &GENIUS_VGM_WEIGHTS; break;
        case GENIUS_GENRE_POP:          W = &GENIUS_POP_WEIGHTS; break;
        case GENIUS_GENRE_EXPERIMENTAL: W = &GENIUS_EXPERIMENTAL_WEIGHTS; break;
        case GENIUS_GENRE_PHONK:        W = &GENIUS_PHONK_WEIGHTS; break;
        default: break;
    }

    // weighted aggregate
    double num = out->harmony_score    * W->w_harmony +
                 out->progression_score* W->w_progression +
                 out->melody_score     * W->w_melody +
                 out->rhythm_score     * W->w_rhythm +
                 out->structure_score  * W->w_structure +
                 out->timbre_score     * W->w_timbre +
                 out->creativity_score * W->w_creativity +
                 W->bias;

    double den = W->w_harmony + W->w_progression + W->w_melody +
                 W->w_rhythm + W->w_structure + W->w_timbre +
                 W->w_creativity;

    int overall = (int)round(num / den);

    // Apply penalties
    int penalty = compute_penalties(in);
    out->overall_score = (overall - penalty >= 0 ? overall - penalty : 0);

    // Genius logic
    out->is_genius = (out->overall_score >= 85 &&
                     (out->melody_score >= 80 ||
                      out->creativity_score >= 80 ||
                      out->harmony_score >= 80));

    // Confidence adjustment
    double conf = (in->rhythm.tempo_confidence + in->melody.f0_confidence) / 2.0;

    // Reduce confidence if major modules missing
    if (!in->melody_valid)  conf *= 0.8;
    if (!in->structure_valid) conf *= 0.9;
    if (!in->prod_valid) conf *= 0.9;

    out->confidence = conf;

    // --- Step 7: add originality & complexity ---
    out->originality_score   = compute_originality_score(in);
    out->complexity_score    = compute_complexity_score(in);
    out->genre_distance_score= compute_genre_distance_score(in, genre);

    // Boost genius flag if both originality+complexity are excellent
    if (!out->is_genius && 
        out->originality_score >= 85 &&
        out->complexity_score >= 85) {
        out->is_genius = 1;
    }

    // --- Step 8: emotion / tension release ---
    out->emotion_score = compute_emotion_score(in);

    // If emotion_score is very high, reinforce genius verdict
    if (!out->is_genius && out->emotion_score >= 90 && out->overall_score >= 80) {
        out->is_genius = 1;
    }

    // Fill explanations
    fill_explanations(in, out, penalty);


    return 0;
}