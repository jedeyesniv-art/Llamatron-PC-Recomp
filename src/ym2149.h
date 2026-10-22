// Yamaha YM2149 PSG (the Atari ST's sound chip), 2 MHz, rendered to mono float samples.
#pragma once

#include <cstdint>
#include <vector>

class Ym2149 {
 public:
  explicit Ym2149(double clock_hz = 2000000.0);

  void Reset();
  void SetSampleRate(int rate);
  void Write(int reg, uint8_t value);
  uint8_t Read(int reg) const { return regs_[reg & 15]; }

  // Advances the chip by `clocks` master clocks (2 MHz), appending output samples to `out`.
  void Run(double clocks, std::vector<float>* out);

 private:
  void Tick();  // one step of the tone/noise/envelope counters (master clock / 8)
  double Level() const;

  uint8_t regs_[16] = {};
  double step_clocks_ = 8.0;        // master clocks per Tick()
  double clocks_per_sample_ = 1.0;  // master clocks per output sample
  double phase_ = 0.0;              // master clocks into the current sample
  double acc_ = 0.0;                // output accumulated over the current sample
  double step_phase_ = 0.0;         // master clocks into the current Tick()
  double clock_hz_;

  unsigned tone_count_[3] = {}, tone_out_[3] = {};
  unsigned noise_count_ = 0, noise_lfsr_ = 1, noise_out_ = 0, noise_half_ = 0;
  unsigned env_count_ = 0, env_step_ = 0, env_holding_ = 0, env_flip_ = 0, env_hold_level_ = 0;
  double dc_ = 0.0;  // DC blocker state (the digitized samples ride on a large offset)
};
