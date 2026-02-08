#include "audio_decoder.h"
#include <mpg123.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static float* mix_to_mono(const float* interleaved, size_t frames, int channels) {
    if (channels <= 0) return NULL;
    float* mono = (float*)malloc(sizeof(float) * frames);
    if (!mono) return NULL;

    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        const float* frame = interleaved + i * channels;
        for (int c = 0; c < channels; ++c) {
            acc += frame[c];
        }
        mono[i] = (float)(acc / (double)channels);
    }
    return mono;
}

int decode_mp3_to_pcm(const char* path, AudioBuffer* out) {
    if (!path || !out) return -1;

    memset(out, 0, sizeof(*out));

    int ret = 0;
    int err = MPG123_OK;
    mpg123_handle* mh = NULL;

    if (mpg123_init() != MPG123_OK) {
        fprintf(stderr, "mpg123_init failed\n");
        return -2;
    }

    mh = mpg123_new(NULL, &err);
    if (!mh) {
        fprintf(stderr, "mpg123_new failed: %s\n", mpg123_plain_strerror(err));
        mpg123_exit();
        return -3;
    }

    // Request float32 output in any sample rate, mono or stereo etc.
    // Request float32 output (do not clear formats on Windows)
    mpg123_param(mh, MPG123_FLAGS, MPG123_FORCE_FLOAT, 0.0);

    if ((err = mpg123_open(mh, path)) != MPG123_OK) {
        fprintf(stderr, "mpg123_open failed for %s: %s\n", path, mpg123_plain_strerror(err));
        mpg123_delete(mh);
        mpg123_exit();
        return -5;
    }

    // Decode loop
    size_t cap = 0;
    size_t sz = 0; // bytes
    float* data = NULL;

    unsigned char* io_buf = NULL;
    size_t done = 0;

    long rate = 0;
    int channels = 0;
    int encoding = 0;

    // Ensure we know the format
    mpg123_getformat(mh, &rate, &channels, &encoding);
    if (encoding != MPG123_ENC_FLOAT_32) {
        // Try to enforce again
        mpg123_format_none(mh);
        err = mpg123_format(mh, rate, channels == 1 ? MPG123_MONO : MPG123_STEREO, MPG123_ENC_FLOAT_32);
        if (err != MPG123_OK) {
            fprintf(stderr, "Failed to set float32 output: %s\n", mpg123_plain_strerror(err));
            mpg123_close(mh);
            mpg123_delete(mh);
            mpg123_exit();
            return -6;
        }
    }

    // Read until EOF
    for (;;) {
        // Grow buffer if needed
        if (sz + 4096 * sizeof(float) > cap) {
            size_t newcap = cap == 0 ? (size_t)1 << 20 : cap * 2; // grow exponentially
            float* nd = (float*)realloc(data, newcap);
            if (!nd) {
                fprintf(stderr, "Out of memory while decoding\n");
                free(data);
                mpg123_close(mh);
                mpg123_delete(mh);
                mpg123_exit();
                return -7;
            }
            data = nd;
            cap = newcap;
        }

        size_t bytes_avail = cap - sz;
        int read_res = mpg123_read(mh, (unsigned char*)data + sz, bytes_avail, &done);
        sz += done;

        if (read_res == MPG123_DONE) {
            break;
        } else if (read_res != MPG123_OK) {
            fprintf(stderr, "mpg123_read error: %s\n", mpg123_plain_strerror(read_res));
            free(data);
            mpg123_close(mh);
            mpg123_delete(mh);
            mpg123_exit();
            return -8;
        }
    }

    // Final format after decode (in case stream changed)
    mpg123_getformat(mh, &rate, &channels, &encoding);

    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();

    size_t total_samples = sz / sizeof(float);
    if (channels <= 0) {
        free(data);
        return -9;
    }
    size_t frames = total_samples / (size_t)channels;

    out->pcm = data;
    out->frames = frames;
    out->sample_rate = (int)rate;   
    out->channels = channels;
    return ret;
}

void free_audio_buffer(AudioBuffer* buf) {
    if (!buf) return;
    free(buf->pcm);
    buf->pcm = NULL;
    buf->frames = 0;
    buf->sample_rate = 0;
    buf->channels = 0;
}

int resample_and_mix_mono(const AudioBuffer* in, int target_sr, float** out_pcm, size_t* out_frames) {
    if (!in || !out_pcm || !out_frames || !in->pcm || in->frames == 0 || target_sr <= 0) {
        return -1;
    }

    // Mix to mono first
    float* mono = mix_to_mono(in->pcm, in->frames, in->channels);
    if (!mono) return -2;

    if (in->sample_rate == target_sr) {
        // No resampling needed
        *out_pcm = mono;
        *out_frames = in->frames;
        return 0;
    }

    double in_sr = (double)in->sample_rate;
    double out_sr = (double)target_sr;
    double ratio = in_sr / out_sr;
    size_t n_out = (size_t)floor((double)in->frames * (out_sr / in_sr));
    if (n_out == 0) n_out = 1;

    float* out = (float*)malloc(sizeof(float) * n_out);
    if (!out) {
        free(mono);
        return -3;
    }

    for (size_t n = 0; n < n_out; ++n) {
        double src_pos = n * ratio;
        size_t i0 = (size_t)floor(src_pos);
        double frac = src_pos - (double)i0;

        if (i0 >= in->frames - 1) {
            out[n] = mono[in->frames - 1];
        } else {
            float s0 = mono[i0];
            float s1 = mono[i0 + 1];
            out[n] = (float)((1.0 - frac) * s0 + frac * s1);
        }
    }

    free(mono);
    *out_pcm = out;
    *out_frames = n_out;
    return 0;
}