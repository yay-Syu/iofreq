# audio_load

A lightweight C++ audio loader with FFT and STFT support. Decodes MP3, FLAC and WAV files into normalized float PCM via `Audio_load`, then optionally runs frequency-domain analysis via the `FFT` subclass.

---

## Class overview

```
Audio_load      decodes MP3 / FLAC / WAV into float PCM [-1, 1]
    └── FFT     adds fft() and stft() on top of the loaded audio
```

`Audio_load` can be used standalone if only raw samples are needed. `FFT` extends it with frequency analysis.

---

## Features

- MP3, FLAC and WAV decoding to interleaved float PCM normalized to [-1, 1]
- Full-signal FFT on channel 0 via `FFT::fft()`
- Short-Time Fourier Transform via `FFT::stft()` with Hann, Hamming or Blackman window
- 4 STFT quality levels mapping to window sizes 512, 1024, 2048 and 4096

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
g++ -c audio_load.cpp -o audio_load.o
g++ example.cpp audio_load.o -o my_program -lFLAC -lkissfft
```

---

## STFT window functions

| Window | Character |
|---|---|
| `"hann"` | General purpose, good sidelobe suppression |
| `"hamming"` | Slightly higher main lobe, common in speech processing |
| `"blackman"` | Best sidelobe suppression, wider main lobe |

---

## License

Released under the Unlicense — no restrictions, no attribution required. See [unlicense.org](https://unlicense.org).

---

## Acknowledgements

- [lieff/minimp3](https://github.com/lieff/minimp3) — MP3 decoding
- [mborgerding/kissfft](https://github.com/mborgerding/kissfft) — FFT engine
