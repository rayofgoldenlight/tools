#ifndef PRODUCTION_H
#define PRODUCTION_H

#include <stddef.h>

// Features describing production/timbre aspects
typedef struct {
    double loudness_db;          // Integrated loudness (dB LUFS or RMS converted)
    double dynamic_range_db;     // Peak-to-quiet difference
    double stereo_width;         // Correlation between channels (1.0 = mono, 0.0 = wide)
    double spectral_balance;     // Ratio of low vs high frequencies
    double masking_index;        // Estimate of spectral masking/clutter
} ProductionFeatures;

// Skeleton for computation (to implement step by step)
int compute_production_features(const float* stereo, size_t frames, int sample_rate, int channels, ProductionFeatures* out);

void free_production_features(ProductionFeatures* pf); // in case we add malloc'd arrays later

#endif