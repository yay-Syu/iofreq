#pragma once
#include "fft.hpp"
#include <vector>

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
