#pragma once
#include <stdint.h>
#include <stdbool.h>

struct WavHeader {
    /* RIFF Chunk Descriptor */
    uint8_t         riff[4];        // RIFF Header Magic header
    uint32_t        data_size;      // RIFF Chunk Size
    uint8_t         wave[4];        // WAVE Header
    /* "fmt" sub-chunk */
    uint8_t         fmt[4];         // FMT header
    uint32_t        chunk1_size;  // Size of the fmt chunk
    uint16_t        audio_format;    // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM Mu-Law, 258=IBM A-Law, 259=ADPCM
    uint16_t        channels;      // Number of channels 1=Mono 2=Sterio
    uint32_t        samples;  // Sampling Frequency in Hz
    uint32_t        bytes_per_second;    // bytes per second
    uint16_t        block_align;     // 2=16-bit mono, 4=16-bit stereo
    uint16_t        bits_per_sample;  // Number of bits per sample
    /* "data" sub-chunk */
    uint8_t         data[4]; // "data"  string
    uint32_t        chunk2_size;  // Sampled data length

    uint8_t         data_;
};

WavHeader* wav_open(const char* filename, uint8_t** data, uint32_t* data_size);
