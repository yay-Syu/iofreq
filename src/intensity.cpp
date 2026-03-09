#include "intensity.hpp"
#include <cmath>
#include <algorithm>

// Normalizes a vector by its maximum value — returns unchanged if max is 0
static std::vector<float> intensity__norm_by_max(std::vector<float> v) {
    float mx = *std::max_element(v.begin(), v.end());
    if (mx == 0.f) return v;
    for (float& x : v) x /= mx;
    return v;
}

// Computes first-order discrete difference: D[n] = V[n] - V[n-1], D[0] = 0
static std::vector<float> intensity__diff(const std::vector<float>& v) {
    std::vector<float> d(v.size(), 0.f);
    for (int n = 1; n < (int)v.size(); n++)
        d[n] = v[n] - v[n-1];
    return d;
}

std::vector<float> Intensity::base(const std::vector<float>& energy,
                                   float acceleration_param,
                                   float velocity_param,
                                   float intensity_param)
{
    if (energy.empty()) return {};

    // Normalize weights so alpha + beta + gamma = 1
    float total = intensity_param + velocity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param    / total;
    float beta  = velocity_param     / total;
    float gamma = acceleration_param / total;

    std::vector<float> E_norm = intensity__norm_by_max(energy);
    std::vector<float> V      = intensity__norm_by_max(intensity__diff(E_norm));
    std::vector<float> A      = intensity__norm_by_max(intensity__diff(V));

    std::vector<float> I(energy.size());
    for (int n = 0; n < (int)I.size(); n++)
        I[n] = alpha * E_norm[n] + beta * V[n] + gamma * A[n];

    return intensity__norm_by_max(I);
}

std::vector<float> Intensity::velocity(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       char type)
{
    if (intensity.empty()) return {};

    float total = intensity_param + velocity_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param / total;
    float beta  = velocity_param  / total;

    std::vector<float> V = intensity__norm_by_max(intensity__diff(intensity));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + beta * V[n]); // multiplicative modulation
        else
            out[n] = alpha * intensity[n] + beta * V[n]; // additive blend (default)
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::acceleration(const std::vector<float>& intensity,
                                           float intensity_param,
                                           float acceleration_param,
                                           char type)
{
    if (intensity.empty()) return {};

    float total = intensity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param    / total;
    float gamma = acceleration_param / total;

    // Acceleration = second-order difference of intensity
    std::vector<float> V = intensity__diff(intensity);
    std::vector<float> A = intensity__norm_by_max(intensity__diff(V));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + gamma * A[n]);
        else
            out[n] = alpha * intensity[n] + gamma * A[n];
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::combined(const std::vector<float>& intensity,
                                       float intensity_param,
                                       float velocity_param,
                                       float acceleration_param,
                                       char type)
{
    if (intensity.empty()) return {};

    float total = intensity_param + velocity_param + acceleration_param;
    if (total == 0.f) total = 1.f;
    float alpha = intensity_param    / total;
    float beta  = velocity_param     / total;
    float gamma = acceleration_param / total;

    std::vector<float> V = intensity__norm_by_max(intensity__diff(intensity));
    std::vector<float> A = intensity__norm_by_max(intensity__diff(V));

    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        if (type == '*')
            out[n] = intensity[n] * (1.f + beta * V[n] + gamma * A[n]);
        else
            out[n] = alpha * intensity[n] + beta * V[n] + gamma * A[n];
    }
    return intensity__norm_by_max(out);
}

std::vector<float> Intensity::smooth(const std::vector<float>& intensity,
                                     float smoothing_param)
{
    if (intensity.empty()) return {};

    // Map smoothing_param [0, 1] to window half-size [0, N/2]
    int N    = (int)intensity.size();
    int half = std::max(1, (int)(smoothing_param * (N / 2)));

    std::vector<float> out(N, 0.f);
    for (int n = 0; n < N; n++) {
        int   lo  = std::max(0, n - half);
        int   hi  = std::min(N - 1, n + half);
        float sum = 0.f;
        for (int i = lo; i <= hi; i++) sum += intensity[i];
        out[n] = sum / (float)(hi - lo + 1);
    }
    return out;
}

std::vector<float> Intensity::relative(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity)
{
    if (intensity.size() != smoothed_intensity.size()) return {};
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++)
        out[n] = (smoothed_intensity[n] != 0.f) ? intensity[n] / smoothed_intensity[n] : 0.f;
    return out;
}

std::vector<float> Intensity::centered(const std::vector<float>& intensity,
                                       const std::vector<float>& smoothed_intensity)
{
    if (intensity.size() != smoothed_intensity.size()) return {};
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++)
        out[n] = intensity[n] - smoothed_intensity[n];
    return out;
}

std::vector<float> Intensity::derived(const std::vector<float>& intensity)
{
    // Raw discrete derivative — no normalization or weighting
    return intensity__diff(intensity);
}

std::vector<float> Intensity::logarithmic(const std::vector<float>& intensity, char rms)
{
    float factor = (rms == 'r') ? 20.f : 10.f; // 'r' = amplitude (RMS), else power
    std::vector<float> out(intensity.size());
    for (int n = 0; n < (int)out.size(); n++) {
        float v = (intensity[n] > 1e-10f) ? intensity[n] : 1e-10f; // clamp to avoid log(0)
        out[n] = factor * log10f(v);
    }
    return out;
}
