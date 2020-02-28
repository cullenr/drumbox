#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>

// Little endian magic numbers for the WAVE file header
#define WH_LE_RIFF 0x46464952
#define WH_LE_WAVE 0x45564157
#define WH_LE_FMT  0x20746D66
#define WH_LE_DATA 0x61746164

typedef struct WaveHeader {
    // chunk 1
    uint32_t c1Id;               //big endian
    uint32_t c1Size;
    uint32_t c1Format;           //big endian
    // sub-chunk 1
    uint32_t sc1Id;              //big endian
    uint32_t sc1Size;
    uint16_t sc1Format;
    uint16_t sc1Channels;
    uint32_t sc1SampleRate;
    uint32_t sc1ByteRate;
    uint16_t sc1BlockAlign;
    uint16_t sc1BitsPerSample;
    // sub-chunk 2
    uint32_t sc2Id;              //big endian
    uint32_t sc2Size;
} WaveHeader;

void writeHeader(WaveHeader *h, uint32_t num_samples, uint16_t channels, 
                 uint16_t bitdepth, uint32_t sample_rate) {

    h->c1Id             = WH_LE_RIFF;
    // 36 = the byte size of the remaining header fields after this one (c1Size)
    h->c1Size           = 36 + num_samples; 
    h->c1Format         = WH_LE_WAVE;

    h->sc1Id            = WH_LE_FMT;
    h->sc1Size          = 16; // size of this sub-chunk is 16 for PCM format
    h->sc1Format        = 1;  // enum value for PCM
    h->sc1Channels      = channels;
    h->sc1SampleRate    = sample_rate;
    // over 8 as we we are converting bits to bytes
    h->sc1ByteRate      = (num_samples * channels * bitdepth) / 8; 
    h->sc1BlockAlign    = (bitdepth * channels) / 8;
    h->sc1BitsPerSample = bitdepth;

    h->sc2Id            = WH_LE_DATA;
    h->sc2Size          = num_samples;
}

void generateAudio(uint8_t *samples, uint32_t num_samples, uint32_t sample_rate) {
    double freq = 440;
    for(int i = 0; i < num_samples; i++) {
        double sample = sin(freq * (float)i * 3.14159 / sample_rate);
        samples[i] = ((sample + 1.0) / 2.0) * 256.0; 
    }
}

int main(void) {
    uint32_t sample_rate = 8000; // samples per second
    int duration         = 1;    // seconds of audio
    uint32_t num_samples = duration * sample_rate;
    char filename[]      = "/tmp/test.wav";
    WaveHeader h;
    FILE *file;
    uint8_t *samples = malloc(num_samples * sizeof(uint8_t)); // sizeof(char) == 1 :p

    generateAudio(samples, num_samples, sample_rate);
    writeHeader(&h, num_samples, 1, 8, sample_rate);

    file = fopen(filename, "wb");
    fwrite(&h, sizeof(WaveHeader), 1, file);
    fwrite(samples, sizeof(uint8_t), num_samples, file);

    fclose(file);
    free(samples);

    return EXIT_SUCCESS;
}
