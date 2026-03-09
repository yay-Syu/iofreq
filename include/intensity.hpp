#pragma once
#include <vector>

// ----------------------------------------------------------------------------
// Intensity — time-domain intensity analysis (standalone)
// All methods operate on pre-computed energy or intensity vectors
// ----------------------------------------------------------------------------
class Intensity
{
public:
    // Computes a weighted combination of normalized energy, velocity and acceleration
    // I[n] = alpha * E_norm[n] + beta * V[n] + gamma * A[n]
    // Weights are auto-normalized so that alpha + beta + gamma = 1
    // Final result is normalized by max(I)
    static std::vector<float> base(const std::vector<float>& energy,
                                   float acceleration_param,
                                   float velocity_param,
                                   float intensity_param);

    // Modulates intensity with its own velocity (first-order difference)
    // type '+' : additive        I'[n] = alpha * I[n] + beta * V[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + beta * V[n])
    static std::vector<float> velocity(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       char type);

    // Modulates intensity with its acceleration (second-order difference)
    // type '+' : additive        I'[n] = alpha * I[n] + gamma * A[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + gamma * A[n])
    static std::vector<float> acceleration(const std::vector<float>& intensity,
                                           float intensity_param,
                                           float acceleration_param,
                                           char type);

    // Weighted combination of intensity, velocity and acceleration in one pass
    // type '+' : additive        I'[n] = alpha * I[n] + beta * V[n] + gamma * A[n]
    // type '*' : multiplicative  I'[n] = I[n] * (1 + beta * V[n] + gamma * A[n])
    static std::vector<float> combined(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       float acceleration_param,
                                       char type);

    // Moving average smoothing — smoothing_param in [0, 1]
    // 0 = no smoothing | 1 = maximum smoothing
    // Do not use on a vector already produced by combined()
    static std::vector<float> smooth(const std::vector<float>& intensity,
                                     float smoothing_param);

    // R[n] = I[n] / I_smooth[n]
    // ~1 = normal energy | >1 = energy peak | <1 = low energy
    static std::vector<float> relative(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity);

    // C[n] = I[n] - I_smooth[n]
    // Excess energy above local mean — positive = peak, negative = dip
    static std::vector<float> centered(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity);

    // D[n] = I[n] - I[n-1]
    // Raw discrete derivative — no weighting or normalization
    static std::vector<float> derived(const std::vector<float>& intensity);

    // Logarithmic compression of intensity
    // rms 'e' : power scale      I_log[n] = 10 * log10(I[n])
    // rms 'r' : amplitude scale  I_log[n] = 20 * log10(I[n])
    static std::vector<float> logarithmic(const std::vector<float>& intensity, char rms);
};
