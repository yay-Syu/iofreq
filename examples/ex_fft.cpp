#include "fft.hpp"
#include <iostream>
#include <complex>
#include <cmath>

int main()
{
    FFT fft("sample.wav", "wav");
    fft.wav_load();

    // Full-signal FFT — returns N/2 + 1 complex coefficients
    // Frequency of bin k (Hz) = k * sample_rate / frame_count
    auto spectrum = fft.fft();
    std::cout << "fft bins   : " << spectrum.size() << "\n";
    if (!spectrum.empty())
        std::cout << "bin[1] mag : " << std::abs(spectrum[1]) << "\n";

    // STFT — window: "hann" | "hamming" | "blackman"
    // quality: 0=512 | 1=1024 | 2=2048 | 3=4096  (hop = window / 2)
    auto s_hann     = fft.stft("hann",     1);
    auto s_hamming  = fft.stft("hamming",  2);
    auto s_blackman = fft.stft("blackman", 3);

    std::cout << "stft hann     : " << s_hann.size()     << " frames, "
              << (s_hann.empty()     ? 0 : s_hann[0].size())     << " bins\n";
    std::cout << "stft hamming  : " << s_hamming.size()  << " frames, "
              << (s_hamming.empty()  ? 0 : s_hamming[0].size())  << " bins\n";
    std::cout << "stft blackman : " << s_blackman.size() << " frames, "
              << (s_blackman.empty() ? 0 : s_blackman[0].size()) << " bins\n";

    return 0;
}
