#include "audio_load.hpp"
#include <iostream>
#include <complex>
#include <cmath>

int main()
{
    // ------------------------------------------------------------------ MP3
    std::cout << "=== MP3 — FFT + STFT ===\n";
    FFT mp3("sample.mp3", "mp3");
    mp3.mp3_load();

    // Full-signal FFT (channel 0 only)
    auto spectrum = mp3.fft();
    std::cout << "fft bins     : " << spectrum.size() << "\n";
    if (!spectrum.empty())
        std::cout << "bin[1] mag   : " << std::abs(spectrum[1]) << "\n";

    // STFT with each window type
    auto s_hann     = mp3.stft("hann",     1);
    auto s_hamming  = mp3.stft("hamming",  2);
    auto s_blackman = mp3.stft("blackman", 3);

    std::cout << "stft hann     : " << s_hann.size()     << " frames, "
              << (s_hann.empty()     ? 0 : s_hann[0].size())     << " bins\n";
    std::cout << "stft hamming  : " << s_hamming.size()  << " frames, "
              << (s_hamming.empty()  ? 0 : s_hamming[0].size())  << " bins\n";
    std::cout << "stft blackman : " << s_blackman.size() << " frames, "
              << (s_blackman.empty() ? 0 : s_blackman[0].size()) << " bins\n";

    // ------------------------------------------------------------------ FLAC
    std::cout << "\n=== FLAC — STFT ===\n";
    FFT flac("sample.flac", "flac");
    flac.flac_load();

    auto s_flac = flac.stft("hann", 1);
    std::cout << "stft hann : " << s_flac.size() << " frames, "
              << (s_flac.empty() ? 0 : s_flac[0].size()) << " bins\n";

    // ------------------------------------------------------------------ WAV
    std::cout << "\n=== WAV — FFT + STFT ===\n";
    FFT wav("sample.wav", "wav");
    wav.wav_load();

    auto spectrum_wav = wav.fft();
    std::cout << "fft bins  : " << spectrum_wav.size() << "\n";

    auto s_wav = wav.stft("blackman", 0); // quality 0 = fastest
    std::cout << "stft blackman (quality 0) : " << s_wav.size() << " frames, "
              << (s_wav.empty() ? 0 : s_wav[0].size()) << " bins\n";

    // ------------------------------------------------------------------ Audio_load only (no FFT)
    std::cout << "\n=== Audio_load only ===\n";
    Audio_load loader("sample.wav", "wav");
    loader.wav_load();
    // loader.fft() — not available, Audio_load has no FFT methods

    return 0;
}
