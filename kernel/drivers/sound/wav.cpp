#include "wav.h"
#include "debug.h"
#include "unifs.h"

// Opens WAV file. Supports only default WAV files with 44 byte header.
WavHeader* wav_open(const char* filename, uint8_t** data, uint32_t* data_size) {
    // Try to open WAV file.
    const UniFSFile* file = unifs_open(filename);
    if (!file) {
        DEBUG_ERROR("%s: %s: unifs_open failed", __func__, filename);
        return nullptr;
    }

    // Check if WAV file size is atleast bigger than its own header. Avoids unnecessary headache with exceptions while reading such files.
    if (file->size <= sizeof(WavHeader)) {
        DEBUG_ERROR("%s: %s: invalid or corrupted wav file", __func__, filename);
        return nullptr;
    }

    WavHeader* wav = (WavHeader*)(uint64_t)file->data;

    // Check if WAVE signature is valid.
    if (wav->wave[0] != 'W' || wav->wave[1] != 'A' || wav->wave[2] != 'V' || wav->wave[3] != 'E') {
        DEBUG_ERROR("%s: %s: invalid wav header", __func__, filename);
        return nullptr;
    }

    // Print debug info containing basic WAV information.
    DEBUG_INFO("%s: %s: wav header format: %d | sample rate: %d | bits per sample: %d | channels: %d | data size: %d", __func__, filename, wav->audio_format, wav->samples, wav->bits_per_sample, wav->channels, wav->data_size);
    if (wav->audio_format == 0 || wav->samples == 0 || wav->channels == 0 || wav->data_size == 0) {
        DEBUG_ERROR("%s: %s: invalid wav data", __func__, filename);
        return nullptr;
    }

    // Check if WAV file is PCM format. Others are not supported at the moment.
    if (wav->audio_format != 1) {
        DEBUG_ERROR("%s: %s: non-pcm format is not supported", __func__, filename);
        return nullptr;
    }

    // Check if WAV file is stereo channel and has 16 bit samples. Mono channel or 20 bit WAV files are not supported at the moment.
    if (wav->channels != 2 || wav->bits_per_sample != 16) {
        DEBUG_ERROR("%s: %s: only 16-bit stereo data is supported", __func__, filename);
        return nullptr;
    }

    // Return sample data and header itself.
    *data = &wav->data_;
    *data_size = wav->data_size;
    return wav;
}
