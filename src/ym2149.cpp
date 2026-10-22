#include "ym2149.h"

#include <algorithm>

namespace {

// YM2149 DAC levels for the 32 envelope steps (fixed volumes use 2v+1); from ayumi (Peter Sovietov, MIT).
const double kLevels[32] = {
    0.0,           0.0,           0.00465400167849, 0.00772106507973, 0.0109559777218, 0.0139620050355,
    0.0169985503929, 0.0200198367285, 0.024368657969, 0.029694056611,  0.0350652323186, 0.0403906309606,
    0.0485389486534, 0.0583352407111, 0.0680552376593, 0.0777752346075, 0.0925154497597, 0.111085679408,
    0.129747463188, 0.148485542077,  0.17666895552,   0.211551079576,  0.246387426566,  0.281101701381,
    0.333730067903, 0.400427252613,  0.467383840696,  0.53443198291,   0.635172045472,  0.75800717174,
    0.879926756695, 1.0};

}  // namespace

Ym2149::Ym2149(double clock_hz) : clock_hz_(clock_hz) { Reset(); }

void Ym2149::Reset() {
  std::fill(std::begin(regs_), std::end(regs_), 0);
  regs_[7] = 0xFF;
  noise_lfsr_ = 1;
  env_holding_ = 1;
}

void Ym2149::SetSampleRate(int rate) { clocks_per_sample_ = clock_hz_ / rate; }

void Ym2149::Write(int reg, uint8_t v) {
  static const uint8_t kMask[16] = {0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
                                    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF};
  reg &= 15;
  regs_[reg] = v & kMask[reg];
  if (reg == 13) {  // restarts the envelope
    env_step_ = 0;
    env_count_ = 0;
    env_holding_ = 0;
    env_flip_ = 0;
  }
}

void Ym2149::Tick() {
  for (int c = 0; c < 3; ++c) {
    unsigned period = regs_[c * 2] | (regs_[c * 2 + 1] << 8);
    if (++tone_count_[c] >= std::max(period, 1u)) {
      tone_count_[c] = 0;
      tone_out_[c] ^= 1;
    }
  }
  if ((noise_half_ ^= 1) == 0) {  // the noise generator runs at half the tone rate
    if (++noise_count_ >= std::max<unsigned>(regs_[6], 1u)) {
      noise_count_ = 0;
      unsigned bit = (noise_lfsr_ ^ (noise_lfsr_ >> 3)) & 1;  // 17-bit LFSR
      noise_lfsr_ = (noise_lfsr_ >> 1) | (bit << 16);
      noise_out_ = noise_lfsr_ & 1;
    }
  }
  // Envelope: 32 steps per cycle, a step every `period` ticks (clock / 8 / period).
  unsigned period = regs_[11] | (regs_[12] << 8);
  if (!env_holding_ && ++env_count_ >= std::max(period, 1u)) {
    env_count_ = 0;
    if (++env_step_ == 32) {
      unsigned shape = regs_[13];
      bool attack = (shape & 4) != 0;
      if (!(shape & 8)) {
        env_holding_ = 1;
        env_hold_level_ = 0;  // ends low
      } else if (shape & 1) {
        env_holding_ = 1;
        env_hold_level_ = (attack != ((shape & 2) != 0)) ? 31 : 0;
      } else {
        env_step_ = 0;
        if (shape & 2) env_flip_ ^= 1;  // alternate: flip direction
      }
    }
  }
}

double Ym2149::Level() const {
  unsigned shape = regs_[13];
  unsigned env;
  if (env_holding_) {
    env = env_hold_level_;
  } else {
    bool attack = ((shape & 4) != 0) != (env_flip_ != 0);
    env = attack ? env_step_ : 31 - env_step_;
  }
  double sum = 0;
  for (int c = 0; c < 3; ++c) {
    unsigned tone_off = (regs_[7] >> c) & 1, noise_off = (regs_[7] >> (c + 3)) & 1;
    if (!((tone_out_[c] | tone_off) & (noise_out_ | noise_off))) continue;
    uint8_t vol = regs_[8 + c];
    sum += kLevels[(vol & 0x10) ? env : (vol & 15) * 2 + 1];
  }
  return sum / 3.0;
}

void Ym2149::Run(double clocks, std::vector<float>* out) {
  while (clocks > 1e-9) {
    double d = std::min({clocks, step_clocks_ - step_phase_, clocks_per_sample_ - phase_});
    acc_ += Level() * d;
    phase_ += d;
    step_phase_ += d;
    clocks -= d;
    if (step_phase_ >= step_clocks_ - 1e-9) {
      step_phase_ = 0;
      Tick();
    }
    if (phase_ >= clocks_per_sample_ - 1e-9) {
      double x = acc_ / clocks_per_sample_;
      dc_ += (x - dc_) * 0.0005;
      out->push_back(float(x - dc_));
      acc_ = 0;
      phase_ = 0;
    }
  }
}
