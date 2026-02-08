#include "production.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- small FFT wrapper (Cooley-Tukey radix-2) ---
typedef struct { double r, i; } Complex;
static void fft(Complex* buf, int n) {
    // Bit reversal
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i < j) { Complex tmp = buf[i]; buf[i] = buf[j]; buf[j] = tmp; }
        int m = n >> 1;
        while (j >= m && m >= 2) { j -= m; m >>= 1; }
        j += m;
    }
    // Danielson-Lanczos
    for (int m = 2; m <= n; m <<= 1) {
        double theta = -2.0 * M_PI / m;
        Complex wm = { cos(theta), sin(theta) };
        for (int k = 0; k < n; k += m) {
            Complex w = {1.0, 0.0};
            for (int j = 0; j < m/2; ++j) {
                Complex t = {
                    w.r * buf[k+j+m/2].r - w.i * buf[k+j+m/2].i,
                    w.r * buf[k+j+m/2].i + w.i * buf[k+j+m/2].r
                };
                Complex u = buf[k+j];
                buf[k+j].r        = u.r + t.r;
                buf[k+j].i        = u.i + t.i;
                buf[k+j+m/2].r    = u.r - t.r;
                buf[k+j+m/2].i    = u.i - t.i;
                // update twiddle
                double wr = w.r * wm.r - w.i * wm.i;
                double wi = w.r * wm.i + w.i * wm.r;
                w.r = wr; w.i = wi;
            }
        }
    }
}

int compute_production_features(const float* stereo, size_t frames, int sample_rate, int channels, ProductionFeatures* out) {
    if (!stereo || frames == 0 || sample_rate <= 0 || !out) return -1;
    memset(out, 0, sizeof(*out));

    // --- RMS + Peak ---
    double sumsq = 0.0, peak = 0.0;
    size_t total_samples = frames * channels;
    for (size_t i=0; i<total_samples; i++) {
        double x = (double)stereo[i];
        sumsq += x*x;
        if (fabs(x) > peak) peak = fabs(x);
    }
    double rms = (total_samples? sqrt(sumsq/total_samples) : 0.0);
    out->loudness_db = (rms>1e-12 ? 20.0*log10(rms) : -120.0);
    out->dynamic_range_db = (rms>1e-12 && peak>1e-12? 20.0*log10(peak/rms) : 0.0);

    // --- Stereo Width ---
    if (channels >= 2) {
        double sumL=0,sumR=0,sumL2=0,sumR2=0,sumLR=0;
        for (size_t i=0; i<frames; i++) {
            double L=(double)stereo[i*channels+0];
            double R=(double)stereo[i*channels+1];
            sumL+=L; sumR+=R; sumL2+=L*L; sumR2+=R*R; sumLR+=L*R;
        }
        double meanL=sumL/frames, meanR=sumR/frames;
        double cov=(sumLR/frames) - (meanL*meanR);
        double varL=(sumL2/frames) - (meanL*meanL);
        double varR=(sumR2/frames) - (meanR*meanR);
        out->stereo_width=(varL>1e-12 && varR>1e-12? cov/(sqrt(varL)*sqrt(varR)):0.0);
    } else {
        out->stereo_width = 1.0; // mono
    }

    // --- Spectral Balance (FFT on first chunk, mono-mix of L+R) ---
        // --- Spectral Balance + Masking Index (multi-window average) ---
    int N = 4096; // FFT size
    int numWindows = 10;
    if ((size_t)N > frames) N = (int)frames; // fallback for very short files

    double balanceSum = 0.0;
    double flatnessSum = 0.0;
    int validWindows = 0;

    for (int w = 0; w < numWindows; w++) {
        size_t start = (size_t)((frames - N) * (w / (double)(numWindows - 1)));
        if (start + N > frames) break;

        Complex* bufc = (Complex*)malloc(sizeof(Complex) * N);
        if (!bufc) break;

        // Build mono window with Hann
        for (int i = 0; i < N; i++) {
            double sample = 0.0;
            for (int c = 0; c < channels; c++) {
                sample += stereo[(start + i) * channels + c];
            }
            sample /= channels;
            double wHann = 0.5 * (1.0 - cos(2 * M_PI * i / (N - 1)));
            bufc[i].r = sample * wHann;
            bufc[i].i = 0.0;
        }

        fft(bufc, N);

        double lowE = 0.0, highE = 0.0;
        double sumLin = 0.0;
        double sumLog = 0.0;
        int bins = 0;

        for (int k = 1; k < N/2; k++) {
            double freq = (double)k * sample_rate / (double)N;
            double psd = bufc[k].r*bufc[k].r + bufc[k].i*bufc[k].i + 1e-15;

            if (freq < 200.0) lowE += psd;
            else if (freq > 2000.0) highE += psd;

            sumLin += psd;
            sumLog += log(psd);
            bins++;
        }

        if (bins > 0) {
            // balance ratio
            double denom = lowE + highE;
            double bal = (denom > 1e-12 ? lowE / denom : 0.5);

            // flatness ratio
            double geoMean = exp(sumLog / bins);
            double arithMean = sumLin / bins;
            double flatness = (arithMean > 1e-15 ? geoMean / arithMean : 0.0);

            balanceSum += bal;
            flatnessSum += flatness;
            validWindows++;
        }

        free(bufc);
    }

    if (validWindows > 0) {
        out->spectral_balance = balanceSum / validWindows;
        out->masking_index    = flatnessSum / validWindows;
    } else {
        out->spectral_balance = 0.5;
        out->masking_index    = 0.0;
    }

    return 0;
}