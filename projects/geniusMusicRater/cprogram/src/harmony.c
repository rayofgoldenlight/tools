#include "harmony.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/**
 * Compute chroma matrix from PCM.
 * 
 * @param mono        mono PCM buffer
 * @param frames      number of samples
 * @param sample_rate sampling rate
 * @param hop_size    frame hop (e.g. 2048 for ~46ms @44.1kHz)
 * @param fft_size    analysis fft size (e.g. 4096)
 * @param out_chroma  pointer to double* to hold [num_frames x 12] (row-major)
 * @param out_frames  number of frames in result
 * 
 * Caller must free(*out_chroma).
 */
// Simple Hann window
static void hann_windowf(float* w, size_t N) {
    if (!w || N == 0) return;
    for (size_t i=0; i<N; ++i) {
        w[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * (float)i / ((float)N - 1)));
    }
}

/**
 * Fast chroma via Goertzel + internal decimation (no FFT dependency).
 * 
 * - Downsamples by `decim` (4 recommended) to reduce workload.
 * - Uses Hann window, hop_size step per frame.
 * - Uses Goertzel tuned to MIDI notes 40..88 (~E2 to C8).
 * - Aggregates power into 12 pitch classes, normalizes.
 *
 * out_chroma is [num_frames x 12], row-major.
 * Caller must free(*out_chroma).
 */
static int compute_chroma_goertzel(const float* mono,
                                   size_t frames,
                                   int sample_rate,
                                   size_t win_size,
                                   size_t hop_size,
                                   int decim,
                                   double** out_chroma,
                                   size_t* out_frames) {
    if (!mono || frames < win_size || !out_chroma || !out_frames) return -1;
    if (decim < 1) decim = 1;

    // Downsample
    size_t ds_frames = frames / decim;
    float* ds = (float*)malloc(sizeof(float) * ds_frames);
    if (!ds) return -2;
    for (size_t i=0; i<ds_frames; i++) {
        ds[i] = mono[i*decim];
    }
    int ds_rate = sample_rate / decim;

    // Setup frames
    size_t num_frames = (ds_frames - win_size) / hop_size + 1;
    *out_frames = num_frames;
    double* chroma = (double*)calloc(num_frames * 12, sizeof(double));
    if (!chroma) { free(ds); return -3; }

    // Window
    float* window = (float*)malloc(sizeof(float) * win_size);
    hann_windowf(window, win_size);

    // Pitch range: MIDI note 40 (E2, ~82Hz) to 88 (C8, ~4186Hz)
    int min_note = 40, max_note = 88;

    for (size_t fi=0; fi<num_frames; fi++) {
        const float* frame = ds + fi*hop_size;

        // apply window
        float* xw = (float*)malloc(sizeof(float)*win_size);
        for (size_t n=0; n<win_size; n++) {
            xw[n] = frame[n] * window[n];
        }

        // Analyze selected pitches
        for (int midi = min_note; midi <= max_note; midi++) {
            double freq = 440.0 * pow(2.0, (midi - 69) / 12.0);
            if (freq >= ds_rate/2.0) break; // beyond Nyquist

            // Goertzel
            double k = 0.5 + ((double)win_size * freq / (double)ds_rate);
            int K = (int)k;
            double w = 2.0 * M_PI * (double)K / (double)win_size;
            double c = cos(w);
            double coeff = 2.0 * c;

            double s_prev = 0.0, s_prev2 = 0.0;
            for (size_t n=0; n<win_size; n++) {
                double s = xw[n] + coeff * s_prev - s_prev2;
                s_prev2 = s_prev;
                s_prev = s;
            }
            double power = s_prev2*s_prev2 + s_prev*s_prev - coeff*s_prev*s_prev2;

            int pc = midi % 12;
            chroma[fi*12 + pc] += power;
        }
        free(xw);

        // normalize
        double norm = 0.0;
        for (int p=0; p<12; p++) norm += chroma[fi*12+p] * chroma[fi*12+p];
        norm = sqrt(norm);
        if (norm > 0.0) {
            for (int p=0; p<12; p++) chroma[fi*12+p] /= norm;
        }
    }

    free(window);
    free(ds);
    *out_chroma = chroma;
    return 0;
}

// Names for pitch classes
static const char* PC_NAMES[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// Major and minor triad templates (idealized, 12 bins)
static void fill_chord_templates(double templates[24][12], char names[24][16], int* count) {
    *count = 0;
    for (int root=0; root<12; root++) {
        // Major: root, +4, +7
        memset(templates[*count], 0, 12*sizeof(double));
        templates[*count][root] = 1.0;
        templates[*count][(root+4)%12] = 1.0;
        templates[*count][(root+7)%12] = 1.0;
        snprintf(names[*count], 16, "%s", PC_NAMES[root]);
        (*count)++;

        // Minor: root, +3, +7
        memset(templates[*count], 0, 12*sizeof(double));
        templates[*count][root] = 1.0;
        templates[*count][(root+3)%12] = 1.0;
        templates[*count][(root+7)%12] = 1.0;
        snprintf(names[*count], 16, "%sm", PC_NAMES[root]);
        (*count)++;
    }
}

// Cosine similarity between two 12-dim vectors
static double cosine_similarity(const double* a, const double* b) {
    double dot=0, na=0, nb=0;
    for (int i=0; i<12; i++) {
        dot += a[i]*b[i];
        na  += a[i]*a[i];
        nb  += b[i]*b[i];
    }
    if (na==0||nb==0) return 0.0;
    return dot / (sqrt(na)*sqrt(nb));
}


// Krumhansl-Schmuckler key profiles (major/minor).
// Normalized values for 12 pitch classes.
static const double KEY_PROFILE_MAJOR[12] = {
    6.35, 2.23, 3.48, 2.33, 4.38,
    4.09, 2.52, 5.19, 2.39, 3.66,
    2.29, 2.88
};
static const double KEY_PROFILE_MINOR[12] = {
    6.33, 2.68, 3.52, 5.38, 2.60,
    3.53, 2.54, 4.75, 3.98, 2.69,
    3.34, 3.17
};

// Rotate profile to match tonic offset
static void rotate_profile(const double* base, int offset, double* out) {
    for (int i=0; i<12; i++) {
        out[(i+offset)%12] = base[i];
    }
}

static void detect_key_from_chroma(const double* chroma, char* out_key, size_t out_sz, double* best_score) {
    double best_sim = -1.0;
    char best_name[16] = "C";

    double profile[12];
    static const char* maj_min[2] = {"", "m"};

    for (int mode=0; mode<2; mode++) { // 0=maj, 1=min
        const double* base = (mode==0? KEY_PROFILE_MAJOR : KEY_PROFILE_MINOR);
        for (int tonic=0; tonic<12; tonic++) {
            rotate_profile(base, tonic, profile);
            // cosine similarity
            double dot=0, na=0, nb=0;
            for (int i=0; i<12; i++) {
                dot += chroma[i]*profile[i];
                na  += chroma[i]*chroma[i];
                nb  += profile[i]*profile[i];
            }
            double sim = (na>0 && nb>0)? dot/(sqrt(na)*sqrt(nb)) : 0;
            if (sim > best_sim) {
                best_sim = sim;
                snprintf(best_name, sizeof(best_name), "%s%s", PC_NAMES[tonic], maj_min[mode]);
            }
        }
    }
    *best_score = best_sim;
    snprintf(out_key, out_sz, "%s", best_name);
}

// Map chord (triads) into pitch class set
// Supports names like "C", "Cm", "G#", "F#m"
static void chord_to_pcset(const char* name, int* pcs, int* count) {
    *count = 0;
    if (!name || strlen(name) < 1) return;

    // Find root
    int root = -1;
    size_t len = strlen(name);
    char buf[4] = {0};
    if (len>=2 && name[1]=='#') {
        buf[0]=name[0]; buf[1]='#';
        buf[2]=0;
    } else {
        buf[0]=name[0]; buf[1]=0;
    }
    for (int i=0; i<12; i++) {
        if (strcmp(buf, PC_NAMES[i])==0) { root=i; break; }
    }
    if (root<0) return;

    // Major or minor
    int minor = (name[strlen(name)-1]=='m');
    pcs[(*count)++] = root;
    if (minor) {
        pcs[(*count)++] = (root+3)%12; // minor 3rd
    } else {
        pcs[(*count)++] = (root+4)%12; // major 3rd
    }
    pcs[(*count)++] = (root+7)%12;     // perfect fifth
}

static double compute_harmonic_motion(const ChordLabel* chords, int n) {
    if (!chords || n<2) return 0.0;
    double total=0.0;
    int steps=0;

    for (int i=1; i<n; i++) {
        int pcs1[3], pcs2[3];
        int n1=0,n2=0;
        chord_to_pcset(chords[i-1].name, pcs1,&n1);
        chord_to_pcset(chords[i].name,   pcs2,&n2);

        if (n1==0||n2==0) continue;

        // Jaccard distance
        int common=0;
        for (int a=0;a<n1;a++){
            for (int b=0;b<n2;b++){
                if (pcs1[a]==pcs2[b]) common++;
            }
        }
        int total_notes = n1+n2-common;
        double dist = (total_notes>0)? 1.0 - ((double)common/(double)total_notes):0.0;

        total += dist;
        steps++;
    }
    return (steps>0? total/steps:0.0);
}

// Build diatonic scale for a key (major or minor)
static void build_diatonic_scale(const char* key, int* scale, int* scale_size) {
    *scale_size = 0;
    if (!key || strlen(key)<1) return;

    // parse tonic
    int tonic=-1, minor=0;
    char rootbuf[4]={0};
    if (key[1]=='#') {
        rootbuf[0]=key[0]; rootbuf[1]='#';
        rootbuf[2]=0;
        minor = (strstr(key,"m")!=NULL);
    } else {
        rootbuf[0]=key[0]; rootbuf[1]=0;
        minor = (strstr(key,"m")!=NULL);
    }
    for (int i=0; i<12; i++) {
        if (strcmp(rootbuf, PC_NAMES[i])==0) { tonic=i; break; }
    }
    if (tonic<0) return;

    // intervals: major = W W H W W W H; minor = W H W W H W W
    int major_steps[7] = {0,2,4,5,7,9,11};
    int minor_steps[7] = {0,2,3,5,7,8,10};

    const int* steps = (minor? minor_steps: major_steps);
    for (int i=0; i<7; i++) {
        scale[i] = (tonic+steps[i])%12;
    }
    *scale_size = 7;
}

// Check if a pitch class is in scale
static int pc_in_scale(int pc, const int* scale, int size) {
    for (int i=0; i<size; i++) if (scale[i]==pc) return 1;
    return 0;
}

static double compute_harmonic_tension(const ChordLabel* chords, int n, const char* global_key) {
    if (!chords || n==0 || !global_key) return 0.0;

    int scale[7]; int scale_size=0;
    build_diatonic_scale(global_key, scale, &scale_size);
    if (scale_size==0) return 0.0;

    double total=0.0;
    int count=0;

    for (int i=0;i<n;i++) {
        int pcs[3], pc_count=0;
        chord_to_pcset(chords[i].name, pcs, &pc_count);
        if (pc_count==0) continue;

        int out_notes=0;
        for (int j=0;j<pc_count;j++) {
            if (!pc_in_scale(pcs[j], scale, scale_size)) out_notes++;
        }

        double instability = (double)out_notes / (double)pc_count; // fraction outside scale

        // add minor chord penalty
        double is_minor = (chords[i].name[strlen(chords[i].name)-1]=='m');
        double local_tension = instability + (is_minor?0.2:0.0);

        if (local_tension>1.0) local_tension=1.0;
        total += local_tension;
        count++;
    }

    return (count>0? total/count:0.0);
}

int compute_harmony_features(const float* mono,
                             size_t frames,
                             int sample_rate,
                             HarmonyFeatures* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    // --- Step 2.1: Compute chroma features ---
    size_t win_size   = 2048;  // at ds_rate, ~186ms window
    size_t hop_size   = 1024;  // 50% overlap
    int decim         = 4;     // downsample factor

    double* chroma = NULL;
    size_t chroma_frames = 0;
    int rc = compute_chroma_goertzel(mono, frames, sample_rate,
                                    win_size, hop_size, decim,
                                    &chroma, &chroma_frames);

    if (rc != 0) {
        strncpy(out->global_key, "unknown", sizeof(out->global_key));
        out->key_stability   = 0.0;
        out->modulation_count= 0.0;
        out->harmonic_motion = 0.0;
        out->tension         = 0.0;
        out->chords          = NULL;
        out->chord_count     = 0;
        return rc;
    }

    // Aggregate chroma across entire song for rough key guess
    double avg_chroma[12] = {0};
    for (size_t f=0; f<chroma_frames; f++) {
        for (int p=0; p<12; p++) {
            avg_chroma[p] += chroma[f*12+p];
        }
    }

    // Find strongest pitch class
    int best_pc = 0;
    double best_val = 0.0;
    for (int p=0; p<12; p++) {
        if (avg_chroma[p] > best_val) {
            best_val = avg_chroma[p];
            best_pc = p;
        }
    }

    const char* pitch_names[12] =
        {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    snprintf(out->global_key,
             sizeof(out->global_key),
             "%s", pitch_names[best_pc]);
    

        // --- Step 2.2: Chord recognition ---
    double templates[24][12];
    char chord_names[24][16];
    int num_templates=0;
    fill_chord_templates(templates, chord_names, &num_templates);

    // Allocate chord labels, one every N frames (e.g. ~beat-synchronous)
    // For now we’ll rate chords every 1 sec (choose downsampled interval)
    double hop_time = (double)hop_size * (double)decim / (double)sample_rate;
    int step = (int)(1.0 / hop_time); // number of frames ~1 sec hop
    if (step < 1) step=1;
    int chord_capacity = (int)(chroma_frames/step + 1);
    out->chords = (ChordLabel*)calloc(chord_capacity, sizeof(ChordLabel));
    out->chord_count = 0;
    
    for (size_t f=0; f<chroma_frames; f += step) {
        const double* vec = &chroma[f*12];
        // find best template
        double best_sim = -1.0;
        int best_idx = -1;
        for (int t=0; t<num_templates; t++) {
            double sim = cosine_similarity(vec, templates[t]);
            if (sim > best_sim) { best_sim = sim; best_idx = t; }
        }
        if (best_idx>=0) {
            strncpy(out->chords[out->chord_count].name,
                    chord_names[best_idx],
                    sizeof(out->chords[out->chord_count].name));
            out->chords[out->chord_count].time_sec = f * hop_time;
            out->chord_count++;
        }
    }

        // --- Step 2.3: Key stability and modulation ---
    // Compute average chroma to get global key

    double best_score=0.0;
    detect_key_from_chroma(avg_chroma, out->global_key, sizeof(out->global_key), &best_score);
    out->key_stability = best_score; // 0..1

    // Now detect local keys in blocks (~10s each) and count modulations
    int window = (int)(10.0 / hop_time); // 10s worth of frames
    if (window < 1) window = 1;
    char last_key[16]="";
    int mods=0;

    for (size_t f=0; f<chroma_frames; f+=window) {
        double block_chroma[12]={0};
        int block_frames=0;
        for (size_t i=f; i<f+window && i<chroma_frames; i++) {
            for (int p=0; p<12; p++) block_chroma[p]+=chroma[i*12+p];
            block_frames++;
        }
        if (block_frames>0) {
            double dummy;
            char block_key[16];
            detect_key_from_chroma(block_chroma, block_key, sizeof(block_key), &dummy);
            if (last_key[0]==0) {
                strncpy(last_key, block_key, sizeof(last_key));
            } else if (strcmp(block_key,last_key)!=0) {
                mods++;
                strncpy(last_key, block_key, sizeof(last_key));
            }
        }
    }
    out->modulation_count = (double)mods;

    // --- Step 2.4: Harmonic motion ---
    out->harmonic_motion = compute_harmonic_motion(out->chords, out->chord_count);

    // --- Step 2.5: Harmonic tension ---
    out->tension = compute_harmonic_tension(out->chords, out->chord_count, out->global_key);

    free(chroma);
    return 0;
}

void free_harmony_features(HarmonyFeatures* hf) {
    if (!hf) return;
    if (hf->chords) free(hf->chords);
    hf->chords = NULL;
    hf->chord_count = 0;
}