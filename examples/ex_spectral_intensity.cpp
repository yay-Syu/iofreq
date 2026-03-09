#include "fft.hpp"
#include "spectral_intensity.hpp"
#include <iostream>

int main()
{
    FFT fft("sample.wav", "wav");
    fft.wav_load();

    // Pre-compute STFT — all SpectralIntensity methods take it as input
    auto S = fft.stft("hann", 1);

    // Spectral flux — sum of positive magnitude increases between frames
    // Spikes at onsets when many bins increase simultaneously
    auto sf = SpectralIntensity::spectral_flux(S);
    std::cout << "spectral flux frames          : " << sf.size() << "\n";

    // Spectral difference — total magnitude change between frames (increases and decreases)
    auto sd = SpectralIntensity::spectral_diff(S);
    std::cout << "spectral diff frames          : " << sd.size() << "\n";

    // Log spectral flux — spectral flux on log-magnitude spectrum
    // More robust to volume variations than linear spectral flux
    auto lsf = SpectralIntensity::log_spectral_flux(S);
    std::cout << "log spectral flux frames      : " << lsf.size() << "\n";

    // High Frequency Content — HFC[n] = sum_k( k * |X[n][k]|^2 )
    // Emphasizes high-frequency energy — useful for detecting sharp transients
    auto hfc = SpectralIntensity::high_frequency_content(S);
    std::cout << "high frequency content frames : " << hfc.size() << "\n";

    return 0;
}
