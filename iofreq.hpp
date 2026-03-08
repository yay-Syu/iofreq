#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <complex>
#include <cstdint>
#include "kiss_fftr.h"

// Holds raw decoded audio — samples are interleaved float PCM normalized to [-1, 1]
typedef struct {
    float*   samples;      // interleaved PCM: [L0, R0, L1, R1, ...] for stereo
    uint64_t frame_count;  // number of frames (samples per channel)
    uint32_t sample_rate;  // e.g. 44100, 48000
    uint32_t channels;     // 1 = mono, 2 = stereo
} AudioSamples;

// ----------------------------------------------------------------------------
// Audio_load — decodes audio files into float PCM
// ----------------------------------------------------------------------------
class Audio_load
{
public:
    Audio_load(const std::string& file_path, const std::string& type);

    void mp3_load();
    void flac_load();
    void wav_load();

    // Prints sample rate, channel count, frame count and duration to stdout
    static void print_infos(AudioSamples& audio);

protected:
    std::string  m_file_path;
    std::string  m_type;
    AudioSamples m_audio;
    float*       m_samples = nullptr;
};

// ----------------------------------------------------------------------------
// FFT — extends Audio_load with frequency-domain analysis
// ----------------------------------------------------------------------------
class FFT : public Audio_load
{
public:
    FFT(const std::string& file_path, const std::string& type);

    // Full-signal FFT on channel 0 (mono or left channel for stereo)
    // Returns a vector of (frame_count / 2 + 1) complex<float> coefficients
    // Magnitude of bin k : std::abs(result[k])
    // Frequency of bin k : k * sample_rate / frame_count  (in Hz)
    std::vector<std::complex<float>> fft();

    // Short-Time Fourier Transform on channel 0
    // window  : "hann" | "hamming" | "blackman"
    // quality : 0 -> 512 | 1 -> 1024 | 2 -> 2048 | 3 -> 4096  (hop = window / 2)
    // Returns : vector[num_frames][num_bins]  where num_bins = window_size / 2 + 1
    std::vector<std::vector<std::complex<float>>> stft(const std::string& window = "hann",
                                                       int quality = 1);
};

// ----------------------------------------------------------------------------
// Energy — extends FFT with spectral energy analysis
// ----------------------------------------------------------------------------
class Energy : public FFT
{
public:
    Energy(const std::string& file_path, const std::string& type);

    // E[n] = sum(|X[n][k]|^2)  for k in [band1, band2]
    // band1, band2 : frequency bin bounds — set to -1 to use the full spectrum
    // Returns a vector of size num_frames
    std::vector<float> global(const std::string& window = "hann", int quality = 1,
                              int band1 = -1, int band2 = -1);

    // E[n] = sqrt( (1/M) * sum(|X[n][k]|^2) )  for k in [band1, band2]
    // M = number of bins in the range (band2 - band1 + 1)
    // Normalizes energy across different window sizes for amplitude comparison
    // Returns a vector of size num_frames
    std::vector<float> global_rms(const std::string& window = "hann", int quality = 1,
                                  int band1 = -1, int band2 = -1);

    // Z[n] = (E[n] - mean(E)) / std(E)
    // Centers and scales the energy vector — useful to detect local energy peaks
    // energy : pre-computed energy vector from global() or global_rms()
    // Returns a vector of size num_frames
    static std::vector<float> Z_score(const std::vector<float>& energy);
};

// ----------------------------------------------------------------------------
// Intensity — time-domain intensity analysis (standalone, no inheritance)
// All methods operate on pre-computed energy or intensity vectors
// ----------------------------------------------------------------------------
class Intensity
{
public:
    // Computes a weighted combination of normalized energy, velocity and acceleration
    // I[n] = alpha * E_norm[n] + beta * V[n] + gamma * A[n]
    // Weights (intensity_param, velocity_param, acceleration_param) are auto-normalized so they sum to 1
    // Final result is normalized by max(I) to stay in [0, 1]
    static std::vector<float> base(const std::vector<float>& energy,
                                   float acceleration_param,
                                   float velocity_param,
                                   float intensity_param);

    // Modulates intensity with its own velocity (first-order difference of intensity)
    // type '+' : additive   I'[n] = alpha * I[n] + beta * V[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + beta * V[n])
    static std::vector<float> velocity(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       char type);

    // Modulates intensity with its acceleration (first-order difference of velocity)
    // Requires a velocity vector computed beforehand
    // type '+' : additive   I'[n] = alpha * I[n] + gamma * A[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + gamma * A[n])
    static std::vector<float> acceleration(const std::vector<float>& intensity,
                                           float intensity_param,
                                           float acceleration_param,
                                           char type);

    // Weighted combination of intensity, velocity and acceleration in one pass
    // Internally computes velocity and acceleration from the input intensity
    // type '+' : additive   I'[n] = alpha * I[n] + beta * V[n] + gamma * A[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + beta * V[n] + gamma * A[n])
    static std::vector<float> combined(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       float acceleration_param,
                                       char type);

    // Moving average smoothing — window size derived from smoothing_param in [0, 1]
    // smoothing_param = 0 -> no smoothing | 1 -> maximum smoothing
    // Do not use on a vector already produced by combined(), which is smoothed internally
    static std::vector<float> smooth(const std::vector<float>& intensity,
                                     float smoothing_param);

    // R[n] = I[n] / I_smooth[n]
    // ~1 = normal energy | >1 = energy peak | <1 = low energy
    // Measures how much the current intensity exceeds the local average
    static std::vector<float> relative(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity);

    // C[n] = I[n] - I_smooth[n]
    // Excess energy above the local mean — positive = peak, negative = dip
    static std::vector<float> centered(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity);

    // D[n] = I[n] - I[n-1]
    // Raw discrete derivative — simpler than velocity(), no weighting or normalization
    // Useful for quick onset detection
    static std::vector<float> derived(const std::vector<float>& intensity);

    // Logarithmic compression of intensity
    // rms 'e' : power scale  I_log[n] = 10 * log10(I[n])
    // rms 'r' : amplitude scale  I_log[n] = 20 * log10(I[n])
    // Clamps values to avoid log(0)
    static std::vector<float> logarithmic(const std::vector<float>& intensity, char rms);
};

// ----------------------------------------------------------------------------
// SpectralIntensity — frequency-domain intensity analysis (standalone)
// All methods take a pre-computed STFT — vector[num_frames][num_bins]
// ----------------------------------------------------------------------------
class SpectralIntensity
{
public:
    // Spectral flux — sum of positive magnitude increases between consecutive frames
    // SF[n] = sum_k( max(0, |X[n][k]| - |X[n-1][k]|) )
    // Detects onsets: spikes when many frequency bins increase simultaneously
    static std::vector<float> spectral_flux(
        const std::vector<std::vector<std::complex<float>>>& stft);

    // Spectral difference — total magnitude change between consecutive frames
    // SD[n] = sum_k( ||X[n][k]| - |X[n-1][k]|| )
    // Detects any global spectral change, not just increases
    static std::vector<float> spectral_diff(
        const std::vector<std::vector<std::complex<float>>>& stft);

    // Log spectral flux — spectral flux computed on log-magnitude spectrum
    // More robust to volume variations than linear spectral flux
    // LF[n] = sum_k( max(0, log(1 + |X[n][k]|) - log(1 + |X[n-1][k]|)) )
    static std::vector<float> log_spectral_flux(
        const std::vector<std::vector<std::complex<float>>>& stft);

    // High Frequency Content — weights each bin by its frequency index
    // HFC[n] = sum_k( k * |X[n][k]|^2 )
    // Emphasizes high-frequency energy — useful for detecting sharp transients
    static std::vector<float> high_frequency_content(
        const std::vector<std::vector<std::complex<float>>>& stft);
};
