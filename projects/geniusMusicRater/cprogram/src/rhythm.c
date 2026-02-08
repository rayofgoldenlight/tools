#include "rhythm.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------- Internal Helper Functions ----------

/**
 * Simple Hann window generator
 */
static void hann_window(float* w, size_t N) {
    for (size_t i = 0; i < N; i++) {
        w[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * (float)i / (float)(N - 1)));
    }
}


/**
 * Energy-based onset envelope.
 * Splits audio into hop-sized frames, computes RMS energy,
 * then returns the positive differences (onset strength).
 */
static float* compute_onset_envelope_energy(const float* mono,
                                            size_t frames,
                                            int sample_rate,
                                            size_t hop_size,
                                            size_t* out_len) {
    if (!mono || frames < hop_size) {
        *out_len = 0;
        return NULL;
    }

    size_t num_frames = frames / hop_size;
    float* env = (float*)calloc(num_frames, sizeof(float));
    if (!env) {
        *out_len = 0;
        return NULL;
    }

    // 1. Compute RMS energy per frame
    for (size_t f = 0; f < num_frames; f++) {
        double sum = 0.0;
        for (size_t i = 0; i < hop_size; i++) {
            size_t idx = f * hop_size + i;
            if (idx < frames) {
                float x = mono[idx];
                sum += (double)x * (double)x;
            }
        }
        env[f] = (float)sqrt(sum / (double)hop_size);
    }

    // 2. Convert to onset detection function (positive first differences)
    for (size_t f = num_frames-1; f > 0; f--) {
        float diff = env[f] - env[f-1];
        env[f] = (diff > 0 ? diff : 0.0f);
    }
    env[0] = 0.0f;

    *out_len = num_frames;
    return env;
}

/**
 * Estimate Tempo (BPM) using autocorrelation of onset envelope.
 * Confidence is defined as best_peak / second_best_peak ratio,
 * normalized into [0,1].
 */
static void estimate_tempo_from_odf(const float* odf,
                                    size_t odf_len,
                                    int sample_rate,
                                    size_t hop_size,
                                    double* tempo_bpm,
                                    double* confidence) {
    if (!odf || odf_len == 0) {
        *tempo_bpm = 0.0;
        *confidence = 0.0;
        return;
    }

    // Simple autocorrelation
    size_t max_lag = odf_len / 2;
    double best_val = -1.0, second_val = -1.0;
    size_t best_lag = 0;

    for (size_t lag = 1; lag < max_lag; lag++) {
        double sum = 0.0;
        for (size_t i = 0; i + lag < odf_len; i++) {
            sum += odf[i] * odf[i+lag];
        }

        // Only consider BPMs in reasonable range
        double sec = (lag * hop_size) / (double)sample_rate;
        double bpm = (sec > 0.0 ? 60.0 / sec : 0.0);
        if (bpm < 40.0 || bpm > 200.0) continue;

        // Track top 2 peaks
        if (sum > best_val) {
            second_val = best_val;
            best_val = sum;
            best_lag = lag;
        } else if (sum > second_val) {
            second_val = sum;
        }
    }

    if (best_lag > 0) {
        double sec = (best_lag * hop_size) / (double)sample_rate;
        *tempo_bpm = 60.0 / sec;

        // ----- Confidence improvement -----
        if (second_val > 0.0) {
            double ratio = best_val / second_val;  // >1 = dominant
            // map into [0,1] with a soft clamp
            *confidence = (ratio > 2.0 ? 1.0 : (ratio - 1.0));
            if (*confidence < 0.0) *confidence = 0.0;
            if (*confidence > 1.0) *confidence = 1.0;
        } else {
            *confidence = 1.0; // only one strong peak
        }
    } else {
        *tempo_bpm = 0.0;
        *confidence = 0.0;
    }
}

/**
 * Compute pulse clarity:
 * Compare average onset energy at beat-aligned positions vs. offbeats.
 * Returns a normalized value [0,1].
 */
static double compute_pulse_clarity(const float* odf,
                                    size_t odf_len,
                                    double tempo_bpm,
                                    int sample_rate,
                                    size_t hop_size) {
    if (!odf || odf_len == 0 || tempo_bpm <= 0.0) return 0.0;

    double hop_time = (double)hop_size / (double)sample_rate;
    double beat_period_sec = 60.0 / tempo_bpm;
    double beat_period_frames = beat_period_sec / hop_time;

    if (beat_period_frames < 2.0) return 0.0;

    double beat_energy = 0.0;
    double offbeat_energy = 0.0;
    int beat_count = 0;
    int offbeat_count = 0;

    // tolerance window (10% of beat period)
    size_t half_window = (size_t)(0.1 * beat_period_frames);

    for (size_t b = 0; b < odf_len; b += (size_t)beat_period_frames) {
        size_t center = (size_t)b;
        if (center >= odf_len) break;

        // --- Beat energy ---
        size_t start = (center > half_window ? center - half_window : 0);
        size_t end   = (center + half_window < odf_len ? center + half_window : odf_len - 1);
        float local_max = 0.0f;
        for (size_t i = start; i <= end; i++) {
            if (odf[i] > local_max) local_max = odf[i];
        }
        beat_energy += local_max;
        beat_count++;

        // --- Off-beat energy (halfway between beats) ---
        size_t mid = center + (size_t)(beat_period_frames / 2.0);
        if (mid < odf_len) {
            start = (mid > half_window ? mid - half_window : 0);
            end   = (mid + half_window < odf_len ? mid + half_window : odf_len - 1);
            float off_local = 0.0f;
            for (size_t i = start; i <= end; i++) {
                if (odf[i] > off_local) off_local = odf[i];
            }
            offbeat_energy += off_local;
            offbeat_count++;
        }
    }

    if (beat_count == 0) return 0.0;

    double avg_beat = beat_energy / (double)beat_count;
    double avg_off  = (offbeat_count > 0 ? offbeat_energy / (double)offbeat_count : 0.0);

    // Normalize clarity measure
    double clarity = 0.0;
    if (avg_beat > 0.0) {
        clarity = avg_beat / (avg_beat + avg_off + 1e-9); // ratio in [0..1]
    }

    return clarity;
}

/**
 * Compute syncopation level (0..1).
 * High = more off-beat emphasis.
 */
static double compute_syncopation(const float* odf,
                                  size_t odf_len,
                                  double tempo_bpm,
                                  int sample_rate,
                                  size_t hop_size) {
    if (!odf || odf_len == 0 || tempo_bpm <= 0.0) return 0.0;

    double hop_time = (double)hop_size / (double)sample_rate;
    double beat_period_sec = 60.0 / tempo_bpm;
    double beat_period_frames = beat_period_sec / hop_time;

    if (beat_period_frames < 2.0) return 0.0;

    double beat_energy = 0.0, offbeat_energy = 0.0;
    int beat_count = 0, offbeat_count = 0;
    size_t half_window = (size_t)(0.1 * beat_period_frames);

    for (size_t b = 0; b < odf_len; b += (size_t)beat_period_frames) {
        size_t center = (size_t)b;
        if (center >= odf_len) break;

        // beat
        size_t start = (center > half_window ? center - half_window : 0);
        size_t end   = (center + half_window < odf_len ? center + half_window : odf_len-1);
        float local_max = 0.0f;
        for (size_t i=start; i<=end; i++) if (odf[i] > local_max) local_max = odf[i];
        beat_energy += local_max;
        beat_count++;

        // offbeat
        size_t mid = center + (size_t)(beat_period_frames/2.0);
        if (mid < odf_len) {
            start = (mid > half_window ? mid - half_window : 0);
            end   = (mid + half_window < odf_len ? mid + half_window : odf_len-1);
            float off_local = 0.0f;
            for (size_t i=start; i<=end; i++) if (odf[i] > off_local) off_local = odf[i];
            offbeat_energy += off_local;
            offbeat_count++;
        }
    }

    if (beat_count == 0 || offbeat_count == 0) return 0.0;

    double avg_beat = beat_energy / (double)beat_count;
    double avg_off = offbeat_energy / (double)offbeat_count;

    double sync = avg_off / (avg_beat + avg_off + 1e-9); // normalized [0..1]
    return sync;
}

/**
 * Estimate swing ratio. Approx by comparing onset strengths
 * in first vs second half of each beat.
 *
 * Return around:
 *   ~1.0 => straight
 *   ~1.5-2.0 => swung feel
 */
static double compute_swing_ratio(const float* odf,
                                  size_t odf_len,
                                  double tempo_bpm,
                                  int sample_rate,
                                  size_t hop_size) {
    if (!odf || odf_len == 0 || tempo_bpm <= 0.0) return 1.0;

    double hop_time = (double)hop_size / (double)sample_rate;
    double beat_period_sec = 60.0 / tempo_bpm;
    double beat_period_frames = beat_period_sec / hop_time;
    if (beat_period_frames < 4.0) return 1.0;

    double half1 = 0.0, half2 = 0.0;
    int count1 = 0, count2 = 0;

    for (size_t b = 0; b < odf_len; b += (size_t)beat_period_frames) {
        size_t half_point = (size_t)(b + beat_period_frames/2.0);
        if (half_point >= odf_len) break;

        // average energy in first half
        double sum1 = 0.0;
        for (size_t i=b; i<half_point && i<odf_len; i++) sum1 += odf[i];
        if (half_point > b) { half1 += sum1/(half_point-b); count1++; }

        // average in second half
        double sum2 = 0.0;
        for (size_t i=half_point; i<b+beat_period_frames && i<odf_len; i++) sum2 += odf[i];
        if (b+beat_period_frames > half_point) { half2 += sum2/((b+beat_period_frames)-half_point); count2++; }
    }

    if (count1==0 || count2==0) return 1.0;

    double avg1 = half1/count1;
    double avg2 = half2/count2;
    if (avg1 < 1e-6 || avg2 < 1e-6) return 1.0;

    double ratio;
    if (avg1 > avg2)
        ratio = avg1/avg2; // strong first note (short-long)
    else
        ratio = avg2/avg1; // strong later note (long-short)

    // Clamp into reasonable swing ratio
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 3.0) ratio = 3.0;
    return ratio;
}

// ---------- Public Rhythm Stub (now with onset env) ----------

int compute_rhythm_features(const float* mono,
                            size_t frames,
                            int sample_rate,
                            RhythmFeatures* out) {
    if (!mono || frames == 0 || sample_rate <= 0 || !out)
        return -1;

    memset(out, 0, sizeof(*out));

    // Parameters for STFT-based onset detection
    size_t fft_size = 1024;   // ~23 ms at 44.1kHz
    size_t hop_size = 512;    // 50% overlap

    // Compute spectral flux onset envelope
    size_t odf_len = 0;
     float* onset_env = compute_onset_envelope_energy(mono, frames, sample_rate,
                                                     hop_size, &odf_len);

    if (!onset_env || odf_len == 0) {
        return -2; // onset detection failed
    }

    // ---------- FUTURE STEPS ----------
    // From now, we’ll:
    // - Normalize onset_env
    // - Autocorrelation/FFT for tempo
    // - Beat tracking from peaks
    //
    // For now, just compute average onset strength as proxy:
    double sum = 0.0;
    for (size_t i=0; i<odf_len; i++) sum += onset_env[i];
    double avg_flux = (odf_len>0? sum/odf_len : 0.0);

     // --- Tempo Estimation (Fast FFT ACF) ---
    double tempo_bpm = 0.0, tempo_conf = 0.0;
    estimate_tempo_from_odf(onset_env, odf_len, sample_rate, hop_size,
                            &tempo_bpm, &tempo_conf);
    
      // --- Pulse Clarity (Step 1.3) ---
    double clarity = compute_pulse_clarity(onset_env, odf_len,
                                           tempo_bpm, sample_rate, hop_size);

    out->tempo_bpm = tempo_bpm;
    out->tempo_confidence = tempo_conf;
    out->beat_strength = avg_flux; // crude proxy until beat tracking
    out->pulse_clarity = clarity;     // Step 1.3
      // --- Syncopation & Swing (Step 1.4) ---
    out->syncopation = compute_syncopation(onset_env, odf_len,
                                           tempo_bpm, sample_rate, hop_size);
    out->swing_ratio = compute_swing_ratio(onset_env, odf_len,
                                           tempo_bpm, sample_rate, hop_size);

    free(onset_env);
    return 0;
}