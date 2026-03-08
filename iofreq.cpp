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
 */

#include "iofreq.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>
#include <complex>
#include <cstdint>
#include <algorithm>

#include "minimp3.h"
#include <FLAC/stream_decoder.h>
#include "kiss_fftr.h"

// Frees the sample buffer inside an AudioSamples struct
static void audio_samples_free(AudioSamples* a) {
    if (a && a->samples) { delete[] a->samples; a->samples = nullptr; }
}

// Reads an entire file into a heap-allocated buffer; caller must delete[]
static uint8_t* mp3__read_file(const std::string& path, size_t* out_size) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return nullptr;

    f.seekg(0, std::ios::end);
    *out_size = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);

    uint8_t* buf = new uint8_t[*out_size];
    if (buf)
        f.read(reinterpret_cast<char*>(buf), *out_size);

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
            if (sample_rate == 0) {
                sample_rate = (uint32_t)info.hz;
                channels    = (uint32_t)info.channels;
            }
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
        const FLAC__StreamDecoder* dec,
        const FLAC__Frame* frame,
        const FLAC__int32* const buffer[],
        void* client_data)
{
    (void)dec;
    FlacCtx* ctx      = (FlacCtx*)client_data;
    uint32_t channels = frame->header.channels;
    uint32_t count    = frame->header.blocksize;
    uint32_t bps      = frame->header.bits_per_sample;
    float    scale    = 1.0f / (float)(1 << (bps - 1));

    // Grow buffer dynamically if needed (shouldn't happen when total_samples is known)
    uint64_t needed = ctx->written + (uint64_t)(count * channels);
    if (needed > ctx->capacity) {
        uint64_t new_cap = needed * 2;
        float* nb = (float*)realloc(ctx->buf, new_cap * sizeof(float));
        if (!nb) { ctx->error = 1; return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT; }
        ctx->buf      = nb;
        ctx->capacity = new_cap;
    }

    // Interleave channels: [L0, R0, L1, R1, ...]
    for (uint32_t i = 0; i < count; i++)
        for (uint32_t c = 0; c < channels; c++)
            ctx->buf[ctx->written++] = buffer[c][i] * scale;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

// Called by libFLAC when stream metadata is available — pre-allocates the buffer
static void flac__meta_cb(const FLAC__StreamDecoder* dec,
                          const FLAC__StreamMetadata* meta,
                          void* client_data)
{
    (void)dec;
    FlacCtx* ctx = (FlacCtx*)client_data;
    if (meta->type == FLAC__METADATA_TYPE_STREAMINFO) {
        ctx->channels        = meta->data.stream_info.channels;
        ctx->sample_rate     = meta->data.stream_info.sample_rate;
        ctx->bits_per_sample = meta->data.stream_info.bits_per_sample;
        uint64_t total       = meta->data.stream_info.total_samples * ctx->channels;
        if (total > 0) {
            ctx->buf      = new float[total];
            ctx->capacity = total;
        }
    }
}

// Called by libFLAC on decoding error — sets error flag to abort gracefully
static void flac__error_cb(const FLAC__StreamDecoder* dec,
                           FLAC__StreamDecoderErrorStatus status,
                           void* client_data)
{
    (void)dec; (void)status;
    ((FlacCtx*)client_data)->error = 1;
}

// Decodes a FLAC file to interleaved float PCM in [-1, 1]
static AudioSamples audio_flac_load(const std::string& filepath) {
    AudioSamples out = {0};

    FlacCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
    if (!dec) return out;

    FLAC__stream_decoder_set_md5_checking(dec, false);

    FLAC__StreamDecoderInitStatus status =
        FLAC__stream_decoder_init_file(dec, filepath.c_str(),
                                       flac__write_cb,
                                       flac__meta_cb,
                                       flac__error_cb,
                                       &ctx);
    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(dec);
        return out;
    }

    FLAC__stream_decoder_process_until_end_of_stream(dec);
    FLAC__stream_decoder_delete(dec);

    if (ctx.error || !ctx.buf || ctx.written == 0) {
        delete[] ctx.buf;
        return out;
    }

    out.samples     = ctx.buf;
    out.frame_count = ctx.written / ctx.channels;
    out.sample_rate = ctx.sample_rate;
    out.channels    = ctx.channels;
    return out;
}

// Little-endian read helpers for WAV parsing
static uint16_t wav__read_u16(std::ifstream& f) {
    uint8_t b[2];
    f.read(reinterpret_cast<char*>(b), 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t wav__read_u32(std::ifstream& f) {
    uint8_t b[4];
    f.read(reinterpret_cast<char*>(b), 4);
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
    wav__read_u32(f); // file size — unused
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
            wav__read_u32(f); // byte rate — unused
            wav__read_u16(f); // block align — unused
            bits_per_sample = wav__read_u16(f);
            if (size > 16) f.seekg((long)(size - 16), std::ios::cur);
        } else if (memcmp(id, "data", 4) == 0) {
            data_offset = f.tellg();
            data_size   = size;
            break;
        } else {
            f.seekg((long)size, std::ios::cur); // skip unknown chunks
        }
    }

    if ((audio_format != 1 && audio_format != 3) || data_offset == 0) return out;

    uint32_t bytes_per_sample = bits_per_sample / 8;
    uint64_t total_samples    = data_size / bytes_per_sample;
    uint64_t frame_count      = total_samples / channels;

    float* samples = new float[total_samples];

    f.seekg(data_offset, std::ios::beg);

    // Convert each sample to float [-1, 1] according to bit depth
    for (uint64_t i = 0; i < total_samples; i++) {
        if (audio_format == 3 && bits_per_sample == 32) {
            // IEEE 754 float — clamp to [-1, 1]
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
    out.frame_count = frame_count;
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

void Audio_load::mp3_load()
{
    AudioSamples audio = audio_mp3_load(m_file_path);
    if (!audio.samples) {
        std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n";
        return;
    }
    print_infos(audio);
    m_samples = audio.samples;
    m_audio   = audio;
}

void Audio_load::flac_load()
{
    AudioSamples audio = audio_flac_load(m_file_path);
    if (!audio.samples) {
        std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n";
        return;
    }
    print_infos(audio);
    m_samples = audio.samples;
    m_audio   = audio;
}

void Audio_load::wav_load()
{
    AudioSamples audio = audio_wav_load(m_file_path);
    if (!audio.samples) {
        std::cerr << "[Audio_load] ERROR : unable to load " << m_file_path << "\n";
        return;
    }
    print_infos(audio);
    m_samples = audio.samples;
    m_audio   = audio;
}

/*=== FFT ===*/

FFT::FFT(const std::string& file_path, const std::string& type)
    : Audio_load(file_path, type) {}

// Runs a full-signal FFT on channel 0 (mono, or left channel for stereo)
// Extracts every Nth sample (stride = channels) to deinterleave channel 0
// Returns frame_count complex coefficients — bin k represents frequency k * sr / N
std::vector<std::complex<float>> FFT::fft()
{
    if (!m_samples || m_audio.frame_count == 0) {
        std::cerr << "[FFT] ERROR: no audio loaded\n";
        return {};
    }

    uint64_t N        = m_audio.frame_count;
    uint32_t channels = m_audio.channels;

    // Build a real-valued input buffer from channel 0 only
    std::vector<float> input(N);
    for (uint64_t i = 0; i < N; i++)
        input[i] = m_samples[i * channels]; // stride by channel count to deinterleave

    kiss_fftr_cfg cfg = kiss_fftr_alloc((int)N, 0, nullptr, nullptr);

    // KissFFT real FFT output has N/2 + 1 unique bins (spectrum is symmetric for real input)
    std::vector<kiss_fft_cpx> out(N / 2 + 1);
    kiss_fftr(cfg, input.data(), out.data());
    free(cfg);

    std::vector<std::complex<float>> result(N / 2 + 1);
    for (uint64_t k = 0; k < N / 2 + 1; k++)
        result[k] = std::complex<float>(out[k].r, out[k].i);

    return result;
}

// Maps quality level to (window size, hop size) — higher quality = finer frequency resolution
static void fft__get_window_params(int quality, int& window_size, int& hop) {
    switch (quality) {
        case 0: window_size = 512;  hop = 256;  break;
        case 1: window_size = 1024; hop = 512;  break;
        case 2: window_size = 2048; hop = 1024; break;
        case 3: window_size = 4096; hop = 2048; break;
        default: window_size = 1024; hop = 512; break;
    }
}

// Builds a real-valued analysis window of length M
static std::vector<float> fft__create_window(int M, const std::string& type) {
    std::vector<float> w(M);
    if (type == "hann") {
        for (int m = 0; m < M; m++)
            w[m] = 0.5f - 0.5f * cosf(2.0f * M_PI * m / (M - 1));
    } else if (type == "hamming") {
        for (int m = 0; m < M; m++)
            w[m] = 0.54f - 0.46f * cosf(2.0f * M_PI * m / (M - 1));
    } else if (type == "blackman") {
        for (int m = 0; m < M; m++)
            w[m] = 0.42f - 0.5f  * cosf(2.0f * M_PI * m / (M - 1))
                         + 0.08f * cosf(4.0f * M_PI * m / (M - 1));
    } else {
        // Unknown window type — fall back to rectangular (no windowing)
        std::cerr << "[FFT] WARNING: unknown window '" << type << "', using rectangular\n";
        for (int m = 0; m < M; m++) w[m] = 1.0f;
    }
    return w;
}

// Computes the L2 norm of the window for energy normalization across different window types
static double fft__compute_norm(const std::vector<float>& w) {
    double norm = 0;
    for (float v : w) norm += v * v;
    return sqrt(norm);
}

// Runs a Short-Time Fourier Transform on channel 0 using the specified window and quality
// Output X[i][k]: complex spectrum at time frame i and frequency bin k
// Frequency of bin k (Hz): k * sample_rate / window_size
std::vector<std::vector<std::complex<float>>> FFT::stft(const std::string& window, int quality)
{
    if (!m_samples || m_audio.frame_count == 0) {
        std::cerr << "[FFT] ERROR: no audio loaded\n";
        return {};
    }

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop);

    uint32_t channels  = m_audio.channels;
    uint64_t N         = m_audio.frame_count;
    int      num_frames = ((int)N - window_size) / hop + 1;

    std::vector<float> w    = fft__create_window(window_size, window);
    double             norm = fft__compute_norm(w);

    kiss_fftr_cfg             cfg = kiss_fftr_alloc(window_size, 0, nullptr, nullptr);
    std::vector<kiss_fft_cpx> fft_out(window_size / 2 + 1);
    std::vector<float>        frame_buf(window_size);

    std::vector<std::vector<std::complex<float>>> X(
        num_frames, std::vector<std::complex<float>>(window_size / 2 + 1));

    for (int i = 0; i < num_frames; i++) {
        int start = i * hop;
        // Deinterleave channel 0 and apply window
        for (int m = 0; m < window_size; m++)
            frame_buf[m] = m_samples[(start + m) * channels] * w[m];

        kiss_fftr(cfg, frame_buf.data(), fft_out.data());

        // Normalize by window L2 norm so magnitude is consistent across window types
        for (int k = 0; k < window_size / 2 + 1; k++)
            X[i][k] = std::complex<float>(fft_out[k].r / norm, fft_out[k].i / norm);
    }

    free(cfg);
    return X;
}

/*=== Energy ===*/
std::vector<std::complex<float>> Energy::global(const std::string& window = "hann", int quality = 1,
                                                    int &band1 = -1, int &band2 = -1)
{
    std::vector<std::vector<std::complex<float>>> samples = stft(window, quality);
    std::vector<std::complex<float>> energy(samples.size(), 0.0f);

    if (band1 == -1)
        band1 = 0;
    if (band2 == -1 && !samples.empty())
        band2 = samples[0].size();

    for (int n = 0; n < samples.size(); n++)
    {
        energy[n] = 0.0f;
        for (int k = band1; k < band2; k++)
        {
            energy[n] += abs(pow(samples[n][k], 2));
        }
    }
    return energy;
}

std::vector<std::complex<float>> Energy::global_rms(const std::string& window = "hann", int quality = 1,
                                                int &band1 = -1, int &band2 = -1)
{
    std::vector<std::vector<std::complex<float>>> samples = stft(window, quality);
    std::vector<std::complex<float>> energy(samples.size(), 0.0f);

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop);

    if (band1 == -1)
        band1 = 0;
    if (band2 == -1 && !samples.empty())
        band2 = samples[0].size();

    for (int n = 0; n < samples.size(); n++)
    {
        energy[n] = 0.0f;
        for (int k = band1; k < band2; k++)
        {
            energy[n] += abs(pow(samples[n][k], 2));
        }
        energy[n] = sqrt((1/window_size) * energy[n]);
    }
    return energy;
}

std::vector<std::complex<float>> Energy::Z_score(const std::string& window = "hann", int quality = 1, 
                        const std::string& type = "global", int &band1 = -1, int &band2 = -1)
{
    std::vector<std::complex<float>> energy;
    if (type == "global") {
        energy = global(window, quality, band1, band2);
    } else if (type == "global_rms") {
        energy = global_rms(window, quality, band1, band2);
    } else {
        energy = global(window, quality, band1, band2);
    }

    float sum = 0.0f;
    for (int n = 0; n < energy.size(); n++)
        sum += std::real(energy[n]);
    float mean = sum / energy.size();

    float sq_sum = 0.0f;
    for (int n = 0; n < energy.size(); n++) {
        float diff = std::real(energy[n]) - mean;
        sq_sum += diff * diff;
    }
    float std_dev = sqrt(sq_sum / energy.size());
    
    std::vector<std::complex<float>> Z_energy(energy.size());
    for (int n = 0; n < energy.size(); n++)
        Z_energy[n] = std::complex<float>((std::real(energy[n]) - mean) / std_dev, 0.0f);
    return Z_energy;
}
/*=== Energy ===*/

Energy::Energy(const std::string& file_path, const std::string& type)
    : FFT(file_path, type) {}

// Clamps bin bounds to valid range — replaces -1 with full spectrum bounds
static void energy__clamp_bands(int num_bins, int& band1, int& band2) {
    if (band1 < 0 || band1 >= num_bins) band1 = 0;
    if (band2 < 0 || band2 >= num_bins) band2 = num_bins - 1;
    if (band1 > band2) std::swap(band1, band2);
}

std::vector<float> Energy::global(const std::string& window, int quality,
                                  int band1, int band2)
{
    auto X = stft(window, quality);
    if (X.empty()) return {};

    int num_bins = (int)X[0].size();
    energy__clamp_bands(num_bins, band1, band2);

    std::vector<float> E(X.size());
    for (int n = 0; n < (int)X.size(); n++) {
        float sum = 0.0f;
        for (int k = band1; k <= band2; k++)
            sum += std::norm(X[n][k]); // std::norm = |z|^2 for complex
        E[n] = sum;
    }
    return E;
}

std::vector<float> Energy::global_rms(const std::string& window, int quality,
                                      int band1, int band2)
{
    auto X = stft(window, quality);
    if (X.empty()) return {};

    int num_bins = (int)X[0].size();
    energy__clamp_bands(num_bins, band1, band2);

    int M = band2 - band1 + 1; // number of bins in the range

    std::vector<float> E(X.size());
    for (int n = 0; n < (int)X.size(); n++) {
        float sum = 0.0f;
        for (int k = band1; k <= band2; k++)
            sum += std::norm(X[n][k]);
        E[n] = sqrtf(sum / (float)M);
    }
    return E;
}

std::vector<float> Energy::Z_score(const std::vector<float>& energy)
{
    if (energy.empty()) return {};

    // Compute mean
    float mean = 0.0f;
    for (float e : energy) mean += e;
    mean /= (float)energy.size();

    // Compute standard deviation
    float variance = 0.0f;
    for (float e : energy) variance += (e - mean) * (e - mean);
    float sigma = sqrtf(variance / (float)energy.size());

    // Avoid division by zero if all values are identical
    if (sigma == 0.0f) return std::vector<float>(energy.size(), 0.0f);

    std::vector<float> Z(energy.size());
    for (int n = 0; n < (int)energy.size(); n++)
        Z[n] = (energy[n] - mean) / sigma;

    return Z;
}

/*=== Intensity ===*/
std::vector<float> Intensity::base(const std::vector<float>& energy, float acceleration_param,
                        float velocity_param, float intensity_param)
{
    // normalize weights
    if (acceleration_param + velocity_param + intensity_param != 0.0f) 
    {
        float adjustment = acceleration_param + velocity_param + intensity_param;
        acceleration_param /= adjustment;
        velocity_param     /= adjustment;
        intensity_param    /= adjustment;
    }
    auto max_energy = std::max_element(energy.begin(), energy.end());
    float maxE = (max_energy == energy.end() ? 1.0f : *max_energy + 1e-6f);

    std::vector<float> norm(energy.size());
    for (int n = 0; n < (int)energy.size(); n++)
        norm[n] = energy[n] / maxE;

    std::vector<float> velocity(energy.size(), 0.0f);
    for (int n = 1; n < (int)energy.size(); n++)
        velocity[n] = (norm[n] - norm[n - 1]) / (energy[n] + 1e-6f);

    std::vector<float> acceleration(energy.size(), 0.0f);
    for (int n = 1; n < (int)energy.size(); n++)
        acceleration[n] = velocity[n] - velocity[n - 1];

    std::vector<float> intensity(energy.size(), 0.0f);
    for (int n = 0; n < (int)energy.size(); n++)
        intensity[n] = intensity_param * norm[n]
                     + velocity_param * velocity[n]
                     + acceleration_param * acceleration[n];
    auto max_intensity = std::max_element(intensity.begin(), intensity.end());
    if (max_intensity != intensity.end()) {
        float mx = *max_intensity;
        for (int n = 0; n < (int)energy.size(); n++)
            intensity[n] /= mx;
    }
    return intensity;
}

std::vector<float> Intensity::velocity(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       char type)
{
    if (velocity_param + intensity_param != 0.0f) {
        float adjustment = velocity_param + intensity_param;
        velocity_param   /= adjustment;
        intensity_param  /= adjustment;
    }

    auto max_intensity = std::max_element(intensity.begin(), intensity.end());
    float maxI = (max_intensity == intensity.end() ? 1.0f : *max_intensity + 1e-6f);

    std::vector<float> velocity(intensity.size(), 0.0f);
    for (int n = 1; n < (int)intensity.size(); n++)
        velocity[n] = (intensity[n] - intensity[n - 1]) / maxI;

    switch (type) {
        case '+':
            for (int n = 0; n < (int)intensity.size(); n++)
                velocity[n] = intensity_param * intensity[n] + velocity_param * velocity[n];
            break;
        case '*':
            for (int n = 0; n < (int)intensity.size(); n++)
                velocity[n] = intensity[n] * (1 + velocity_param * velocity[n]);
            break;
        default:
            for (int n = 0; n < (int)intensity.size(); n++)
                velocity[n] = intensity_param * intensity[n] + velocity_param * velocity[n];
            break;
    }
    return velocity;
}

std::vector<float> Intensity::acceleration(const std::vector<float>& intensity,
                                           float intensity_param,
                                           float acceleration_param,
                                           char type)
{
    // normalize parameters as before
    if (acceleration_param + intensity_param != 0.0f) {
        float adjustment = acceleration_param + intensity_param;
        acceleration_param /= adjustment;
        intensity_param    /= adjustment;
    }

    auto max_intensity = std::max_element(intensity.begin(), intensity.end());
    float maxI = (max_intensity == intensity.end() ? 1.0f : *max_intensity + 1e-10f);
    std::vector<float> vel(intensity.size(), 0.0f);
    for (int n = 1; n < (int)intensity.size(); n++)
        vel[n] = (intensity[n] - intensity[n - 1]) / maxI;

    auto max_vel_it = std::max_element(vel.begin(), vel.end());
    float maxV = (max_vel_it == vel.end() ? 1.0f : *max_vel_it + 1e-10f);

    std::vector<float> accel(intensity.size(), 0.0f);
    for (int n = 1; n < (int)intensity.size(); n++)
        accel[n] = fabs(vel[n] - vel[n - 1]) / maxV;

    switch (type) {
        case '+':
            for (int n = 0; n < (int)intensity.size(); n++)
                accel[n] = intensity_param * intensity[n] + acceleration_param * accel[n];
            break;
        case '*':
            for (int n = 0; n < (int)intensity.size(); n++)
                accel[n] = intensity[n] * (1 + acceleration_param * accel[n]);
            break;
        default:
            for (int n = 0; n < (int)intensity.size(); n++)
                accel[n] = intensity_param * intensity[n] + acceleration_param * accel[n];
            break;
    }

    return accel;
}

std::vector<float> Intensity::combined(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       float acceleration_param,
                                       char type)
{
    std::vector<float> vel = velocity(intensity, intensity_param, velocity_param, type);
    std::vector<float> acc = acceleration(intensity, intensity_param, acceleration_param, type);

    std::vector<float> combined(intensity.size(), 0.0f);
    switch (type)
    {
    case "+": 
        for (int n = 0; n < (int)intensity.size(); n++)
            combined[n] = intensity_param * intensity[n] + velocity_param * vel[n] + acceleration_param * acc[n];
        break;
    case "*":
        for (int n = 0; n < (int)intensity.size(); n++)
            combined[n] = intensity[n] * (1 + velocity_param * vel[n] + acceleration_param * acc[n]);
        break;
    default:
        for (int n = 0; n < (int)intensity.size(); n++)
            combined[n] = intensity_param * intensity[n] + velocity_param * vel[n] + acceleration_param * acc[n];
        break;
    }
    return combined;
}

std::vector<float> Intensity::smooth(const std::vector<float>& intensity, float smoothing_param)
{
    if (smoothing_param <= 0.0f || smoothing_param >= 1.0f)
        smoothing_param = std::clamp(smoothing_param, 0.0f, 1.0f);
    std::vector<float> smoothed(intensity.size(), 0.0f);

    smoothed[0] = intensity[0];
    for (int n = 1; n < (int)intensity.size(); n++)
    {
        smoothed[n] = smoothing_param * intensity[n] + (1.0f - smoothing_param) * smoothed[n-1];
    }
    return smoothed;
}

std::vector<float> Intensity::relative(std::vector<float>& intensity, std::vector<float>& smoothed_intensity)
{
    int n(0);
    if (intensity >= smoothed_intensity)
        n = smoothed_intensity.size();
    else
        n = intensity.size();
    
    std::vector<float> relative(n, 0.0f);

    for (int i=0; i < n; i++)
        relative[i] = intensity[i] / smoothed_intensity[i];
    return relative;
}

static std::vector<float> Intensity::centered(std::vector<float>& intensity, std::vector<float>& smoothed_intensity)
{
    int n(0);
    if (intensity >= smoothed_intensity)
        n = smoothed_intensity.size();
    else
        n = intensity.size();
    
    std::vector<float> relative(n, 0.0f);

    for (int i=0; i < n; i++)
        relative[i] = intensity[i] - smoothed_intensity[i];
    //Cela donne l’énergie excédentaire
    return relative;
}

static std::vector<float> Intensity::logarithmic(std::vector<float>& intensity, char rms)
{
    int rms(10);
    switch (rms)
    {
    case "o" : rms = 20; break;
    default: break;
    }
    std::vector<float> logarithm(intensity.size(), 0.0f);
    for (int n=0; n < intensity.size; n++)
        logarithm[n] = rms * log10(intensity[n] + 1e-10f);
    return logarithm;
}

std::vector<float> Intensity::derived(std::vector<float>& intensity)
{
    std::vector<float> ddx(intensity.size(), 0.0f);
    for (int n = 1; n < intensity.size(); n++)
        ddx[n] = intensity[n] - intensity[n-1];
    return ddx;
}

std::vector<float> Intensity::spectral(std::vector<std::vector<std::complex<float>>>& stft, int quality)
{
    std::vector<float> spec(stft.size(), 0.0f);
    float flow;
    float sum;

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop)
    for (int n=1; n < stft.size(), n++)
    {
        sum=0;
        for (int k=0; k < hop; k++)
        {
            flow = abs(stft[n][k]) - abs(stft[n-1][k]);
            if (flow > 0)
                sum += flow;
        }
        spec[n] = sum;
    }
    return spec;
}

std::vector<float> Intensity::spec_diff(std::vector<std::vector<std::complex<float>>>& stft, int quality)
{
    std::vector<float> spec(stft.size(), 0.0f);
    float sum;

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop)
    for (int n=1; n < stft.size(), n++)
    {
        sum=0;
        for (int k=0; k < hop; k++)
        {
            sum += pow(stft[n][k] - stft[n-1][k], 2);
        }
        spec[n] = sum;
    }
    return spec;
}

std::vector<float> Intensity::log_spec(std::vector<std::vector<std::complex<float>>>& stft, int quality)
{
    std::vector<float> spec(stft.size(), 0.0f);
    float flow;
    float sum;

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop)
    for (int n=1; n < stft.size(), n++)
    {
        sum=0;
        for (int k=0; k < hop; k++)
        {
            flow = log10(abs(stft[n][k])) - log10(abs(stft[n-1][k]));
            if (flow > 0)
                sum += flow;
        }
        spec[n] = sum;
    }
    return spec;
}

std::vector<float> Intensity::high_frequency(std::vector<std::vector<std::complex<float>>>& stft, int quality)
{
    std::vector<float> hfc(stft.size(), 0.0f);
    float sum;

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop)
    for (int n=1; n < stft.size(), n++)
    {
        sum=0;
        for (int k=0; k < hop; k++)
        {
            sum += k * pow(abs(stft[n][k]), 2)
        }
        hfc[n] = sum;
    }
    return hfc;
}
/*=== Intensity ===*/

// Clamps a value to [0, 1] after normalization
static float intensity__clamp(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Normalizes a vector by its maximum value — returns unchanged if max is 0
static std::vector<float> intensity__norm_by_max(std::vector<float> v) {
    float mx = *std::max_element(v.begin(), v.end());
    if (mx == 0.f) return v;
    for (float& x : v) x /= mx;
    return v;
}

// Computes first-order discrete difference: D[n] = V[n] - V[n-1], D[0] = 0
static std::vector<float> intensity__diff(const std::vector<float>& v) {
    std::vector<float> d(v.size(), 0.f);
    for (int n = 1; n < (int)v.size(); n++)
        d[n] = v[n] - v[n-1];
    return d;
}

std::vector<float> Intensity::base(const std::vector<float>& energy,
                                   float acceleration_param,
                                   float velocity_param,
                                   float intensity_param)
{
    if (energy.empty()) return {};

    // Normalize weights so alpha + beta + gamma = 1
    float total = intensity_param + velocity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param   / total;
    float beta  = velocity_param    / total;
    float gamma = acceleration_param / total;

    // Normalize energy to [0, 1] by max
    std::vector<float> E_norm = intensity__norm_by_max(energy);

    // Velocity V[n] = E_norm[n] - E_norm[n-1], normalized to [0, 1]
    std::vector<float> V = intensity__norm_by_max(intensity__diff(E_norm));

    // Acceleration A[n] = V[n] - V[n-1], normalized to [0, 1]
    std::vector<float> A = intensity__norm_by_max(intensity__diff(V));

    // Weighted combination
    std::vector<float> I(energy.size());
    for (int n = 0; n < (int)I.size(); n++)
        I[n] = alpha * E_norm[n] + beta * V[n] + gamma * A[n];

    // Final normalization by max
    return intensity__norm_by_max(I);
}

std::vector<float> Intensity::velocity(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       char type)
{
    if (intensity.empty()) return {};

    // Normalize weights
    float total = intensity_param + velocity_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param / total;
    float beta  = velocity_param  / total;

    std::vector<float> V = intensity__norm_by_max(intensity__diff(intensity));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + beta * V[n]); // multiplicative modulation
        else
            out[n] = alpha * intensity[n] + beta * V[n]; // additive blend (default)
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::acceleration(const std::vector<float>& intensity,
                                           float intensity_param,
                                           float acceleration_param,
                                           char type)
{
    if (intensity.empty()) return {};

    float total = intensity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param    / total;
    float gamma = acceleration_param / total;

    // Acceleration = second-order difference of intensity
    std::vector<float> V = intensity__diff(intensity);
    std::vector<float> A = intensity__norm_by_max(intensity__diff(V));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + gamma * A[n]);
        else
            out[n] = alpha * intensity[n] + gamma * A[n];
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::combined(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       float acceleration_param,
                                       char type)
{
    if (intensity.empty()) return {};

    float total = intensity_param + velocity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param    / total;
    float beta  = velocity_param     / total;
    float gamma = acceleration_param / total;

    std::vector<float> V = intensity__norm_by_max(intensity__diff(intensity));
    std::vector<float> A = intensity__norm_by_max(intensity__diff(V));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + beta * V[n] + gamma * A[n]);
        else
            out[n] = alpha * intensity[n] + beta * V[n] + gamma * A[n];
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::smooth(const std::vector<float>& intensity,
                                     float smoothing_param)
{
    if (intensity.empty()) return {};

    // Map smoothing_param [0, 1] to window size [1, N/2]
    int N    = (int)intensity.size();
    int half = std::max(1, (int)(smoothing_param * (N / 2)));
    int win  = 2 * half + 1;

    std::vector<float> out(N, 0.f);
    for (int n = 0; n < N; n++) {
        int lo    = std::max(0, n - half);
        int hi    = std::min(N - 1, n + half);
        float sum = 0.f;
        for (int i = lo; i <= hi; i++) sum += intensity[i];
        out[n] = sum / (float)(hi - lo + 1);
    }
    return out;
}

std::vector<float> Intensity::relative(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity)
{
    if (intensity.size() != smoothed_intensity.size()) return {};
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        // Avoid division by zero
        out[n] = (smoothed_intensity[n] != 0.f) ? intensity[n] / smoothed_intensity[n] : 0.f;
    }
    return out;
}

std::vector<float> Intensity::centered(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity)
{
    if (intensity.size() != smoothed_intensity.size()) return {};
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++)
        out[n] = intensity[n] - smoothed_intensity[n];
    return out;
}

std::vector<float> Intensity::derived(const std::vector<float>& intensity)
{
    // Raw discrete derivative — no normalization or weighting
    return intensity__diff(intensity);
}

std::vector<float> Intensity::logarithmic(const std::vector<float>& intensity, char rms)
{
    float factor = (rms == 'r') ? 20.f : 10.f; // 'r' = amplitude (RMS), else power
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        // Clamp to small positive value to avoid log(0) = -inf
        float v = (intensity[n] > 1e-10f) ? intensity[n] : 1e-10f;
        out[n] = factor * log10f(v);
    }
    return out;
}

/*=== SpectralIntensity ===*/

std::vector<float> SpectralIntensity::spectral_flux(
    const std::vector<std::vector<std::complex<float>>>& stft)
{
    if (stft.empty()) return {};
    int N = (int)stft.size();
    std::vector<float> out(N, 0.f);

    for (int n = 1; n < N; n++) {
        int bins = (int)stft[n].size();
        for (int k = 0; k < bins; k++) {
            float diff = std::abs(stft[n][k]) - std::abs(stft[n-1][k]);
            if (diff > 0.f) out[n] += diff; // only count increases
        }
    }
    return out;
}

std::vector<float> SpectralIntensity::spectral_diff(
    const std::vector<std::vector<std::complex<float>>>& stft)
{
    if (stft.empty()) return {};
    int N = (int)stft.size();
    std::vector<float> out(N, 0.f);

    for (int n = 1; n < N; n++) {
        int bins = (int)stft[n].size();
        for (int k = 0; k < bins; k++)
            out[n] += std::abs(std::abs(stft[n][k]) - std::abs(stft[n-1][k]));
    }
    return out;
}

std::vector<float> SpectralIntensity::log_spectral_flux(
    const std::vector<std::vector<std::complex<float>>>& stft)
{
    if (stft.empty()) return {};
    int N = (int)stft.size();
    std::vector<float> out(N, 0.f);

    for (int n = 1; n < N; n++) {
        int bins = (int)stft[n].size();
        for (int k = 0; k < bins; k++) {
            float log_curr = log1pf(std::abs(stft[n][k]));   // log(1 + |X[n][k]|)
            float log_prev = log1pf(std::abs(stft[n-1][k]));
            float diff = log_curr - log_prev;
            if (diff > 0.f) out[n] += diff;
        }
    }
    return out;
}

std::vector<float> SpectralIntensity::high_frequency_content(
    const std::vector<std::vector<std::complex<float>>>& stft)
{
    if (stft.empty()) return {};
    int N = (int)stft.size();
    std::vector<float> out(N, 0.f);

    for (int n = 0; n < N; n++) {
        int bins = (int)stft[n].size();
        for (int k = 0; k < bins; k++)
            out[n] += (float)k * std::norm(stft[n][k]); // k * |X[n][k]|^2
    }
    return out;
}
