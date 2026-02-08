#include "audio_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "feature_extractor.h"
#include "psychoacoustics.h"
#include "grading.h"
#include "rhythm.h"
#include "harmony.h"
#include "melody.h"
#include <time.h>
#include "structure.h"
#include "production.h"
#include "geniusgrading.h"

typedef struct {
    double duration_sec;
    double rms;
    double peak;
    double dc_offset;
    double zcr; // zero-crossing rate (per second)
} BasicStats;

static BasicStats compute_basic_stats(const float* mono, size_t frames, int sample_rate) {
    BasicStats s = {0};
    if (!mono || frames == 0 || sample_rate <= 0) return s;

    double sum = 0.0;
    double sumsq = 0.0;
    double peak = 0.0;
    size_t zero_crossings = 0;

    float prev = mono[0];
    for (size_t i = 0; i < frames; ++i) {
        float x = mono[i];
        sum += x;
        sumsq += (double)x * (double)x;
        double a = fabs((double)x);
        if (a > peak) peak = a;

        if ((x >= 0.0f && prev < 0.0f) || (x < 0.0f && prev >= 0.0f)) {
            zero_crossings++;
        }
        prev = x;
    }

    double mean = sum / (double)frames;
    double variance = (sumsq / (double)frames) - mean * mean;
    if (variance < 0.0) variance = 0.0;
    double rms = sqrt(sumsq / (double)frames);

    s.duration_sec = (double)frames / (double)sample_rate;
    s.rms = rms;
    s.peak = peak;
    s.dc_offset = mean;
    s.zcr = ((double)zero_crossings / s.duration_sec);
    return s;
}



int main(int argc, char** argv) {
    clock_t start = clock();

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.mp3> [genre] [--m(elody)] [--s(tructure)] [--g(enius)]\n", argv[0]);
        fprintf(stderr, "Genres: rap, vgm, pop, experimental, phonk, default\n");
        fprintf(stderr, "Flags:  --m // --melody     enable melody feature extraction\n");
        fprintf(stderr, "       --s // --structure  enable structure feature extraction\n");
        fprintf(stderr, "       --g // --genius  enable genius rating\n");
        return 1;
    }
    const char* path = argv[1];
    const RatingWeights* weights = &DEFAULT_WEIGHTS;
    const char* profile_label = "default";
    int do_melody = 0; // default off
    int do_structure = 0; // default off
    int do_genius = 0; // default off

    // parse genre if provided
    if (argc >= 3 && argv[2][0] != '-') {
        if      (strcmp(argv[2],"rap")==0) {
            weights = &RAP_WEIGHTS; profile_label="rap";
        }
        else if (strcmp(argv[2],"vgm")==0) {
            weights = &VGM_WEIGHTS; profile_label="vgm";
        }
        else if (strcmp(argv[2],"pop")==0) {
            weights = &POP_WEIGHTS; profile_label="pop";
        }
        else if (strcmp(argv[2],"experimental")==0) {
            weights = &EXPERIMENTAL_WEIGHTS; profile_label="experimental";
        }
        else if (strcmp(argv[2],"phonk")==0) {
            weights = &PHONK_WEIGHTS; profile_label="phonk";
        }
    }

    // check for melody/structure flags anywhere in args
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--m") == 0 || strcmp(argv[i], "--melody") == 0) {
            do_melody = 1;
        }
        if (strcmp(argv[i], "--s") == 0 || strcmp(argv[i], "--structure") == 0) {
            do_structure = 1;
        }
        if (strcmp(argv[i], "--g") == 0 || strcmp(argv[i], "--genius") == 0) {
            do_genius = 1;
        }
    }
    AudioBuffer buf = {0};
    int rc = decode_mp3_to_pcm(path, &buf);
    if (rc != 0) {
        fprintf(stderr, "Failed to decode MP3: error %d\n", rc);
        return 2;
    }

    // Convert to mono and resample to 44100 Hz for consistent analysis
    float* mono = NULL;
    size_t mono_frames = 0;
    int target_sr = 44100;

    rc = resample_and_mix_mono(&buf, target_sr, &mono, &mono_frames);
    if (rc != 0) {
        fprintf(stderr, "Failed to resample/mix: error %d\n", rc);
        free_audio_buffer(&buf);
        return 3;
    }

    BasicStats stats = compute_basic_stats(mono, mono_frames, target_sr);
    
    // --- Spectral + musical features ---
    SpectralFeatures spec;
    int rc_sf = compute_spectral_features(mono, mono_frames, target_sr, &spec);
    double tempo_bpm = 0.0;
    int rc_tempo = estimate_tempo_bpm(mono, mono_frames, target_sr, &tempo_bpm);
    char key[8] = {0};
    int rc_key = estimate_key(mono, mono_frames, target_sr, key);

    PsychoacousticFeatures psy;
    int rc_psy = compute_psychoacoustics(mono, mono_frames, target_sr, &psy);
    
    Ratings ratings;
    int rc_ratings = compute_ratings(&spec,
                                     tempo_bpm,
                                     (rc_key==0? key:"unknown"),
                                     &psy,
                                     &ratings,
                                     weights);
    
    RhythmFeatures rhythm;
    int rc_rhythm = compute_rhythm_features(mono, mono_frames, target_sr, &rhythm);

    HarmonyFeatures harmony;
    int rc_harmony = compute_harmony_features(mono, mono_frames, target_sr, &harmony);
    
    int rc_mel = 1;
    MelodyFeatures melody;
    memset(&melody, 0, sizeof(MelodyFeatures));
    if (do_melody) {
        rc_mel = compute_melody_features(mono, mono_frames, target_sr, &melody);
    }

    // --- Structure Analysis (Step 4b) ---
    StructureFeatures structure;
    memset(&structure, 0, sizeof(StructureFeatures));
    int rc_structure = 0;
    if (do_structure){
        rc_structure = compute_structure_features(mono, mono_frames, target_sr, &structure);
    }

     // --- Production / Timbre Features (Step 5a: Loudness) ---
    ProductionFeatures prod;
    int rc_prod = compute_production_features(
        buf.pcm,            // raw PCM samples (interleaved)
        buf.frames,         // number of frames
        buf.sample_rate,    // native sample rate
        buf.channels,       // number of channels
        &prod
    );

    // Print JSON skeleton for later parts to fill advanced analysis/grades
    printf("{\n");
    printf("  \"file\": \"%s\",\n", path);
    printf("  \"original\": {\n");
    printf("    \"sample_rate\": %d,\n", buf.sample_rate);
    printf("    \"channels\": %d,\n", buf.channels);
    printf("    \"frames\": %zu\n", buf.frames);
    printf("  },\n");
    printf("  \"analysis_basis\": {\n");
    printf("    \"resampled_sample_rate\": %d,\n", target_sr);
    printf("    \"mono_frames\": %zu\n", mono_frames);
    printf("  },\n");
        printf("  \"basic_stats\": {\n");
    printf("    \"duration_seconds\": %.6f,\n", stats.duration_sec);
    printf("    \"rms\": %.6f,\n", stats.rms);
    printf("    \"peak\": %.6f,\n", stats.peak);
    printf("    \"dc_offset\": %.6f,\n", stats.dc_offset);
    printf("    \"zero_crossings_per_second\": %.6f\n", stats.zcr);
    printf("  },\n");
        printf("  \"features\": {\n");
    printf("    \"tempo_bpm\": %.2f,\n", (rc_tempo==0 ? tempo_bpm : 0.0));
    printf("    \"key\": \"%s\",\n", (rc_key==0 ? key : "unknown"));
    //printf("    \"chord_progression\": null,\n"); was supposed to be here but moved to harmony
    printf("    \"spectral\": {\n");
    printf("      \"centroid\": %.2f,\n", (rc_sf==0 ? spec.centroid : 0.0));
    printf("      \"rolloff\": %.2f,\n", (rc_sf==0 ? spec.rolloff : 0.0));
    printf("      \"brightness\": %.4f,\n", (rc_sf==0 ? spec.brightness : 0.0));
    printf("      \"mfcc\": [");
    if (rc_sf==0) {
        for (int i=0; i<FEATURE_MFCC_COUNT; i++) {
            printf("%.4f", spec.mfcc[i]);
            if (i<FEATURE_MFCC_COUNT-1) printf(", ");
        }
    }
    printf("]\n");
    printf("    }\n");
    printf("  },\n");
        printf("  \"psychoacoustics\": {\n");
    printf("    \"roughness\": %.6f,\n", (rc_psy==0? psy.roughness : 0.0));
    printf("    \"dissonance\": %.6f,\n", (rc_psy==0? psy.dissonance : 0.0));
    printf("    \"loudness_lu\": %.2f,\n", (rc_psy==0? psy.loudness_lu : 0.0));
    printf("    \"dynamic_range_db\": %.2f\n", (rc_psy==0? psy.dynamic_range : 0.0));
    printf("  },\n");
        printf("  \"ratings\": {\n");
    printf("    \"harmonic_quality\": %d,\n", (rc_ratings==0? ratings.harmonic_quality: 0));
    printf("    \"progression_quality\": %d,\n", (rc_ratings==0? ratings.progression_quality: 0));
    printf("    \"pleasantness\": %d,\n", (rc_ratings==0? ratings.pleasantness: 0));
    printf("    \"creativity\": %d,\n", (rc_ratings==0? ratings.creativity: 0));
    printf("    \"overall_grade\": %d,\n", (rc_ratings==0? ratings.overall_grade: 0));
    printf("    \"rating_profile\": \"%s\"\n", profile_label);
    printf("  },\n");
        printf("    \"rhythm\": {\n");
    printf("      \"tempo_bpm\": %.2f,\n", rhythm.tempo_bpm);
    printf("      \"tempo_confidence\": %.2f,\n", rhythm.tempo_confidence);
    printf("      \"beat_strength\": %.4f,\n", rhythm.beat_strength);
    printf("      \"pulse_clarity\": %.4f,\n", rhythm.pulse_clarity);
    printf("      \"syncopation\": %.4f,\n", rhythm.syncopation);
    printf("      \"swing_ratio\": %.2f\n", rhythm.swing_ratio);
    printf("    },\n");
            // --- Harmony Analysis (Step 2) ---
    printf("  \"harmony\": {\n");
    printf("    \"global_key\": \"%s\",\n", harmony.global_key);
    printf("    \"key_stability\": %.3f,\n", harmony.key_stability);
    printf("    \"modulation_count\": %.1f,\n", harmony.modulation_count);
    printf("    \"harmonic_motion\": %.3f,\n", harmony.harmonic_motion);
    printf("    \"tension\": %.3f,\n", harmony.tension);
    printf("    \"chords\": [");
    for (int i=0; i<harmony.chord_count; i++) {
        printf("{\"time_sec\": %.2f, \"name\": \"%s\"}",
               harmony.chords[i].time_sec, harmony.chords[i].name);
        if (i < harmony.chord_count-1) printf(", ");
    }
    printf("]\n");
    printf("  },\n");
    free_harmony_features(&harmony);
        printf("  \"melody\": {\n");
    if (rc_mel==0) {
        printf("    \"median_f0\": %.2f,\n", melody.median_f0);
        printf("    \"mean_f0\": %.2f,\n", melody.mean_f0);
        printf("    \"f0_confidence\": %.3f,\n", melody.f0_confidence);
        printf("    \"pitch_range_semitones\": %.2f,\n", melody.pitch_range_semitones);
        printf("    \"contour_count\": %d,\n", melody.contour_count);
        printf("    \"avg_contour_length_sec\": %.3f,\n", melody.avg_contour_length_sec);
        printf("    \"longest_contour_sec\": %.3f,\n", melody.longest_contour_sec);
        printf("    \"avg_interval_semitones\": %.3f,\n", melody.avg_interval_semitones);
        printf("    \"avg_abs_interval_semitones\": %.3f,\n", melody.avg_abs_interval_semitones);
        printf("    \"melodic_entropy\": %.3f,\n", melody.melodic_entropy);
        printf("    \"motif_repetition_rate\": %.3f,\n", melody.motif_repetition_rate);
        printf("    \"motif_count\": %d,\n", melody.motif_count);
        printf("    \"hook_strength\": %.3f\n", melody.hook_strength);
    } else {
        printf("    \"error\": \"melody extraction failed\"\n");
    }
    printf("  },\n");
        printf("  \"structure\": {\n");
        if (do_structure && rc_structure==0) {
    printf("    \"section_count\": %zu,\n", structure.section_count);
    printf("    \"arc_complexity\": %.3f,\n", structure.arc_complexity);
    printf("    \"repetition_ratio\": %.3f,\n", structure.repetition_ratio);

    // --- section durations ---
    printf("    \"section_durations\": [");
    for (size_t i = 0; i < structure.section_count; i++) {
        double len = structure.sections[i].end_sec - structure.sections[i].start_sec;
        printf("%.2f", len);
        if (i < structure.section_count - 1) printf(", ");
    }
    printf("],\n");

    // --- duration ratio (longest/shortest) ---
    double shortest = 1e9, longest = 0.0;
    for (size_t i = 0; i < structure.section_count; i++) {
        double len = structure.sections[i].end_sec - structure.sections[i].start_sec;
        if (len < shortest) shortest = len;
        if (len > longest) longest = len;
    }
    double duration_ratio = (shortest > 1e-6 ? longest / shortest : 0.0);
    printf("    \"duration_ratio\": %.2f,\n", duration_ratio);

    // --- label frequency counts ---
    int count_intro=0, count_verse=0, count_chorus=0, count_bridge=0, count_outro=0;
    int has_chorus=0;
    for (size_t i=0; i<structure.section_count; i++) {
        if (strcmp(structure.sections[i].label, "intro")==0) count_intro++;
        else if (strcmp(structure.sections[i].label, "verse")==0) count_verse++;
        else if (strcmp(structure.sections[i].label, "chorus")==0) {count_chorus++; has_chorus=1;}
        else if (strcmp(structure.sections[i].label, "bridge")==0) count_bridge++;
        else if (strcmp(structure.sections[i].label, "outro")==0) count_outro++;
    }
    printf("    \"section_labels_summary\": {\n");
    printf("      \"intro\": %d,\n", count_intro);
    printf("      \"verse\": %d,\n", count_verse);
    printf("      \"chorus\": %d,\n", count_chorus);
    printf("      \"bridge\": %d,\n", count_bridge);
    printf("      \"outro\": %d\n", count_outro);
    printf("    },\n");

    printf("    \"has_chorus\": %s,\n", has_chorus ? "true" : "false");

    // --- normalized arcs (boundary times / total duration) ---
    double total_duration = structure.sections[structure.section_count-1].end_sec;
    printf("    \"structural_arcs\": [");
    for (size_t i=0; i<structure.section_count; i++) {
        double arc_pos = structure.sections[i].start_sec / total_duration;
        printf("%.3f", arc_pos);
        if (i < structure.section_count - 1) printf(", ");
    }
    printf("],\n");

    // --- actual sections list ---
    printf("    \"sections\": [");
    for (size_t i = 0; i < structure.section_count; i++) {
        printf("{\"start_sec\": %.2f, \"end_sec\": %.2f, \"label\": \"%s\"}",
               structure.sections[i].start_sec,
               structure.sections[i].end_sec,
               structure.sections[i].label);
        if (i < structure.section_count - 1) printf(", ");
    }
    printf("]\n");} else {
        printf("    \"error\": \"structure extraction %s\"\n",
           do_structure ? "failed" : "disabled");
    }
    printf("  },\n");
     printf("  \"production\": {\n");
    if (rc_prod == 0) {
        printf("    \"loudness_db\": %.2f,\n", prod.loudness_db);
        printf("    \"dynamic_range_db\": %.2f,\n", prod.dynamic_range_db);
        printf("    \"stereo_width\": %.3f,\n", prod.stereo_width);
        printf("    \"spectral_balance\": %.3f,\n", prod.spectral_balance);
        printf("    \"masking_index\": %.3f\n", prod.masking_index);
    } else {
        printf("    \"error\": \"production features failed\"\n");
    }
    printf("  },\n");
    // -------- Genius Evaluation (Step 6) --------
    if (do_genius) {
        GeniusInputs g_in = {0};

        // fill with available data
        g_in.duration_sec = stats.duration_sec;
        g_in.rms = stats.rms;
        g_in.peak = stats.peak;
        g_in.dc_offset = stats.dc_offset;
        g_in.zcr = stats.zcr;

        g_in.spectral = spec;
        g_in.psy = psy;
        g_in.rhythm = rhythm;
        g_in.harmony = harmony;
        g_in.melody = melody;
        g_in.melody_valid = (rc_mel == 0);
        g_in.structure = structure;
        g_in.structure_valid = (do_structure && rc_structure == 0);
        g_in.prod = prod;
        g_in.prod_valid = (rc_prod == 0);

        GeniusResult g_out;
        compute_genius_rating(&g_in, &g_out,
            (strcmp(profile_label,"rap")==0? GENIUS_GENRE_RAP :
            strcmp(profile_label,"vgm")==0? GENIUS_GENRE_VGM :
            strcmp(profile_label,"pop")==0? GENIUS_GENRE_POP :
            strcmp(profile_label,"experimental")==0? GENIUS_GENRE_EXPERIMENTAL :
            strcmp(profile_label,"phonk")==0? GENIUS_GENRE_PHONK :
            GENIUS_GENRE_DEFAULT));

        // ---- Print Genius JSON block ----
        printf("  \"genius\": {\n");
        printf("    \"overall_score\": %d,\n", g_out.overall_score);
        printf("    \"is_genius\": %s,\n", g_out.is_genius ? "true" : "false");
        printf("    \"confidence\": %.3f,\n", g_out.confidence);
        printf("    \"categories\": {\n");
        printf("      \"harmony\": %d,\n", g_out.harmony_score);
        printf("      \"progression\": %d,\n", g_out.progression_score);
        printf("      \"melody\": %d,\n", g_out.melody_score);
        printf("      \"rhythm\": %d,\n", g_out.rhythm_score);
        printf("      \"structure\": %d,\n", g_out.structure_score);
        printf("      \"timbre\": %d,\n", g_out.timbre_score);
        printf("      \"creativity\": %d,\n", g_out.creativity_score);
        printf("    \"originality_score\": %d,\n", g_out.originality_score);
        printf("    \"complexity_score\": %d,\n", g_out.complexity_score);
        printf("    \"genre_distance_score\": %d,\n", g_out.genre_distance_score);
        printf("    \"emotion_score\": %d,\n", g_out.emotion_score);
        printf("    \"explanation\": {\n");
        printf("      \"positive_contributors\": [");
        for (int i=0; i<g_out.pos_count; i++) {
            printf("\"%s\"", g_out.positives[i]);
            if (i < g_out.pos_count-1) printf(", ");
        }
        printf("],\n");
        printf("      \"negative_contributors\": [");
        for (int i=0; i<g_out.neg_count; i++) {
            printf("\"%s\"", g_out.negatives[i]);
            if (i < g_out.neg_count-1) printf(", ");
        }
        printf("]\n");
        printf("    }\n");
        printf("    }\n");
        printf("  }\n");
        printf("}\n");
    }

    free_structure_features(&structure);



    printf("Profile: %s | Melody: %s | Structure: %s\n",
       profile_label,
       do_melody ? "on" : "off",
       do_structure ? "on" : "off");
    


    free(mono);
    free_audio_buffer(&buf);

    //time check

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Elapsed time: %.3f seconds\n", elapsed);

    return 0;
}