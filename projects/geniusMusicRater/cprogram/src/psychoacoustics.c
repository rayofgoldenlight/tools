#include "psychoacoustics.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

// --- Helper: percentile ---
static double percentile(double* arr, size_t n, double perc) {
    if (n == 0) return 0.0;
    size_t idx = (size_t)floor(perc * (n-1));
    if (idx >= n) idx = n-1;
    return arr[idx];
}

// --- Helper: simple A-weighting function (approximate response) ---
static double a_weight(double f) {
    double f2 = f*f;
    double num = 12200.0*12200.0 * f2*f2;
    double den = (f2 + 20.6*20.6) * sqrt((f2+107.7*107.7)*(f2+737.9*737.9)) * (f2 + 12200.0*12200.0);
    return den>0 ? num/den : 0.0;
}

int compute_psychoacoustics(const float* mono, size_t frames, int sr, PsychoacousticFeatures* out) {
    if (!mono || !out || frames == 0 || sr <= 0) return -1;

    // Frame parameters (psychoacoustically reasonable and efficient)
    const int win = 4096;
    const int hop = 2048;

    size_t n_frames;
    if (frames <= (size_t)win) n_frames = 1;
    else n_frames = 1 + (frames - (size_t)win) / (size_t)hop;

    double* rms = (double*)calloc(n_frames, sizeof(double));
    double* rms_db = (double*)calloc(n_frames, sizeof(double));
    if (!rms || !rms_db) { free(rms); free(rms_db); return -2; }

    // Compute frame RMS and dB
    for (size_t f = 0; f < n_frames; ++f) {
        size_t off = f * (size_t)hop;
        size_t wlen = win;
        if (off + (size_t)win > frames) wlen = (size_t) (frames - off);
        if (wlen == 0) { rms[f] = 0.0; rms_db[f] = -120.0; continue; }

        long double acc = 0.0L;
        for (size_t i = 0; i < wlen; ++i) {
            long double v = mono[off + i];
            acc += v * v;
        }
        double r = sqrt((double)(acc / (long double)wlen));
        rms[f] = r;
        rms_db[f] = 20.0 * log10(r + 1e-12);
    }

    // Loudness (LUFS-like) with simple gating at -70 dBFS and K-weighting fudge
    // We approximate integrated loudness as 10*log10(mean square of gated frames) - 0.691
    long double sum_ms = 0.0L;
    size_t count_ms = 0;
    for (size_t f = 0; f < n_frames; ++f) {
        if (rms_db[f] > -70.0) {
            long double ms = (long double)rms[f] * (long double)rms[f];
            sum_ms += ms;
            count_ms++;
        }
    }
    if (count_ms == 0) { // fallback to all frames
        for (size_t f = 0; f < n_frames; ++f) {
            long double ms = (long double)rms[f] * (long double)rms[f];
            sum_ms += ms;
        }
        count_ms = n_frames ? n_frames : 1;
    }
    double mean_ms = (double)(sum_ms / (long double)count_ms);
    double loudness_lu = -0.691 + 10.0 * log10(mean_ms + 1e-12);

    // Dynamic range in dB using percentiles of frame RMS in dB
    double* sorted_db = (double*)malloc(n_frames * sizeof(double));
    if (!sorted_db) { free(rms); free(rms_db); return -3; }
    memcpy(sorted_db, rms_db, n_frames * sizeof(double));
    qsort(sorted_db, n_frames, sizeof(double), cmp_double);
    size_t idx05 = (size_t)floor(0.05 * (double)(n_frames - 1));
    size_t idx95 = (size_t)floor(0.95 * (double)(n_frames - 1));
    double p05 = sorted_db[idx05];
    double p95 = sorted_db[idx95];
    double dynamic_range_db = p95 - p05;
    free(sorted_db);

    // Roughness: mean absolute frame-to-frame change of RMS in dB, normalized
    double mad = 0.0;
    for (size_t f = 1; f < n_frames; ++f) {
        mad += fabs(rms_db[f] - rms_db[f - 1]);
    }
    mad = (n_frames > 1) ? mad / (double)(n_frames - 1) : 0.0;
    // Map to ~[0,1] with a soft saturation; typical pop tracks ~0.5–5 dB/frame
    double roughness = tanh(mad / 6.0); // 6 dB scale; adjust if desired

    // Dissonance proxy: variance of first difference (in dB) relative to energy
    double sum_d = 0.0, sum_d2 = 0.0;
    for (size_t f = 1; f < n_frames; ++f) {
        double d = rms_db[f] - rms_db[f - 1];
        sum_d += d;
        sum_d2 += d * d;
    }
    double mean_d = (n_frames > 1) ? sum_d / (double)(n_frames - 1) : 0.0;
    double var_d = (n_frames > 1) ? (sum_d2 / (double)(n_frames - 1) - mean_d * mean_d) : 0.0;
    if (var_d < 0.0) var_d = 0.0;
    // Map to [0,1] with soft saturation; higher modulation variance -> higher perceived "busy-ness"/dissonance proxy
    double dissonance = tanh(sqrt(var_d) / 8.0); // 8 dB/sqrt(frame) scale

    out->roughness = roughness;
    out->dissonance = dissonance;
    out->loudness_lu = loudness_lu;
    out->dynamic_range = dynamic_range_db;

    free(rms_db);
    free(rms);
    return 0;
}