#include "structure.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "feature_extractor.h"  // for FEATURE_MFCC_COUNT

// helper: magnitude spectrum (simple DFT)
static void compute_magnitude_spectrum(const float* frame, int N, double* mag) {
    for (int k = 0; k < N/2; k++) {
        double real = 0.0, imag = 0.0;
        for (int n = 0; n < N; n++) {
            double angle = -2.0 * M_PI * k * n / N;
            real += frame[n] * cos(angle);
            imag += frame[n] * sin(angle);
        }
        mag[k] = sqrt(real*real + imag*imag);
    }
}

// novelty function (spectral flux)
static int compute_novelty_curve(const float* mono, size_t frames, int sr,
                                 double hop_sec, double** out_curve, size_t* out_len) {
    int win_size = 1024; // ~23ms at 44.1kHz
    int hop_size = (int)(hop_sec * sr); // hop in samples
    if (hop_size <= 0) hop_size = win_size / 2;

    size_t n_frames = (frames - win_size) / hop_size;
    double* novelty = (double*)calloc(n_frames, sizeof(double));
    if (!novelty) return 1;

    double* prev_mag = (double*)calloc(win_size/2, sizeof(double));
    double* mag = (double*)calloc(win_size/2, sizeof(double));

    for (size_t f = 0; f < n_frames; f++) {
        const float* frame = mono + f * hop_size;
        compute_magnitude_spectrum(frame, win_size, mag);

        double flux = 0.0;
        for (int k = 0; k < win_size/2; k++) {
            double diff = mag[k] - prev_mag[k];
            if (diff > 0) flux += diff; // rectified difference
            prev_mag[k] = mag[k];
        }
        novelty[f] = flux;
    }

    free(prev_mag);
    free(mag);

    *out_curve = novelty;
    *out_len = n_frames;
    return 0;
}

// --- Helper: simple spectral descriptor per section (avg energy + centroid) ---
static void section_descriptor(const float* mono, size_t frames, int sr,
                               double start_sec, double end_sec,
                               double* energy, double* centroid)
{
    size_t start_idx = (size_t)(start_sec * sr);
    size_t end_idx   = (size_t)(end_sec * sr);
    if (end_idx > frames) end_idx = frames;

    double sum = 0.0, sumsq = 0.0;
    double weighted = 0.0, mag_sum = 0.0;

    for (size_t i = start_idx; i < end_idx; i++) {
        double x = mono[i];
        sum += x;
        sumsq += x*x;
        weighted += fabs(x) * (i - start_idx);
        mag_sum += fabs(x);
    }

    size_t len = (end_idx > start_idx ? end_idx - start_idx : 1);
    *energy = sqrt(sumsq / len);  // RMS

    *centroid = (mag_sum > 1e-9 ? weighted / mag_sum : 0.0);
    *centroid /= (double)len; // normalize
}

static double cosine_similarity(const double* a, const double* b, int dim) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i=0; i<dim; i++) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    if (na < 1e-9 || nb < 1e-9) return 0.0;
    return dot / (sqrt(na) * sqrt(nb));
}

int compute_structure_features(const float* mono,
                               size_t frames,
                               int sample_rate,
                               StructureFeatures* out)
{
    if (!mono || frames == 0 || sample_rate <= 0 || !out) return 1;

    // reset
    out->sections = NULL;
    out->section_count = 0;
    out->arc_complexity = 0.0;
    out->repetition_ratio = 0.0;

    // compute novelty curve
    double* novelty = NULL;
    size_t n_frames = 0;
    if (compute_novelty_curve(mono, frames, sample_rate, 0.5, &novelty, &n_frames) != 0)
        return 2;

    // normalize novelty
    double maxval = 1e-9;
    for (size_t i=0; i<n_frames; i++) if (novelty[i] > maxval) maxval = novelty[i];
    for (size_t i=0; i<n_frames; i++) novelty[i] /= maxval;

    // pick boundaries
    double threshold = 0.5; // relative novelty, changed from 0.3
    size_t max_sections = 128;
    Section* sections = (Section*)calloc(max_sections, sizeof(Section));
    if (!sections) { free(novelty); return 3; }
    
    size_t sec_count = 0;
    double duration_sec = (double)frames / sample_rate;
    double last_boundary = 0.0;

    for (size_t i=1; i<n_frames-1; i++) {
        if (novelty[i] > threshold &&
            novelty[i] > novelty[i-1] &&
            novelty[i] > novelty[i+1]) {
            double time_sec = (double)(i * 0.5); // hop_sec = 0.5s
            if (time_sec - last_boundary > 20.0) {
                if (sec_count < max_sections) {
                    sections[sec_count].start_sec = last_boundary;
                    sections[sec_count].end_sec   = time_sec;
                    snprintf(sections[sec_count].label, sizeof(sections[sec_count].label),
                             "segment_%zu", sec_count+1);
                    sec_count++;
                    last_boundary = time_sec;
                }
            }
        }
    }

    // add final section
    if (sec_count < max_sections) {
        sections[sec_count].start_sec = last_boundary;
        sections[sec_count].end_sec   = duration_sec;
        snprintf(sections[sec_count].label, sizeof(sections[sec_count].label),
                 "segment_%zu", sec_count+1);
        sec_count++;
    }

    free(novelty);

    // --- Compute lengths ---
    double* lengths = (double*)calloc(sec_count, sizeof(double));
    size_t longest_idx = 0;
    double max_len = 0.0;
    for (size_t i=0; i<sec_count; i++) {
        lengths[i] = sections[i].end_sec - sections[i].start_sec;
        if (lengths[i] > max_len) {
            max_len = lengths[i];
            longest_idx = i;
        }
    }

    // --- Assign heuristics (work on local sections, NOT out->sections) ---
    for (size_t i=0; i<sec_count; i++) {
        if (i == 0) {
            strcpy(sections[i].label, (lengths[i] < 40.0 ? "intro" : "verse"));
        }
        else if (i == sec_count-1) {
            strcpy(sections[i].label, (lengths[i] < 30.0 ? "outro" : "verse"));
        }
        else if (i == longest_idx) {
            strcpy(sections[i].label, "chorus");
        }
        else if (sec_count >= 4 && i == sec_count/2) {
            strcpy(sections[i].label, "bridge");
        }
        else {
            strcpy(sections[i].label, "verse");
        }
    }

    free(lengths);

    // --- Copy into out->sections ---
    out->sections = (Section*)calloc(sec_count, sizeof(Section));
    if (!out->sections) { free(sections); return 4; }
    memcpy(out->sections, sections, sec_count * sizeof(Section));
    out->section_count = sec_count;

    free(sections);

    // --- Arc complexity (entropy of section lengths) ---
    double total = duration_sec;
    double entropy = 0.0;
    for (size_t i=0; i<sec_count; i++) {
        double len = out->sections[i].end_sec - out->sections[i].start_sec;
        double p = len / total;
        if (p > 1e-6) entropy -= p * log(p);
    }
    out->arc_complexity = entropy / log(sec_count > 1 ? (double)sec_count : 2.0);

    // --- Inside compute_structure_features, after segmentation + arc complexity ---
    int mfcc_dim = FEATURE_MFCC_COUNT;
    double** mfcc_means = (double**)calloc(sec_count, sizeof(double*));

    for (size_t i=0; i<sec_count; i++) {
        mfcc_means[i] = (double*)calloc(mfcc_dim, sizeof(double));

        // extract average MFCC for section
        double start = out->sections[i].start_sec;
        double end   = out->sections[i].end_sec;

        // simple approach: reuse compute_spectral_features over section slice
        size_t start_idx = (size_t)(start * sample_rate);
        size_t end_idx   = (size_t)(end * sample_rate);
        if (end_idx > frames) end_idx = frames;

        SpectralFeatures feat;
        if (compute_spectral_features(mono + start_idx, end_idx - start_idx,
                                    sample_rate, &feat) == 0) {
            for (int k=0; k<mfcc_dim; k++) {
                mfcc_means[i][k] = feat.mfcc[k];
            }
        }
    }

    // --- Repetition ratio ---
    // Compute descriptor per section
    double repeated_time = 0.0;
    for (size_t i=0; i<sec_count; i++) {
        double len_i = out->sections[i].end_sec - out->sections[i].start_sec;
        for (size_t j=i+1; j<sec_count; j++) {
            double sim = cosine_similarity(mfcc_means[i], mfcc_means[j], mfcc_dim);
            if (sim > 0.85) { // threshold for repetition
                repeated_time += fmin(len_i,
                                    (out->sections[j].end_sec - out->sections[j].start_sec));
            }
        }
    }
    if (total > 1e-6)
        out->repetition_ratio = fmin(1.0, repeated_time / total);
    else
        out->repetition_ratio = 0.0;
    

    for (size_t i=0; i<sec_count; i++) {
        free(mfcc_means[i]);
    }
    free(mfcc_means);

    return 0;
}

void free_structure_features(StructureFeatures* sf)
{
    if (!sf) return;
    if (sf->sections) {
        free(sf->sections);
        sf->sections = NULL;
    }
    sf->section_count = 0;
}