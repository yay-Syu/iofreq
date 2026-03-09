#include "energy.hpp"
#include <iostream>

int main()
{
    Energy nrg("sample.wav", "wav");
    nrg.wav_load();

    // Global energy per frame — sum of |X[n][k]|^2 over all bins
    auto E = nrg.global("hann", 1);
    std::cout << "global energy frames       : " << E.size() << "\n";
    if (!E.empty()) std::cout << "frame[0]                   : " << E[0] << "\n";

    // Restricted to a frequency band (bins 10 to 50)
    auto E_band = nrg.global("hann", 1, 10, 50);
    std::cout << "global energy (band 10-50) : " << E_band.size() << " frames\n";

    // RMS energy per frame — sqrt(1/M * sum(|X[n][k]|^2))
    auto E_rms = nrg.global_rms("hann", 1);
    std::cout << "rms energy frames          : " << E_rms.size() << "\n";
    if (!E_rms.empty()) std::cout << "frame[0] rms               : " << E_rms[0] << "\n";

    // Z-score — centers and scales the energy vector
    auto Z = Energy::Z_score(E);
    std::cout << "z-score frames             : " << Z.size() << "\n";
    if (!Z.empty()) std::cout << "frame[0] z-score           : " << Z[0] << "\n";

    return 0;
}
