#pragma once
#include <string>
#include <cstdint>

// Holds raw decoded audio — samples are interleaved float PCM normalized to [-1, 1]
typedef struct {
    float*   samples;      // interleaved PCM: [L0, R0, L1, R1, ...] for stereo
    uint64_t frame_count;  // number of frames (samples per channel)
    uint32_t sample_rate;  // e.g. 44100, 48000
    uint32_t channels;     // 1 = mono, 2 = stereo
} AudioSamples;

// ----------------------------------------------------------------------------
// Audio_load — decodes audio files into float PCM
// ----------------------------------------------------------------------------
class Audio_load
{
public:
    Audio_load(const std::string& file_path, const std::string& type);

    void mp3_load();
    void flac_load();
    void wav_load();

    // Prints sample rate, channel count, frame count and duration to stdout
    static void print_infos(AudioSamples& audio);

protected:
    std::string  m_file_path;
    std::string  m_type;
    AudioSamples m_audio;
    float*       m_samples = nullptr;
};
