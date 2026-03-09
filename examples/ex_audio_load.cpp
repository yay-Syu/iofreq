#include "audio_load.hpp"
#include <iostream>

int main()
{
    // MP3
    Audio_load mp3("sample.mp3", "mp3");
    mp3.mp3_load();

    // FLAC
    Audio_load flac("sample.flac", "flac");
    flac.flac_load();

    // WAV
    Audio_load wav("sample.wav", "wav");
    wav.wav_load();

    return 0;
}
