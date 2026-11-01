// The one translation unit holding the miniaudio implementation (third_party/miniaudio, 0.11.21, as in our Neon
// port): WASAPI loopback capture of the PC's output (music.cpp) and WAV / MP3 / FLAC decoding for tests.
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
