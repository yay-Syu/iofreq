#include "iofreq.hpp"
#include <iostream>
#include <complex>
#include <cmath>

int main()
{
    // ------------------------------------------------------------------ MP3
    std::cout << "=== MP3 — FFT + STFT ===\n";
    FFT mp3("sample.mp3", "mp3");
    mp3.mp3_load();

    auto spectrum = mp3.fft();
    std::cout << "fft bins     : " << spectrum.size() << "\n";
    if (!spectrum.empty())
        std::cout << "bin[1] mag   : " << std::abs(spectrum[1]) << "\n";

    auto s_hann     = mp3.stft("hann",     1);
    auto s_hamming  = mp3.stft("hamming",  2);
    auto s_blackman = mp3.stft("blackman", 3);

    std::cout << "stft hann     : " << s_hann.size()    << " frames, "
              << (s_hann.empty()    ? 0 : s_hann[0].size())    << " bins\n";
    std::cout << "stft hamming  : " << s_hamming.size() << " frames, "
              << (s_hamming.empty() ? 0 : s_hamming[0].size()) << " bins\n";
    std::cout << "stft blackman : " << s_blackman.size()<< " frames, "
              << (s_blackman.empty()? 0 : s_blackman[0].size())<< " bins\n";

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

    auto s_wav = wav.stft("blackman", 0);
    std::cout << "stft blackman (quality 0) : " << s_wav.size() << " frames, "
              << (s_wav.empty() ? 0 : s_wav[0].size()) << " bins\n";

    // ------------------------------------------------------------------ Audio_load only
    std::cout << "\n=== Audio_load only ===\n";
    Audio_load loader("sample.wav", "wav");
    loader.wav_load();
    // loader.fft() — not available, Audio_load has no FFT methods

    // ------------------------------------------------------------------ Energy
    std::cout << "\n=== Energy ===\n";
    Energy nrg("sample.wav", "wav");
    nrg.wav_load();

    auto E = nrg.global("hann", 1);
    std::cout << "global energy frames       : " << E.size() << "\n";
    if (!E.empty())
        std::cout << "frame[0] energy            : " << E[0] << "\n";

    auto E_band = nrg.global("hann", 1, 10, 50);
    std::cout << "global energy (band 10-50) : " << E_band.size() << " frames\n";

    auto E_rms = nrg.global_rms("hann", 1);
    std::cout << "rms energy frames          : " << E_rms.size() << "\n";
    if (!E_rms.empty())
        std::cout << "frame[0] rms energy        : " << E_rms[0] << "\n";

    auto Z = Energy::Z_score(E);
    std::cout << "z-score frames             : " << Z.size() << "\n";
    if (!Z.empty())
        std::cout << "frame[0] z-score           : " << Z[0] << "\n";

    // ------------------------------------------------------------------ Intensity
    std::cout << "\n=== Intensity ===\n";

    // Base intensity: weighted blend of energy, velocity and acceleration
    auto I_base = Intensity::base(E, /*accel=*/0.3f, /*vel=*/0.3f, /*intensity=*/0.4f);
    std::cout << "base frames       : " << I_base.size() << "\n";

    // Velocity modulation — additive mode
    auto I_vel = Intensity::velocity(I_base, /*intensity=*/0.5f, /*velocity=*/0.5f, '+');
    std::cout << "velocity frames   : " << I_vel.size() << "\n";

    // Velocity modulation — multiplicative mode
    auto I_vel_m = Intensity::velocity(I_base, 0.5f, 0.5f, '*');
    std::cout << "velocity (*) frames : " << I_vel_m.size() << "\n";

    // Acceleration modulation — additive mode
    auto I_acc = Intensity::acceleration(I_base, /*intensity=*/0.5f, /*accel=*/0.5f, '+');
    std::cout << "acceleration frames : " << I_acc.size() << "\n";

    // Combined: velocity + acceleration in one pass — multiplicative mode
    auto I_comb = Intensity::combined(I_base, /*intensity=*/0.4f, /*vel=*/0.3f, /*accel=*/0.3f, '*');
    std::cout << "combined frames   : " << I_comb.size() << "\n";

    // Smoothing
    auto I_smooth = Intensity::smooth(I_base, /*smoothing_param=*/0.05f);
    std::cout << "smoothed frames   : " << I_smooth.size() << "\n";

    // Relative and centered — compare intensity to its smoothed version
    auto I_rel = Intensity::relative(I_base, I_smooth);
    auto I_cen = Intensity::centered(I_base, I_smooth);
    std::cout << "relative frames   : " << I_rel.size() << "\n";
    std::cout << "centered frames   : " << I_cen.size() << "\n";

    // Raw discrete derivative
    auto I_der = Intensity::derived(I_base);
    std::cout << "derived frames    : " << I_der.size() << "\n";

    // Logarithmic compression — 'e' = power (10*log10), 'r' = amplitude (20*log10)
    auto I_log_e = Intensity::logarithmic(I_base, 'e');
    auto I_log_r = Intensity::logarithmic(I_base, 'r');
    std::cout << "log (power) frames : " << I_log_e.size() << "\n";
    std::cout << "log (rms) frames   : " << I_log_r.size() << "\n";

    // ------------------------------------------------------------------ SpectralIntensity
    std::cout << "\n=== SpectralIntensity ===\n";

    // Reuse the STFT computed earlier
    auto sf  = SpectralIntensity::spectral_flux(s_hann);
    auto sd  = SpectralIntensity::spectral_diff(s_hann);
    auto lsf = SpectralIntensity::log_spectral_flux(s_hann);
    auto hfc = SpectralIntensity::high_frequency_content(s_hann);

    std::cout << "spectral flux frames       : " << sf.size()  << "\n";
    std::cout << "spectral diff frames       : " << sd.size()  << "\n";
    std::cout << "log spectral flux frames   : " << lsf.size() << "\n";
    std::cout << "high frequency content frames : " << hfc.size() << "\n";

    return 0;
}
