// Listening to music, as our Neon port does (C:\Xblades\rexglue\src\audio.cpp): WASAPI loopback capture of
// whatever the PC is playing (Spotify, YouTube...), a 2048-sample FFT into 20 log-spaced bands (50 Hz - 16 kHz),
// each normalised to its own recent peak (instant attack, 0.12 s release) - Neon's tuning, which the owner liked.
// On top: bass / mid / treble levels and beats (onsets in the low bands), for Trip-A-Tron's music modes.
#pragma once

#include <string>

namespace music {

constexpr int kBands = 20;

struct Frame {
  float bands[kBands] = {};      // 0..1, low to high
  float bass = 0, mid = 0, treble = 0;  // band averages, 0..1
  bool beat = false;             // an onset this frame
  float beat_strength = 0;       // 0..1
  bool silent = true;            // nothing playing (below -80 dBFS)
  double silent_for = 0;         // seconds of silence so far
};

// Starts listening to the PC's output (once; later calls do nothing). False if loopback isn't available.
bool StartLoopback();
// Tests: a music file fed in at the game's pace (Analyse's dt), decoded up front, not played - deterministic.
bool StartTestFile(const std::string& path);
bool Active();

// Once per game frame, dt = seconds since the last call.
Frame Analyse(double dt);

}  // namespace music
