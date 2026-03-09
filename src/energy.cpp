#include "energy.hpp"
#include <cmath>
#include <complex>

Energy::Energy(const std::string& file_path, const std::string& type)
    : FFT(file_path, type) {}

// Clamps bin bounds to valid range — replaces -1 with full spectrum bounds
static void energy__clamp_bands(int num_bins, int& band1, int& band2) {
    if (band1 < 0 || band1 >= num_bins) band1 = 0;
    if (band2 < 0 || band2 >= num_bins) band2 = num_bins - 1;
    if (band1 > band2) std::swap(band1, band2);
}

std::vector<float> Energy::global(const std::string& window, int quality,
                                  int band1, int band2)
{
    auto X = stft(window, quality);
    if (X.empty()) return {};

    int num_bins = (int)X[0].size();
    energy__clamp_bands(num_bins, band1, band2);

    std::vector<float> E(X.size());
    for (int n = 0; n < (int)X.size(); n++) {
        float sum = 0.0f;
        for (int k = band1; k <= band2; k++)
            sum += std::norm(X[n][k]); // std::norm(z) = |z|^2
        E[n] = sum;
    }
    return E;
}

std::vector<float> Energy::global_rms(const std::string& window, int quality,
                                      int band1, int band2)
{
    auto X = stft(window, quality);
    if (X.empty()) return {};

    int num_bins = (int)X[0].size();
    energy__clamp_bands(num_bins, band1, band2);

    int M = band2 - band1 + 1; // number of bins in the range

    std::vector<float> E(X.size());
    for (int n = 0; n < (int)X.size(); n++) {
        float sum = 0.0f;
        for (int k = band1; k <= band2; k++)
            sum += std::norm(X[n][k]);
        E[n] = sqrtf(sum / (float)M);
    }
    return E;
}

std::vector<float> Energy::Z_score(const std::vector<float>& energy)
{
    if (energy.empty()) return {};

    // Compute mean
    float mean = 0.0f;
    for (float e : energy) mean += e;
    mean /= (float)energy.size();

    // Compute standard deviation
    float variance = 0.0f;
    for (float e : energy) variance += (e - mean) * (e - mean);
    float sigma = sqrtf(variance / (float)energy.size());

    // Avoid division by zero if all values are identical
    if (sigma == 0.0f) return std::vector<float>(energy.size(), 0.0f);

    std::vector<float> Z(energy.size());
    for (int n = 0; n < (int)energy.size(); n++)
        Z[n] = (energy[n] - mean) / sigma;
    return Z;
}
