#ifndef STRUCTURE_H
#define STRUCTURE_H

#include <stddef.h>

typedef struct {
    double start_sec;         // section start time
    double end_sec;           // section end time
    char label[32];           // section label, e.g., "verse", "chorus", "bridge"
} Section;

typedef struct {
    Section* sections;
    size_t section_count;

    double arc_complexity;     // later: measure of narrative/arc similarity
    double repetition_ratio;   // ratio of repeated material vs novel
} StructureFeatures;

// Allocate + compute structure features
int compute_structure_features(const float* mono,
                               size_t frames,
                               int sample_rate,
                               StructureFeatures* out);

// Free allocated memory inside StructureFeatures
void free_structure_features(StructureFeatures* sf);

#endif // STRUCTURE_H