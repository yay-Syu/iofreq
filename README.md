# iofreq

[![GitHub](https://img.shields.io/badge/github-yay--Syu/iofreq-blue)](https://github.com/yay-Syu/iofreq)

Lightweight C++ module for audio file loading (MP3, FLAC, WAV) and frequency domain analysis via FFT, STFT, spectral energy and intensity.

---

## Class overview

```
Audio_load          decodes MP3 / FLAC / WAV into float PCM [-1, 1]
    └── FFT         adds fft() and stft()
        └── Energy  adds spectral energy analysis

Intensity           time-domain intensity analysis (standalone)
SpectralIntensity   frequency-domain intensity analysis (standalone)
```

---

## Features

- MP3, FLAC and WAV decoding to interleaved float PCM normalized to [-1, 1]
- Full-signal FFT on channel 0 via `FFT::fft()`
- Short-Time Fourier Transform via `FFT::stft()` with Hann, Hamming or Blackman window
- 4 STFT quality levels mapping to window sizes 512, 1024, 2048 and 4096
- Per-frame spectral energy via `Energy::global()` and `Energy::global_rms()`
- Z-score normalization via `Energy::Z_score()`
- Time-domain intensity analysis via `Intensity`
- Frequency-domain intensity analysis via `SpectralIntensity`

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
g++ -c iofreq.cpp -o iofreq.o
g++ example.cpp iofreq.o -o my_program -lFLAC -lkissfft
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

`band1` and `band2` restrict energy computations to a frequency bin range. Set to `-1` to use the full spectrum.

---

## Intensity methods

| Method | Description |
|---|---|
| `base()` | Weighted blend of normalized energy, velocity and acceleration |
| `velocity()` | Modulates intensity with its first-order difference |
| `acceleration()` | Modulates intensity with its second-order difference |
| `combined()` | Velocity and acceleration modulation in one pass |
| `smooth()` | Moving average smoothing — `smoothing_param` in [0, 1] |
| `relative()` | `I[n] / I_smooth[n]` — ratio to local mean (~1 normal, >1 peak, <1 dip) |
| `centered()` | `I[n] - I_smooth[n]` — excess energy above local mean |
| `derived()` | Raw discrete derivative `I[n] - I[n-1]` |
| `logarithmic()` | `10·log10` (power) or `20·log10` (amplitude) compression |

`velocity()`, `acceleration()` and `combined()` accept `char type`: `'+'` = additive, `'*'` = multiplicative.

---

## SpectralIntensity methods

| Method | Formula | Description |
|---|---|---|
| `spectral_flux()` | `Σ max(0, \|X[n][k]\| - \|X[n-1][k]\|)` | Positive magnitude increases — onset detection |
| `spectral_diff()` | `Σ \|\|X[n][k]\| - \|X[n-1][k]\|\|` | Total magnitude change between frames |
| `log_spectral_flux()` | log-magnitude version of spectral flux | More robust to volume variations |
| `high_frequency_content()` | `Σ k · \|X[n][k]\|²` | Emphasizes high-frequency energy |

All `SpectralIntensity` methods take a pre-computed STFT from `FFT::stft()`.

---

## License

Released under the Unlicense — no restrictions, no attribution required. See [unlicense.org](https://unlicense.org).

---

## Acknowledgements

- [lieff/minimp3](https://github.com/lieff/minimp3) — MP3 decoding
- [mborgerding/kissfft](https://github.com/mborgerding/kissfft) — FFT engine
