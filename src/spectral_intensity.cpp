#include "spectral_intensity.hpp"
#include <cmath>

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
            if (diff > 0.f) out[n] += diff; // only count positive increases
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
