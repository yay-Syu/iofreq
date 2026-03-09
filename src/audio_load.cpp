/*
 * DEPENDENCIES:
 *   - minimp3.h   — header-only MP3 decoder, drop minimp3.h next to this file
 *                   https://github.com/lieff/minimp3
 *
 *   - kissfft     — FFT engine
 *                   https://github.com/mborgerding/kissfft
 *                   install: sudo apt install libkissfft-dev
 *                   link with: -lkissfft
 *
 *   - libFLAC     — FLAC decoder
 *                   install: sudo apt install libflac-dev
 *                   link with: -lFLAC
 *
 * COMPILE:
 *   g++ -c src/audio_load.cpp -Iinclude -o audio_load.o
 */

#include "audio_load.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "minimp3.h"
#include <FLAC/stream_decoder.h>

// Reads an entire file into a heap-allocated buffer; caller must delete[]
static uint8_t* mp3__read_file(const std::string& path, size_t* out_size) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;
    f.seekg(0, std::ios::end);
    *out_size = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    uint8_t* buf = new uint8_t[*out_size];
    if (buf) f.read(reinterpret_cast<char*>(buf), *out_size);
    return buf;
}

// Decodes an MP3 file to interleaved float PCM in [-1, 1]
// Two-pass: first pass counts samples, second pass fills the buffer
static AudioSamples audio_mp3_load(const std::string& filepath) {
    AudioSamples out = {0};
    size_t   file_size = 0;
    uint8_t* file_buf  = mp3__read_file(filepath, &file_size);
    if (!file_buf) return out;

    mp3dec_t dec;
    mp3dec_init(&dec);

    size_t   offset        = 0;
    uint64_t total_samples = 0;
    uint32_t sample_rate   = 0, channels = 0;

    // First pass: count total interleaved samples
    while (offset < file_size) {
        mp3dec_frame_info_t info;
        int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int ns = mp3dec_decode_frame(&dec, file_buf + offset,
            (int)(file_size - offset), pcm, &info);
        if (info.frame_bytes == 0) break;
        offset += (size_t)info.frame_bytes;
        if (ns > 0) {
            if (sample_rate == 0) { sample_rate = (uint32_t)info.hz; channels = (uint32_t)info.channels; }
            total_samples += (uint64_t)(ns * info.channels);
        }
    }

    if (total_samples == 0) { delete[] file_buf; return out; }
    float* samples = new float[total_samples];

    // Second pass: decode and normalize int16 PCM to float [-1, 1]
    mp3dec_init(&dec);
    offset = 0;
    uint64_t write_pos = 0;
    while (offset < file_size && write_pos < total_samples) {
        mp3dec_frame_info_t info;
        int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int ns = mp3dec_decode_frame(&dec, file_buf + offset,
            (int)(file_size - offset), pcm, &info);
        if (info.frame_bytes == 0) break;
        offset += (size_t)info.frame_bytes;
        if (ns > 0) {
            int count = ns * info.channels;
            for (int i = 0; i < count && write_pos < total_samples; i++)
                samples[write_pos++] = pcm[i] / 32768.0f;
        }
    }

    delete[] file_buf;
    out.samples     = samples;
    out.frame_count = (channels > 0) ? total_samples / channels : 0;
    out.sample_rate = sample_rate;
    out.channels    = channels;
    return out;
}

// Internal context passed through FLAC decoder callbacks
typedef struct {
    float*   buf;
    uint64_t capacity;
    uint64_t written;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t bits_per_sample;
    int      error;
} FlacCtx;

// Called by libFLAC for each decoded audio block — converts fixed-point to float
static FLAC__StreamDecoderWriteStatus flac__write_cb(
        const FLAC__StreamDecoder* dec, const FLAC__Frame* frame,
        const FLAC__int32* const buffer[], void* client_data)
{
    (void)dec;
    FlacCtx* ctx   = (FlacCtx*)client_data;
    uint32_t ch    = frame->header.channels;
    uint32_t count = frame->header.blocksize;
    float    scale = 1.0f / (float)(1 << (frame->header.bits_per_sample - 1));

    // Grow buffer dynamically if needed
    uint64_t needed = ctx->written + (uint64_t)(count * ch);
    if (needed > ctx->capacity) {
        uint64_t new_cap = needed * 2;
        float* nb = (float*)realloc(ctx->buf, new_cap * sizeof(float));
        if (!nb) { ctx->error = 1; return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT; }
        ctx->buf = nb; ctx->capacity = new_cap;
    }

    // Interleave channels: [L0, R0, L1, R1, ...]
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t c = 0; c < ch; c++)
            ctx->buf[ctx->written++] = buffer[c][i] * scale;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

// Called by libFLAC when stream metadata is available — pre-allocates the buffer
static void flac__meta_cb(const FLAC__StreamDecoder* dec,
                          const FLAC__StreamMetadata* meta, void* client_data)
{
    (void)dec;
    FlacCtx* ctx = (FlacCtx*)client_data;
    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->channels        = meta->data.stream_info.channels;
        ctx->sample_rate     = meta->data.stream_info.sample_rate;
        ctx->bits_per_sample = meta->data.stream_info.bits_per_sample;
        uint64_t total = meta->data.stream_info.total_samples * ctx->channels;
        if (total > 0) { ctx->buf = new float[total]; ctx->capacity = total; }
    }
}

// Called by libFLAC on decoding error
static void flac__error_cb(const FLAC__StreamDecoder* dec,
                           FLAC__StreamDecoderErrorStatus status, void* client_data)
{
    (void)dec; (void)status;
    ((FlacCtx*)client_data)->error = 1;
}

// Decodes a FLAC file to interleaved float PCM in [-1, 1]
static AudioSamples audio_flac_load(const std::string& filepath) {
    AudioSamples out = {0};
    FlacCtx ctx; memset(&ctx, 0, sizeof(ctx));

    FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
    if (!dec) return out;
    FLAC__stream_decoder_set_md5_checking(dec, false);

    FLAC__StreamDecoderInitStatus status =
        FLAC__stream_decoder_init_file(dec, filepath.c_str(),
                                       flac__write_cb, flac__meta_cb, flac__error_cb, &ctx);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(dec); return out;
    }

    FLAC__stream_decoder_process_until_end_of_stream(dec);
    FLAC__stream_decoder_delete(dec);

    if (ctx.error || !ctx.buf || ctx.written == 0) { delete[] ctx.buf; return out; }

    out.samples     = ctx.buf;
    out.frame_count = ctx.written / ctx.channels;
    out.sample_rate = ctx.sample_rate;
    out.channels    = ctx.channels;
    return out;
}

// Little-endian read helpers for WAV parsing
static uint16_t wav__read_u16(std::ifstream& f) {
    uint8_t b[2]; f.read(reinterpret_cast<char*>(b), 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}
static uint32_t wav__read_u32(std::ifstream& f) {
    uint8_t b[4]; f.read(reinterpret_cast<char*>(b), 4);
    return (uint32_t)(b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24));
}

// Decodes a WAV file to interleaved float PCM in [-1, 1]
// Supports PCM integer (format 1) and IEEE float (format 3), 8/16/24/32-bit
static AudioSamples audio_wav_load(const std::string& filepath) {
    AudioSamples out = {0};
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return out;

    // Validate RIFF/WAVE header
    char riff[4]; f.read(riff, 4);
    if (memcmp(riff, "RIFF", 4) != 0) return out;
    wav__read_u32(f);
    char wave[4]; f.read(wave, 4);
    if (memcmp(wave, "WAVE", 4) != 0) return out;

    uint16_t audio_format = 0, channels = 0, bits_per_sample = 0;
    uint32_t sample_rate = 0, data_size = 0;
    long data_offset = 0;

    // Parse chunks until we find "fmt " and "data"
    while (f) {
        char id[4]; if (!f.read(id, 4)) break;
        uint32_t size = wav__read_u32(f);
        if (memcmp(id, "fmt ", 4) == 0) {
            audio_format    = wav__read_u16(f); // 1=PCM, 3=IEEE float
            channels        = wav__read_u16(f);
            sample_rate     = wav__read_u32(f);
            wav__read_u32(f); wav__read_u16(f); // byte rate, block align — unused
            bits_per_sample = wav__read_u16(f);
            if (size > 16) f.seekg((long)(size - 16), std::ios::cur);
        } else if (memcmp(id, "data", 4) == 0) {
            data_offset = f.tellg(); data_size = size; break;
        } else {
            f.seekg((long)size, std::ios::cur); // skip unknown chunks
        }
    }

    if ((audio_format != 1 && audio_format != 3) || data_offset == 0) return out;

    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint64_t total_samples    = data_size / bytes_per_sample;
    float*   samples          = new float[total_samples];

    f.seekg(data_offset, std::ios::beg);

    // Convert each sample to float [-1, 1] according to bit depth
    for (uint64_t i = 0; i < total_samples; i++) {
        if (audio_format == 3 && bits_per_sample == 32) {
            float v; f.read(reinterpret_cast<char*>(&v), 4);
            samples[i] = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
        } else if (bits_per_sample == 8) {
            // 8-bit PCM is unsigned [0, 255]
            uint8_t v; f.read(reinterpret_cast<char*>(&v), 1);
            samples[i] = (v / 127.5f) - 1.0f;
        } else if (bits_per_sample == 16) {
            int16_t v; f.read(reinterpret_cast<char*>(&v), 2);
            samples[i] = v / 32768.0f;
        } else if (bits_per_sample == 24) {
            // 24-bit has no native C type — read 3 bytes and sign-extend manually
            uint8_t b[3]; f.read(reinterpret_cast<char*>(b), 3);
            int32_t v = (int32_t)((b[2] << 24) | (b[1] << 16) | (b[0] << 8)) >> 8;
            samples[i] = v / 8388608.0f;
        } else if (bits_per_sample == 32) {
            int32_t v; f.read(reinterpret_cast<char*>(&v), 4);
            samples[i] = v / 2147483648.0f;
        } else {
            samples[i] = 0.0f;
        }
    }

    out.samples     = samples;
    out.frame_count = total_samples / channels;
    out.sample_rate = sample_rate;
    out.channels    = (uint32_t)channels;
    return out;
}

/*=== Audio_load ===*/

Audio_load::Audio_load(const std::string& file_path, const std::string& type)
    : m_file_path(file_path), m_type(type) {}

void Audio_load::print_infos(AudioSamples& audio)
{
    std::cout << "[Audio_load] Sample rate : " << audio.sample_rate << "\n";
    std::cout << "[Audio_load] Channels    : " << audio.channels << "\n";
    std::cout << "[Audio_load] Frames      : " << audio.frame_count << "\n";
    std::cout << "[Audio_load] Duration    : "
              << (audio.sample_rate != 0 ? audio.frame_count / audio.sample_rate : 0)
              << " seconds\n";
}

void Audio_load::mp3_load() {
    AudioSamples audio = audio_mp3_load(m_file_path);
    if (!audio.samples) { std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n"; return; }
    print_infos(audio); m_samples = audio.samples; m_audio = audio;
}

void Audio_load::flac_load() {
    AudioSamples audio = audio_flac_load(m_file_path);
    if (!audio.samples) { std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n"; return; }
    print_infos(audio); m_samples = audio.samples; m_audio = audio;
}

void Audio_load::wav_load() {
    AudioSamples audio = audio_wav_load(m_file_path);
    if (!audio.samples) { std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n"; return; }
    print_infos(audio); m_samples = audio.samples; m_audio = audio;
}
