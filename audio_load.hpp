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
    // Inherits Audio_load constructor — same signature
    FFT(const std::string& file_path, const std::string& type);

    // Full-signal FFT on channel 0 (mono or left channel for stereo)
    // Returns a vector of frame_count complex<float> coefficients
    // Magnitude of bin k: std::abs(result[k])
    // Frequency of bin k: k * sample_rate / frame_count  (in Hz)
    std::vector<std::complex<float>> fft();

    // Short-Time Fourier Transform
    // window  : "hann" | "hamming" | "blackman"
    // quality : 0 -> window 512  | 1 -> 1024 | 2 -> 2048 | 3 -> 4096
    //           hop is always window_size / 2
    // Returns : vector[num_frames][num_bins]  where num_bins = window_size / 2 + 1
    std::vector<std::vector<std::complex<float>>> stft(const std::string& window = "hann",
                                                       int quality = 1);
};
