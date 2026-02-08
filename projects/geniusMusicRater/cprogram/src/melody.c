/* src/melody.c
 *
 * Compact, self-contained melody extraction module.
 * - YIN-based pitch tracking (time-domain)
 * - Median smoothing
 * - Contour segmentation
 * - Simple motif counting (n-gram of rounded MIDI pitches)
 *
 * Designed for clarity and incremental testing, not for perfect polyphonic transcription.
 */

#include "melody.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Parameters you can tune */
#define MELODY_FRAME_SIZE      2048
#define MELODY_HOP             512
#define YIN_THRESHOLD          0.12    /* smaller = more strict voiced decision */
#define YIN_FMIN              80.0     /* Hz */
#define YIN_FMAX             1200.0    /* Hz */
#define MEDIAN_WINDOW         7        /* was 5, increased */
#define MOTIF_N               4        /* motif length in notes */

/* small helpers */
static double safe_log2(double x) { return log(x) / log(2.0); }

/* qsort comparator */
static int cmp_double(const void* a, const void* b) {
    double aa = *(const double*)a;
    double bb = *(const double*)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

/* compute a Hann window (in-place) */
static void fill_hann(float* w, int N) {
    for (int i = 0; i < N; ++i) {
        w[i] = 0.5f - 0.5f * (float)cos(2.0 * M_PI * (double)i / (double)(N - 1));
    }
}

/* median filter on double array (simple, small window) */
static void median_filter(const double* in, double* out, int n, int win) {
    if (win <= 1) {
        memcpy(out, in, sizeof(double) * n);
        return;
    }
    int half = win / 2;
    double* buf = (double*)malloc(sizeof(double) * win);
    for (int i = 0; i < n; ++i) {
        int a = i - half;
        int b = i + half;
        if (a < 0) a = 0;
        if (b >= n) b = n - 1;
        int m = 0;
        for (int j = a; j <= b; ++j) buf[m++] = in[j];
        qsort(buf, m, sizeof(double), cmp_double);
        out[i] = buf[m/2];
    }
    free(buf);
}

/* YIN core: returns frequency in Hz (0 if unvoiced). also returns confidence via out_conf (0..1) */
static double yin_get_pitch(const float* frame, int N,
                            int sr, double fmin, double fmax,
                            double* out_confidence) {
    if (!frame || N < 32) {
        if (out_confidence) *out_confidence = 0.0;
        return 0.0;
    }

    int max_tau = (int)(sr / fmin);
    if (max_tau > N - 2) max_tau = N - 2;
    int min_tau = (int)(sr / fmax);
    if (min_tau < 2) min_tau = 2;
    int range = max_tau + 1;

    double* d = (double*)calloc(range, sizeof(double));
    double* cmnd = (double*)calloc(range, sizeof(double));
    if (!d || !cmnd) { free(d); free(cmnd); if (out_confidence) *out_confidence = 0.0; return 0.0; }

    /* difference function d(tau) for tau = 1..max_tau */
    for (int tau = 1; tau <= max_tau; ++tau) {
        double sum = 0.0;
        int limit = N - tau;
        for (int j = 0; j < limit; ++j) {
            double diff = (double)frame[j] - (double)frame[j + tau];
            sum += diff * diff;
        }
        d[tau] = sum;
    }

    /* cumulative mean normalized difference function */
    double running = 0.0;
    for (int tau = 1; tau <= max_tau; ++tau) {
        running += d[tau];
        if (running == 0.0) cmnd[tau] = 1.0;
        else cmnd[tau] = d[tau] * ((double)tau / running);
    }

    /* find best tau: first dip below threshold */
    int tau_est = -1;
    for (int tau = min_tau; tau <= max_tau; ++tau) {
        if (cmnd[tau] < YIN_THRESHOLD) {
            /* refine to local minimum */
            while (tau + 1 <= max_tau && cmnd[tau + 1] < cmnd[tau]) tau++;
            tau_est = tau;
            break;
        }
    }
    if (tau_est == -1) {
        /* fallback: use global minimum */
        double minv = 1e300;
        int mint = -1;
        for (int tau = min_tau; tau <= max_tau; ++tau) {
            if (cmnd[tau] < minv) { minv = cmnd[tau]; mint = tau; }
        }
        /* if the minimum is too high, treat as unvoiced */
        if (minv > 0.45) {
            free(d); free(cmnd);
            if (out_confidence) *out_confidence = 0.0;
            return 0.0;
        }
        tau_est = mint;
    }

    /* parabolic interpolation around tau_est to refine */
    double better_tau = (double)tau_est;
    if (tau_est > 1 && tau_est < max_tau) {
        double x0 = cmnd[tau_est - 1];
        double x1 = cmnd[tau_est];
        double x2 = cmnd[tau_est + 1];
        double denom = (2.0 * x1 - x0 - x2);
        if (fabs(denom) > 1e-12) {
            better_tau = (double)tau_est + (x0 - x2) / (2.0 * denom);
            if (better_tau < 1.0) better_tau = (double)tau_est;
        }
    }

    double freq = (double)sr / better_tau;
    double confidence = 1.0 - cmnd[tau_est];
    if (confidence < 0.0) confidence = 0.0;
    if (confidence > 1.0) confidence = 1.0;

    free(d); free(cmnd);
    if (out_confidence) *out_confidence = confidence;
    return freq;
}

int compute_melody_features(const float* mono,
                            size_t frames,
                            int sample_rate,
                            MelodyFeatures* out) {
    if (!mono || frames == 0 || sample_rate <= 0 || !out) return 1;

    /* zero-out out initially */
    memset(out, 0, sizeof(MelodyFeatures));

    const int frame_size = MELODY_FRAME_SIZE;
    const int hop = MELODY_HOP;

    if (frames < (size_t)frame_size) {
        /* too short -> nothing to do, return success but features zero */
        return 0;
    }

    int n_frames = (int)((frames - frame_size) / hop) + 1;
    double* f0 = (double*)calloc(n_frames, sizeof(double));
    double* conf = (double*)calloc(n_frames, sizeof(double));
    double* frame_energy = (double*)calloc(n_frames, sizeof(double));
    double* f0_smoothed = (double*)calloc(n_frames, sizeof(double));
    if (!f0 || !conf || !frame_energy || !f0_smoothed) {
        free(f0); free(conf); free(frame_energy); free(f0_smoothed);
        return 1;
    }

    float* window = (float*)malloc(sizeof(float) * frame_size);
    float* frame_buf = (float*)malloc(sizeof(float) * frame_size);
    if (!window || !frame_buf) { free(window); free(frame_buf); free(f0); free(conf); free(frame_energy); free(f0_smoothed); return 1; }
    fill_hann(window, frame_size);

    /* compute frame-wise pitch and energy */
    for (int i = 0; i < n_frames; ++i) {
        size_t start = (size_t)i * hop;
        double esum = 0.0;
        for (int j = 0; j < frame_size; ++j) {
            float s = mono[start + j] * window[j];
            frame_buf[j] = s;
            esum += (double)s * (double)s;
        }
        frame_energy[i] = sqrt(esum / (double)frame_size);
        double c;
        double pitch = yin_get_pitch(frame_buf, frame_size, sample_rate, YIN_FMIN, YIN_FMAX, &c);
        f0[i] = pitch;
        conf[i] = c;
    }

    /* median smoothing of f0 */
    median_filter(f0, f0_smoothed, n_frames, MEDIAN_WINDOW);

    /* decide voiced frames using conf and smoothed f0 */
    int voiced_count = 0;
    double sum_f0 = 0.0;
    double sum_f0_for_median = 0.0;
    int median_list_size = 0;
    /* for median compute we will collect voiced f0s */
    double* voiced_f0_list = (double*)malloc(sizeof(double) * n_frames);

    const double F0_CONF_THRESH = 0.05; // added for accuracy

    for (int i = 0; i < n_frames; ++i) {
        if (f0_smoothed[i] > 0.0 && conf[i] >= F0_CONF_THRESH) { /* lower conf threshold after smoothing */
            voiced_f0_list[median_list_size++] = f0_smoothed[i];
            sum_f0 += f0_smoothed[i];
            voiced_count++;
        }
    }

    /* fill basic statistics */
    out->f0_confidence = (double)voiced_count / (double)n_frames;
    if (voiced_count == 0) {
        /* no voiced material; leave others as 0 and return success */
        free(f0); free(conf); free(frame_energy); free(f0_smoothed);
        free(window); free(frame_buf); free(voiced_f0_list);
        return 0;
    }

    /* median and mean f0 */
    qsort(voiced_f0_list, median_list_size, sizeof(double), cmp_double);
    if (median_list_size > 0) {
        if (median_list_size % 2 == 1) out->median_f0 = voiced_f0_list[median_list_size/2];
        else out->median_f0 = 0.5 * (voiced_f0_list[median_list_size/2 - 1] + voiced_f0_list[median_list_size/2]);
    }
    out->mean_f0 = sum_f0 / (double)voiced_count;

    /* compute midi numbers per voiced frame (rounded) and intervals */
    int prev_midi = -1;
    double interval_sum = 0.0;
    double abs_interval_sum = 0.0;
    int interval_count = 0;

    /* We'll also produce contours and motif extraction */
    int contour_cnt = 0;
    double total_contour_len = 0.0;
    double longest_contour = 0.0;

    /* For motif extraction we'll gather rounded MIDI sequence across contiguous voiced frames */
    int *midi_seq = (int*)malloc(sizeof(int) * n_frames);
    int midi_seq_len = 0;

    for (int i = 0; i < n_frames; ++i) {
        double p = f0_smoothed[i];
        if (p <= 0.0 || conf[i] < 0.18) {
            /* break contour */
            if (midi_seq_len > 0) {
                /* finish contour */
                double len_sec = (double)midi_seq_len * (double)hop / (double)sample_rate;
                contour_cnt++;
                total_contour_len += len_sec;
                if (len_sec > longest_contour) longest_contour = len_sec;
                midi_seq_len = 0;
            }
            prev_midi = -1;
            continue;
        }
        /* compute midi */
        double midi_f = 69.0 + 12.0 * safe_log2(p / 440.0);
        int midi_r = (int)floor(midi_f + 0.5);
        if (midi_r < 0) midi_r = 0;
        if (midi_r > 127) midi_r = 127;

        /* intervals */
        if (prev_midi >= 0) {
            double interval = (double)(midi_r - prev_midi); /* semitone difference */
            interval_sum += interval;
            abs_interval_sum += fabs(interval);
            interval_count++;
        }
        prev_midi = midi_r;

        /* append to current contour sequence */
        midi_seq[midi_seq_len++] = midi_r;
    }
    /* if ended in voiced contour */
    if (midi_seq_len > 0) {
        double len_sec = (double)midi_seq_len * (double)hop / (double)sample_rate;
        contour_cnt++;
        total_contour_len += len_sec;
        if (len_sec > longest_contour) longest_contour = len_sec;
        /* leave midi_seq_len as last contour len; we will re-scan contiguous voiced frames for motif extraction below */
    }

    /* Re-scan to generate motif n-grams across ALL voiced-note sequences (non-overlapping contours allowed across boundaries) */
    /* We'll create a simple array of motif entries (key,count) */
    typedef struct { uint64_t key; int count; } Motif;
    Motif *motifs = NULL;
    int motifs_alloc = 0;
    int motifs_used = 0;
    int total_motif_occurrences = 0;

    /* Build a single vector of midi notes (consecutive voiced frames) so motifs can cross short contours if you like.
     * For simplicity we'll create contiguous sequences of rounded MIDI across all voiced frames (skipping unvoiced) */
    int *all_midi = (int*)malloc(sizeof(int) * n_frames);
    int all_midi_len = 0;
    prev_midi = -1;
    for (int i = 0; i < n_frames; ++i) {
        double p = f0_smoothed[i];
        if (p <= 0.0 || conf[i] < 0.18) {
            prev_midi = -1;
            continue;
        }
        double midi_f = 69.0 + 12.0 * safe_log2(p / 440.0);
        int midi_r = (int)floor(midi_f + 0.5);
        if (midi_r < 0) midi_r = 0;
        if (midi_r > 127) midi_r = 127;
        /* append */
        all_midi[all_midi_len++] = midi_r;
    }

    /* sliding n-gram motifs */
    for (int i = 0; i + MOTIF_N <= all_midi_len; ++i) {
        uint64_t key = 0;
        for (int k = 0; k < MOTIF_N; ++k) {
            key = (key << 8) | (uint64_t)(all_midi[i + k] & 0xFF);
        }
        /* find in motifs */
        int found = -1;
        for (int m = 0; m < motifs_used; ++m) {
            if (motifs[m].key == key) { found = m; break; }
        }
        if (found >= 0) {
            motifs[found].count++;
        } else {
            if (motifs_used >= motifs_alloc) {
                int newcap = motifs_alloc ? motifs_alloc * 2 : 64;
                Motif* tmp = (Motif*)realloc(motifs, sizeof(Motif) * newcap);
                if (!tmp) break; /* out of memory -> stop adding */
                motifs = tmp;
                motifs_alloc = newcap;
            }
            motifs[motifs_used].key = key;
            motifs[motifs_used].count = 1;
            motifs_used++;
        }
        total_motif_occurrences++;
    }

    /* compute motif repetition rate */
    int repeated_occurrences = 0;
    for (int m = 0; m < motifs_used; ++m) {
        if (motifs[m].count > 1) repeated_occurrences += (motifs[m].count - 1);
    }
    double motif_rep_rate = 0.0;
    if (total_motif_occurrences > 0) motif_rep_rate = (double)repeated_occurrences / (double)total_motif_occurrences;

    /* energy normalization: find median energy across frames and compute average voiced energy relative to it */
    double *eng_copy = (double*)malloc(sizeof(double) * n_frames);
    int ec = 0;
    for (int i = 0; i < n_frames; ++i) eng_copy[ec++] = frame_energy[i];
    qsort(eng_copy, ec, sizeof(double), cmp_double);
    double median_eng = eng_copy[ec/2];
    free(eng_copy);

    double voiced_eng_sum = 0.0;
    for (int i = 0; i < n_frames; ++i) {
        if (f0_smoothed[i] > 0.0 && conf[i] >= 0.18) voiced_eng_sum += frame_energy[i];
    }
    double avg_voiced_energy = voiced_eng_sum / (double)voiced_count;
    double energy_factor = (median_eng > 0.0) ? (avg_voiced_energy / median_eng) : 1.0;
    if (energy_factor < 0.0) energy_factor = 0.0;

    /* melodic entropy: histogram over MIDI notes */
    int bins = 128;
    int* hist = (int*)calloc(bins, sizeof(int));
    int hist_total = 0;
    for (int i = 0; i < n_frames; ++i) {
        double p = f0_smoothed[i];
        if (p <= 0.0 || conf[i] < 0.18) continue;
        double midi_f = 69.0 + 12.0 * safe_log2(p / 440.0);
        int midi_r = (int)floor(midi_f + 0.5);
        if (midi_r < 0) midi_r = 0;
        if (midi_r > 127) midi_r = 127;
        hist[midi_r]++;
        hist_total++;
    }
    double entropy = 0.0;
    if (hist_total > 0) {
        for (int b = 0; b < bins; ++b) {
            if (hist[b] == 0) continue;
            double p = (double)hist[b] / (double)hist_total;
            entropy -= p * safe_log2(p);
        }
        /* normalize by log2(number_of_nonzero_bins) or log2(bins) - choose bins for stable result */
        double max_entropy = safe_log2((double)bins);
        if (max_entropy > 0.0) entropy /= max_entropy;
    }
    free(hist);

    /* interval stats */
    double avg_int = 0.0, avg_abs_int = 0.0;
    if (interval_count > 0) {
        avg_int = interval_sum / (double)interval_count;
        avg_abs_int = abs_interval_sum / (double)interval_count;
    }

    /* pitch range estimation: find voiced min and max midi */
    int min_m = 127, max_m = 0;
    for (int i = 0; i < n_frames; ++i) {
        double p = f0_smoothed[i];
        if (p <= 0.0 || conf[i] < 0.18) continue;
        double midi_f = 69.0 + 12.0 * safe_log2(p / 440.0);
        int midi_r = (int)floor(midi_f + 0.5);
        if (midi_r < 0) midi_r = 0;
        if (midi_r > 127) midi_r = 127;
        if (midi_r < min_m) min_m = midi_r;
        if (midi_r > max_m) max_m = midi_r;
    }
    double pitch_range = 0.0;
    if (max_m >= min_m) pitch_range = (double)(max_m - min_m);

    /* hook strength heuristic: repetition * normalized length factor * normalized energy */
    double avg_contour_len = (contour_cnt > 0) ? (total_contour_len / (double)contour_cnt) : 0.0;
    double length_factor = avg_contour_len / 4.0; /* 4s -> 1.0 baseline */
    if (length_factor > 1.0) length_factor = 1.0;
    double hook_strength = motif_rep_rate * length_factor * (0.5 + 0.5 * (energy_factor > 1.0 ? 1.0 : energy_factor));

    /* fill outputs */
    out->pitch_range_semitones = pitch_range;
    out->contour_count = contour_cnt;
    out->avg_contour_length_sec = (contour_cnt > 0) ? (total_contour_len / (double)contour_cnt) : 0.0;
    out->longest_contour_sec = longest_contour;
    out->avg_interval_semitones = avg_int;
    out->avg_abs_interval_semitones = avg_abs_int;
    out->melodic_entropy = entropy;
    out->motif_repetition_rate = motif_rep_rate;
    out->motif_count = motifs_used;
    out->hook_strength = hook_strength;

    /* cleanup */
    free(f0);
    free(conf);
    free(frame_energy);
    free(f0_smoothed);
    free(window);
    free(frame_buf);
    free(voiced_f0_list);
    free(midi_seq);
    free(all_midi);
    if (motifs) free(motifs);
    /* eng_copy already freed earlier; safe guard (compiler can optimize) */

    return 0;
}
