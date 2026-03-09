#pragma once
#include "audio_load.hpp"
#include <vector>
#include <complex>
#include "kiss_fftr.h"

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
