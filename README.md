# iofreq

[![GitHub](https://img.shields.io/badge/github-yay--Syu/iofreq-blue)](https://github.com/yay-Syu/iofreq)

C++ module for audio loading (MP3, FLAC, WAV) and spectral analysis — FFT, STFT, energy, intensity and onset detection.

---

## Class overview

```
Audio_load              decodes MP3 / FLAC / WAV into float PCM [-1, 1]
    └── FFT             adds fft() and stft()
        └── Energy      adds spectral energy analysis

Intensity               time-domain intensity analysis (standalone)
SpectralIntensity       frequency-domain intensity analysis (standalone)
```

---

## Project structure

```
iofreq/
    include/
        audio_load.hpp
        fft.hpp
        energy.hpp
        intensity.hpp
        spectral_intensity.hpp
    src/
        audio_load.cpp
        fft.cpp
        energy.cpp
        intensity.cpp
        spectral_intensity.cpp
    examples/
        ex_audio_load.cpp
        ex_fft.cpp
        ex_energy.cpp
        ex_intensity.cpp
        ex_spectral_intensity.cpp
```

---

## Dependencies

| Library | Purpose | Install |
|---|---|---|
| [minimp3](https://github.com/lieff/minimp3) | MP3 decoding | header-only — drop `minimp3.h` next to your source |
| [kissfft](https://github.com/mborgerding/kissfft) | FFT engine | `sudo apt install libkissfft-dev` |
| libFLAC | FLAC decoding | `sudo apt install libflac-dev` |

---

## Build

```bash
g++ -c src/audio_load.cpp -Iinclude -o audio_load.o
g++ -c src/fft.cpp        -Iinclude -o fft.o
g++ -c src/energy.cpp     -Iinclude -o energy.o
g++ -c src/intensity.cpp  -Iinclude -o intensity.o
g++ -c src/spectral_intensity.cpp -Iinclude -o spectral_intensity.o

g++ examples/ex_fft.cpp audio_load.o fft.o -Iinclude -o ex_fft -lFLAC -lkissfft
```

---

## STFT window functions

| Window | Character |
|---|---|
| `"hann"` | General purpose, good sidelobe suppression |
| `"hamming"` | Slightly higher main lobe, common in speech processing |
| `"blackman"` | Best sidelobe suppression, wider main lobe |

---

## Energy methods

| Method | Formula | Description |
|---|---|---|
| `global()` | `E[n] = Σ\|X[n][k]\|²` | Sum of squared magnitudes per frame |
| `global_rms()` | `E[n] = sqrt(1/M · Σ\|X[n][k]\|²)` | RMS energy — normalized across window sizes |
| `Z_score()` | `Z[n] = (E[n] - μ) / σ` | Centers and scales any energy vector |

`band1` and `band2` restrict energy to a frequency bin range. Set to `-1` for full spectrum.

---

## Intensity methods

| Method | Description |
|---|---|
| `base()` | Weighted blend of energy, velocity and acceleration — result in [0, 1] |
| `velocity()` | Modulates intensity with its first-order difference |
| `acceleration()` | Modulates intensity with its second-order difference |
| `combined()` | Velocity and acceleration modulation in one pass |
| `smooth()` | Moving average — `smoothing_param` in [0, 1] |
| `relative()` | `I[n] / I_smooth[n]` — ratio to local mean |
| `centered()` | `I[n] - I_smooth[n]` — excess above local mean |
| `derived()` | Raw discrete derivative `I[n] - I[n-1]` |
| `logarithmic()` | `10·log10` (power) or `20·log10` (amplitude) |

`velocity()`, `acceleration()` and `combined()` accept `char type`: `'+'` additive, `'*'` multiplicative.

---

## SpectralIntensity methods

| Method | Formula | Description |
|---|---|---|
| `spectral_flux()` | `Σ max(0, \|X[n][k]\| - \|X[n-1][k]\|)` | Positive magnitude increases — onset detection |
| `spectral_diff()` | `Σ \|\|X[n][k]\| - \|X[n-1][k]\|\|` | Total magnitude change between frames |
| `log_spectral_flux()` | log-magnitude spectral flux | More robust to volume variations |
| `high_frequency_content()` | `Σ k · \|X[n][k]\|²` | Emphasizes high-frequency energy |

All `SpectralIntensity` methods take a pre-computed STFT from `FFT::stft()`.

---

## License

Released under the Unlicense — no restrictions, no attribution required. See [unlicense.org](https://unlicense.org).

---

## Acknowledgements

- [lieff/minimp3](https://github.com/lieff/minimp3) — MP3 decoding
- [mborgerding/kissfft](https://github.com/mborgerding/kissfft) — FFT engine
