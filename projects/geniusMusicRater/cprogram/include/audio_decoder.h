#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float* pcm;        // Interleaved float32 PCM
    size_t frames;     // Number of frames (per channel)
    int sample_rate;   // Hz
    int channels;      // 1 or 2 (or more, but typically 2)
} AudioBuffer;

// Decode an MP3 file to float32 PCM using libmpg123.
// Returns 0 on success, non-zero on error.
int decode_mp3_to_pcm(const char* path, AudioBuffer* out);

// Free resources allocated in AudioBuffer.
void free_audio_buffer(AudioBuffer* buf);

// Mix an interleaved multi-channel buffer to mono and resample to target_sr (linear).
// Returns 0 on success, non-zero on error. Caller owns *out_pcm.
int resample_and_mix_mono(const AudioBuffer* in, int target_sr, float** out_pcm, size_t* out_frames);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_DECODER_H