#include "energy.hpp"
#include "intensity.hpp"
#include <iostream>

int main()
{
    Energy nrg("sample.wav", "wav");
    nrg.wav_load();
    auto E = nrg.global("hann", 1);

    // Base intensity — weighted blend of energy, velocity and acceleration
    // params: acceleration_param, velocity_param, intensity_param (auto-normalized to sum 1)
    auto I_base = Intensity::base(E, 0.3f, 0.3f, 0.4f);
    std::cout << "base frames           : " << I_base.size() << "\n";

    // Velocity modulation
    // '+' = additive    I'[n] = a*I[n] + b*V[n]
    // '*' = multiplicative  I'[n] = I[n] * (1 + b*V[n])
    auto I_vel_add  = Intensity::velocity(I_base, 0.5f, 0.5f, '+');
    auto I_vel_mult = Intensity::velocity(I_base, 0.5f, 0.5f, '*');
    std::cout << "velocity (+) frames   : " << I_vel_add.size()  << "\n";
    std::cout << "velocity (*) frames   : " << I_vel_mult.size() << "\n";

    // Acceleration modulation
    auto I_acc_add  = Intensity::acceleration(I_base, 0.5f, 0.5f, '+');
    auto I_acc_mult = Intensity::acceleration(I_base, 0.5f, 0.5f, '*');
    std::cout << "acceleration (+) frames : " << I_acc_add.size()  << "\n";
    std::cout << "acceleration (*) frames : " << I_acc_mult.size() << "\n";

    // Combined — velocity + acceleration in one pass
    auto I_comb = Intensity::combined(I_base, 0.4f, 0.3f, 0.3f, '*');
    std::cout << "combined frames       : " << I_comb.size() << "\n";

    // Smoothing — smoothing_param in [0, 1]
    auto I_smooth = Intensity::smooth(I_base, 0.05f);
    std::cout << "smoothed frames       : " << I_smooth.size() << "\n";

    // Relative and centered — compare intensity to its smoothed version
    // relative: R[n] = I[n] / I_smooth[n]  (~1 normal, >1 peak, <1 dip)
    // centered: C[n] = I[n] - I_smooth[n]
    auto I_rel = Intensity::relative(I_base, I_smooth);
    auto I_cen = Intensity::centered(I_base, I_smooth);
    std::cout << "relative frames       : " << I_rel.size() << "\n";
    std::cout << "centered frames       : " << I_cen.size() << "\n";

    // Raw discrete derivative: D[n] = I[n] - I[n-1]
    auto I_der = Intensity::derived(I_base);
    std::cout << "derived frames        : " << I_der.size() << "\n";

    // Logarithmic compression
    // 'e' = power scale   10 * log10(I[n])
    // 'r' = amplitude scale  20 * log10(I[n])
    auto I_log_e = Intensity::logarithmic(I_base, 'e');
    auto I_log_r = Intensity::logarithmic(I_base, 'r');
    std::cout << "log (power) frames    : " << I_log_e.size() << "\n";
    std::cout << "log (rms) frames      : " << I_log_r.size() << "\n";

    return 0;
}
