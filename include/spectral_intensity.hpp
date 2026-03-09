#pragma once
#include <vector>
#include <complex>

// ----------------------------------------------------------------------------
// SpectralIntensity — frequency-domain intensity analysis (standalone)
// All methods take a pre-computed STFT: vector[num_frames][num_bins]
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

    // Log spectral flux — spectral flux on log-magnitude spectrum
    // LF[n] = sum_k( max(0, log(1 + |X[n][k]|) - log(1 + |X[n-1][k]|)) )
    // More robust to volume variations than linear spectral flux
    static std::vector<float> log_spectral_flux(
        const std::vector<std::vector<std::complex<float>>>& stft);

    // High Frequency Content — weights each bin by its frequency index
    // HFC[n] = sum_k( k * |X[n][k]|^2 )
    // Emphasizes high-frequency energy — useful for detecting sharp transients
    static std::vector<float> high_frequency_content(
        const std::vector<std::vector<std::complex<float>>>& stft);
};
