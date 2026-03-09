#include "fft.hpp"
#include <iostream>
#include <cmath>
#include "kiss_fftr.h"

FFT::FFT(const std::string& file_path, const std::string& type)
    : Audio_load(file_path, type) {}

// Runs a full-signal FFT on channel 0
// Extracts every Nth sample (stride = channels) to deinterleave channel 0
// Returns frame_count/2 + 1 complex coefficients — bin k = frequency k * sr / N
std::vector<std::complex<float>> FFT::fft()
{
    if (!m_samples || m_audio.frame_count == 0) {
        std::cerr << "[FFT] ERROR: no audio loaded\n"; return {};
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

// Maps quality level to (window size, hop size)
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

// Computes the L2 norm of the window for energy normalization across window types
static double fft__compute_norm(const std::vector<float>& w) {
    double norm = 0;
    for (float v : w) norm += v * v;
    return sqrt(norm);
}

// Runs a Short-Time Fourier Transform on channel 0
// Output X[i][k]: complex spectrum at time frame i and frequency bin k
// Frequency of bin k (Hz): k * sample_rate / window_size
std::vector<std::vector<std::complex<float>>> FFT::stft(const std::string& window, int quality)
{
    if (!m_samples || m_audio.frame_count == 0) {
        std::cerr << "[FFT] ERROR: no audio loaded\n"; return {};
    }

    int window_size, hop;
    fft__get_window_params(quality, window_size, hop);

    uint32_t channels   = m_audio.channels;
    uint64_t N          = m_audio.frame_count;
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
