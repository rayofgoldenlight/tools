#include "feature_extractor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------- Utility: simple complex type and radix-2 iterative FFT ----------

typedef struct { double r, i; } cpx;

static void bit_reverse(cpx* a, size_t n) {
    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i < j) {
            cpx t = a[i]; a[i] = a[j]; a[j] = t;
        }
        size_t m = n >> 1;
        while (m && j >= m) {
            j -= m;
            m >>= 1;
        }
        j += m;
    }
}

static void fft(cpx* a, size_t n) {
    bit_reverse(a, n);
    for (size_t s = 1; (size_t)1 << s <= n; ++s) {
        size_t m = (size_t)1 << s;
        double theta = -2.0 * M_PI / (double)m;
        cpx wm = { cos(theta), sin(theta) };
        for (size_t k = 0; k < n; k += m) {
            cpx w = {1.0, 0.0};
            for (size_t j = 0; j < m/2; ++j) {
                cpx t = { w.r * a[k + j + m/2].r - w.i * a[k + j + m/2].i,
                          w.r * a[k + j + m/2].i + w.i * a[k + j + m/2].r };
                cpx u = a[k + j];
                a[k + j].r       = u.r + t.r;
                a[k + j].i       = u.i + t.i;
                a[k + j + m/2].r = u.r - t.r;
                a[k + j + m/2].i = u.i - t.i;
                cpx wnext = { w.r * wm.r - w.i * wm.i, w.r * wm.i + w.i * wm.r };
                w = wnext;
            }
        }
    }
}

// ---------- Windowing and framing helpers ----------

static void hann(double* w, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        w[i] = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n)));
    }
}

static size_t compute_num_frames(size_t total, size_t frame, size_t hop) {
    if (total == 0) return 0;
    if (total <= frame) return 1;
    return 1 + (total - frame) / hop;
}

// ---------- Mel filterbank and DCT for MFCC ----------

static double hz_to_mel(double f) {
    return 2595.0 * log10(1.0 + f / 700.0);
}
static double mel_to_hz(double m) {
    return 700.0 * (pow(10.0, m / 2595.0) - 1.0);
}

typedef struct {
    int n_filters;
    int n_fft;
    int sr;
    double* weights; // size n_filters * (n_fft/2+1)
} MelFB;

// ---------- Mel Filterbank + MFCC ----------

static MelFB* mel_filterbank(int sr, int n_fft, int n_filters, double fmin, double fmax) {
    MelFB* fb = (MelFB*)calloc(1, sizeof(MelFB));
    if (!fb) return NULL;
    fb->n_filters = n_filters;
    fb->n_fft = n_fft;
    fb->sr = sr;
    fb->weights = (double*)calloc((size_t)n_filters * (n_fft/2+1), sizeof(double));
    if (!fb->weights) { free(fb); return NULL; }

    double mel_min = hz_to_mel(fmin);
    double mel_max = hz_to_mel(fmax);
    double mel_step = (mel_max - mel_min) / (n_filters + 1);

    double* mel_points = (double*)malloc((n_filters+2) * sizeof(double));
    int* bins = (int*)malloc((n_filters+2) * sizeof(int));

    for (int i=0; i<n_filters+2; ++i) {
        mel_points[i] = mel_to_hz(mel_min + mel_step * i);
        bins[i] = (int)floor((n_fft+1) * mel_points[i] / sr);
    }

    for (int m=1; m <= n_filters; ++m) {
        int f_m_minus = bins[m-1];
        int f_m = bins[m];
        int f_m_plus = bins[m+1];
        for (int k=f_m_minus; k<f_m; ++k) {
            if (k>=0 && k<=n_fft/2)
                fb->weights[(m-1)*(n_fft/2+1) + k] = (double)(k - f_m_minus) / (f_m - f_m_minus);
        }
        for (int k=f_m; k<f_m_plus; ++k) {
            if (k>=0 && k<=n_fft/2)
                fb->weights[(m-1)*(n_fft/2+1) + k] = (double)(f_m_plus - k) / (f_m_plus - f_m);
        }
    }

    free(mel_points); free(bins);
    return fb;
}

static void free_melfb(MelFB* fb) {
    if (!fb) return;
    free(fb->weights);
    free(fb);
}

// Discrete Cosine Transform (DCT-II) for MFCC
static void dct(double* in, int n_in, double* out, int n_out) {
    for (int k=0; k<n_out; ++k) {
        double sum = 0.0;
        for (int n=0; n<n_in; ++n) {
            sum += in[n] * cos(M_PI / n_in * (n + 0.5) * k);
        }
        out[k] = sum;
    }
}

// ---------------- Spectral Feature Computations -----------------

static void spectral_features_from_frame(
    const double* mag, int nfft, int sr,
    double* centroid, double* rolloff, double* brightness)
{
    int n = nfft/2 + 1;

    // Use power spectrum for all three to keep them consistent
    double energy_tot = 0.0;
    double weighted_sum = 0.0;

    for (int k = 0; k < n; ++k) {
        double f = (double)k * sr / (double)nfft;
        double p = mag[k] * mag[k]; // power
        energy_tot += p;
        weighted_sum += f * p;
    }

    *centroid = (energy_tot > 1e-12) ? (weighted_sum / energy_tot) : 0.0;

    // 85% rolloff
    double target = 0.85 * energy_tot;
    double acc = 0.0;
    *rolloff = 0.0;
    for (int k = 0; k < n; ++k) {
        double p = mag[k] * mag[k];
        acc += p;
        if (acc >= target) {
            *rolloff = (double)k * sr / (double)nfft;
            break;
        }
    }

    // Brightness: fraction of energy above 1500 Hz
    double bright_energy = 0.0;
    for (int k = 0; k < n; ++k) {
        double f = (double)k * sr / (double)nfft;
        double p = mag[k] * mag[k];
        if (f >= 1500.0) bright_energy += p;
    }
    *brightness = (energy_tot > 1e-12) ? (bright_energy / energy_tot) : 0.0;
}

// ----------------- Public API Implementations --------------------

int compute_spectral_features(const float* mono, size_t frames, int sr, SpectralFeatures* out) {
    if (!mono || frames == 0 || sr <= 0 || !out) return -1;

    // Analysis parameters
    int n_fft = 1024;
    int hop = n_fft/2;
    int n_filters = 26;

    // Window
    double* window = (double*)malloc(n_fft * sizeof(double));
    hann(window, n_fft);

    // Mel filterbank
    MelFB* fb = mel_filterbank(sr, n_fft, n_filters, 0.0, sr/2.0);
    if (!fb) { free(window); return -2; }

    // Accumulators
    double centroid_sum = 0.0, rolloff_sum = 0.0, bright_sum = 0.0;
    double mfcc_acc[FEATURE_MFCC_COUNT];
    for(int i=0;i<FEATURE_MFCC_COUNT;i++) mfcc_acc[i]=0.0;
    int n_frames = 0;

    // Frame loop
    size_t num_frames = compute_num_frames(frames, n_fft, hop);
    for (size_t fi=0; fi<num_frames; ++fi) {
        size_t offset = fi * hop;
        if (offset + n_fft > frames) break;

        // Copy + window
        cpx* X = (cpx*)calloc(n_fft, sizeof(cpx));
        for (int i=0; i<n_fft; ++i) {
            double s = (double)mono[offset+i] * window[i];
            X[i].r = s; X[i].i=0.0;
        }
        fft(X, n_fft);

        // magnitude spectrum
        int n_bins = n_fft/2+1;
        double* mag = (double*)malloc(n_bins*sizeof(double));
        for (int k=0; k<n_bins; ++k) {
            mag[k] = sqrt(X[k].r*X[k].r + X[k].i*X[k].i);
        }

        // spectral feats
        double c, r, b; 
        spectral_features_from_frame(mag, n_fft, sr, &c, &r, &b);
        centroid_sum += c; rolloff_sum += r; bright_sum += b;

        // mel energies
        double* melE = (double*)calloc(n_filters, sizeof(double));
        for (int m=0; m<n_filters; ++m) {
            double e=0.0;
            for (int k=0; k<n_bins; ++k)
                e += mag[k]*mag[k] * fb->weights[m*n_bins + k];
            melE[m] = log(e+1e-9);
        }

        // DCT
        double mfcc_frame[FEATURE_MFCC_COUNT];
        dct(melE, n_filters, mfcc_frame, FEATURE_MFCC_COUNT);
        for (int i=0; i<FEATURE_MFCC_COUNT; ++i)
            mfcc_acc[i] += mfcc_frame[i];

        free(melE);
        free(mag);
        free(X);
        n_frames++;
    }

    if (n_frames==0) { 
        free(window); free_melfb(fb); 
        return -3; 
    }

    // Average
    out->centroid   = centroid_sum / n_frames;
    out->rolloff    = rolloff_sum / n_frames;
    out->brightness = bright_sum / n_frames;
    for (int i=0; i<FEATURE_MFCC_COUNT; ++i)
        out->mfcc[i] = mfcc_acc[i] / n_frames;

    free(window);
    free_melfb(fb);
    return 0;
}

// ---------------- Tempo Estimation -----------------

static void compute_onset_envelope(const float* mono, size_t frames, int sr,
                                   double** out_env, int* out_len, int hop, int n_fft) {
    int n_bins = n_fft/2 + 1;
    int num_frames = (int)compute_num_frames(frames, n_fft, hop);
    double* window = (double*)malloc(n_fft * sizeof(double));
    hann(window, n_fft);

    double* env = (double*)calloc(num_frames, sizeof(double));
    double* prev_mag = (double*)calloc(n_bins, sizeof(double));

    for (int fi=0; fi<num_frames; ++fi) {
        size_t offset = (size_t)fi * hop;
        if (offset + n_fft > frames) break;

        // FFT
        cpx* X = (cpx*)calloc(n_fft, sizeof(cpx));
        for (int i=0;i<n_fft;i++) {
            double s = (double)mono[offset+i] * window[i];
            X[i].r = s; X[i].i = 0.0;
        }
        fft(X, n_fft);

        // spectral flux
        double flux = 0.0;
        for (int k=0; k<n_bins; ++k) {
            double mag = sqrt(X[k].r*X[k].r + X[k].i*X[k].i);
            double diff = mag - prev_mag[k];
            if (diff > 0) flux += diff;
            prev_mag[k] = mag;
        }
        env[fi] = flux;

        free(X);
    }

    free(window); free(prev_mag);
    *out_env = env;
    *out_len = num_frames;
}

static void autocorrelate(const double* x, int n, double* ac) {
    for (int lag=0; lag<n; ++lag) {
        double sum=0.0;
        for (int i=0;i<n-lag;i++) sum += x[i]*x[i+lag];
        ac[lag]=sum;
    }
}

int estimate_tempo_bpm(const float* mono, size_t frames, int sr, double* out_bpm) {
    if (!mono || frames==0 || sr<=0 || !out_bpm) return -1;

    int n_fft = 1024;
    int hop = n_fft/2;
    double* env = NULL; int env_len=0;
    compute_onset_envelope(mono, frames, sr, &env, &env_len, hop, n_fft);
    if (!env || env_len<4) { free(env); *out_bpm=0.0; return -2; }

    // Autocorrelation
    double* ac = (double*)calloc(env_len, sizeof(double));
    autocorrelate(env, env_len, ac);

    // Search best peak in lag range corresponding to 40–200 BPM
    double best_val=0.0; int best_lag=0;
    double min_period_sec = 60.0/200.0;
    double max_period_sec = 60.0/40.0;
    double hop_time = (double)hop / sr;

    int min_lag = (int)floor(min_period_sec / hop_time);
    int max_lag = (int)ceil (max_period_sec / hop_time);
    if (max_lag >= env_len) max_lag = env_len-1;

    for (int lag=min_lag; lag<=max_lag; ++lag) {
        if (ac[lag] > best_val) {
            best_val = ac[lag];
            best_lag = lag;
        }
    }

    if (best_lag>0) {
        double period_sec = best_lag * hop_time;
        *out_bpm = 60.0 / period_sec;
    } else {
        *out_bpm = 0.0;
    }

    free(env); free(ac);
    return 0;
}

// ---------------- Key Estimation -----------------

// Krumhansl & Kessler (1982) key profiles for major and minor
static const double KK_major[12] = {6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
static const double KK_minor[12] = {6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17};

static void compute_chroma(const float* mono, size_t frames, int sr,
                           double* out_chroma) {
    int n_fft = 4096;
    int hop = n_fft/2;
    int n_bins = n_fft/2+1;

    double* window = (double*)malloc(n_fft * sizeof(double));
    hann(window, n_fft);

    size_t num_frames = compute_num_frames(frames, n_fft, hop);
    double chroma_acc[12]; memset(chroma_acc, 0, sizeof(chroma_acc));

    for (size_t fi=0; fi<num_frames; ++fi) {
        size_t offset = fi*hop;
        if (offset + n_fft > frames) break;

        // FFT
        cpx* X = (cpx*)calloc(n_fft, sizeof(cpx));
        for (int i=0;i<n_fft;i++) {
            double s = (double)mono[offset+i]*window[i];
            X[i].r = s; X[i].i=0.0;
        }
        fft(X,n_fft);

        // Energy per pitch class
        for (int k=1;k<n_bins;k++) {
            double freq = (double)k*sr/n_fft;
            if (freq < 50.0 || freq > 5000.0) continue;
            double mag2 = X[k].r*X[k].r + X[k].i*X[k].i;
            // MIDI pitch
            double midi = 69.0 + 12.0*log2(freq/440.0);
            int pc = ((int)round(midi)) % 12;
            if (pc<0) pc+=12;
            chroma_acc[pc] += mag2;
        }

        free(X);
    }
    free(window);

    // Normalize
    double sum=0; for(int i=0;i<12;i++) sum+=chroma_acc[i];
    if (sum>1e-9) { for(int i=0;i<12;i++) out_chroma[i]=chroma_acc[i]/sum; }
    else { for(int i=0;i<12;i++) out_chroma[i]=0.0; }
}

int estimate_key(const float* mono, size_t frames, int sr, char out_key[8]) {
    if (!mono || frames==0 || sr<=0 || !out_key) return -1;

    double chroma[12];
    compute_chroma(mono,frames,sr,chroma);

    // Try all 12 rotations
    const char* names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    double best_corr=-1e9;
    int best_index=0; int best_is_major=1;

    for (int tonic=0; tonic<12; ++tonic) {
        // Major
        double sum_xy=0,sum_x2=0,sum_y2=0;
        for(int i=0;i<12;i++){
            double x=chroma[(i+tonic)%12];
            double y=KK_major[i];
            sum_xy+=x*y; sum_x2+=x*x; sum_y2+=y*y;
        }
        double corr = sum_xy / (sqrt(sum_x2*sum_y2)+1e-9);
        if (corr>best_corr) {best_corr=corr; best_index=tonic; best_is_major=1;}

        // Minor
        sum_xy=0; sum_x2=0; sum_y2=0;
        for(int i=0;i<12;i++){
            double x=chroma[(i+tonic)%12];
            double y=KK_minor[i];
            sum_xy+=x*y; sum_x2+=x*x; sum_y2+=y*y;
        }
        corr = sum_xy / (sqrt(sum_x2*sum_y2)+1e-9);
        if (corr>best_corr) {best_corr=corr; best_index=tonic; best_is_major=0;}
    }

    snprintf(out_key,8,"%s %s",names[best_index], best_is_major?"maj":"min");
    return 0;
}