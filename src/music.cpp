#include "music.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

#include "log.h"
#include "miniaudio.h"

namespace music {
namespace {

constexpr uint32_t kRingSize = 1 << 16;  // mono samples, a power of two
constexpr uint32_t kFftSize = 2048;
constexpr double kLowestHz = 50.0, kHighestHz = 16000.0;

// Mono samples: the capture thread writes, the game thread reads the newest kFftSize.
float g_ring[kRingSize];
std::atomic<uint64_t> g_written{0};
std::atomic<uint32_t> g_rate{48000};
bool g_started = false;

ma_device g_device;
bool g_device_ok = false;

// Tests: a whole decoded file, fed in at the game's pace.
std::vector<float> g_test;
double g_test_pos = 0;
bool g_testing = false;

void Push(const float* frames, uint32_t count, uint32_t channels) {
  const uint64_t written = g_written.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < count; ++i) {
    float sum = 0;
    for (uint32_t c = 0; c < channels; ++c) sum += frames[i * channels + c];
    g_ring[(written + i) & (kRingSize - 1)] = sum / float(channels);
  }
  g_written.store(written + count, std::memory_order_release);
}

void CaptureCallback(ma_device*, void*, const void* input, ma_uint32 frames) {
  if (input) Push(static_cast<const float*>(input), frames, 2);
}

void Fft(std::vector<std::complex<float>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const float angle = -2.0f * 3.14159265358979f / float(len);
    const std::complex<float> step(std::cos(angle), std::sin(angle));
    for (size_t i = 0; i < n; i += len) {
      std::complex<float> w(1, 0);
      for (size_t k = 0; k < len / 2; ++k) {
        const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= step;
      }
    }
  }
}

// Neon's analyser (same constants), plus onsets.
struct Analyser {
  std::vector<float> window = std::vector<float>(kFftSize);
  std::vector<std::complex<float>> spectrum = std::vector<std::complex<float>>(kFftSize);
  uint32_t band_rate = 0;
  int band_lo[kBands] = {}, band_hi[kBands] = {};
  float peak_db[kBands], level[kBands] = {}, prev[kBands] = {};
  // Onsets: the rise of the low bands, against its own recent average and spread.
  float flux_hist[64] = {};
  int flux_n = 0;
  double since_beat = 10, silent_for = 0;

  Analyser() {
    for (uint32_t i = 0; i < kFftSize; ++i)
      window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979f * float(i) / float(kFftSize - 1));
    std::fill(std::begin(peak_db), std::end(peak_db), -60.0f);
  }

  void SetBands(uint32_t rate) {
    band_rate = rate;
    const double hi_hz = std::min(kHighestHz, rate * 0.45);
    for (int b = 0; b < kBands; ++b) {
      const double f0 = kLowestHz * std::pow(hi_hz / kLowestHz, double(b) / kBands);
      const double f1 = kLowestHz * std::pow(hi_hz / kLowestHz, double(b + 1) / kBands);
      band_lo[b] = std::max(1, int(f0 * kFftSize / rate));
      band_hi[b] = std::max(band_lo[b] + 1, int(f1 * kFftSize / rate));
    }
  }

  Frame Run(double dt) {
    Frame f;
    dt = std::clamp(dt, 0.0, 0.1);
    if (uint32_t rate = g_rate.load(); rate != band_rate) SetBands(rate);
    const uint64_t written = g_written.load(std::memory_order_acquire);
    double sum_squares = 0;
    for (uint32_t i = 0; i < kFftSize; ++i) {
      const float s = written >= kFftSize ? g_ring[(written + i - kFftSize) & (kRingSize - 1)] : 0.0f;
      sum_squares += double(s) * s;
      spectrum[i] = std::complex<float>(s * window[i], 0);
    }
    f.silent = std::sqrt(sum_squares / kFftSize) < 1e-4;
    Fft(spectrum);
    constexpr float kRangeDb = 30, kPeakFallDbPerSecond = 6, kMaxBelowLoudestDb = 24;
    float db[kBands], loudest = -200;
    for (int b = 0; b < kBands; ++b) {
      double power = 0;
      for (int k = band_lo[b]; k < band_hi[b]; ++k) power += std::norm(spectrum[size_t(k)]);
      db[b] = float(10.0 * std::log10(power / (band_hi[b] - band_lo[b]) + 1e-12));
      peak_db[b] = std::max(db[b], peak_db[b] - float(kPeakFallDbPerSecond * dt));
      loudest = std::max(loudest, peak_db[b]);
    }
    const float release = float(std::exp(-dt / 0.12));
    for (int b = 0; b < kBands; ++b) {
      const float reference = std::max(peak_db[b], loudest - kMaxBelowLoudestDb);
      const float target = f.silent ? 0.0f : std::clamp((db[b] - (reference - kRangeDb)) / kRangeDb, 0.0f, 1.0f);
      prev[b] = level[b];
      level[b] = target >= level[b] ? target : target + (level[b] - target) * release;
      f.bands[b] = level[b];
    }
    auto avg = [&](int a, int b) {
      float s = 0;
      for (int i = a; i < b; ++i) s += level[i];
      return s / float(b - a);
    };
    f.bass = avg(0, 5), f.mid = avg(5, 13), f.treble = avg(13, kBands);

    // Onsets: how much the low bands rose this frame (kick drums, bass notes), weighted to the lowest.
    float flux = 0;
    for (int b = 0; b < 7; ++b) flux += std::max(0.0f, level[b] - prev[b]) * (b < 4 ? 1.0f : 0.5f);
    float mean = 0, var = 0;
    int n = std::min(flux_n, 64);
    for (int i = 0; i < n; ++i) mean += flux_hist[i];
    mean = n ? mean / float(n) : 0;
    for (int i = 0; i < n; ++i) var += (flux_hist[i] - mean) * (flux_hist[i] - mean);
    float sd = n ? std::sqrt(var / float(n)) : 0;
    flux_hist[flux_n++ % 64] = flux;
    since_beat += dt;
    float threshold = std::max(0.35f, mean + 1.6f * sd);
    if (!f.silent && flux > threshold && since_beat > 0.25) {
      f.beat = true;
      f.beat_strength = std::clamp(flux / (threshold * 2.0f), 0.0f, 1.0f);
      since_beat = 0;
    }
    silent_for = f.silent ? silent_for + dt : 0;
    f.silent_for = silent_for;
    return f;
  }
} g_analyser;

}  // namespace

bool StartLoopback() {
  if (g_started) return g_device_ok;
  g_started = true;
  ma_device_config config = ma_device_config_init(ma_device_type_loopback);
  config.capture.format = ma_format_f32;
  config.capture.channels = 2;
  config.sampleRate = 0;  // the output device's own rate
  config.dataCallback = CaptureCallback;
  const ma_backend backends[] = {ma_backend_wasapi};  // loopback is WASAPI-only
  if (ma_device_init_ex(backends, 1, nullptr, &config, &g_device) != MA_SUCCESS) {
    LOG("music: loopback capture unavailable");
    return false;
  }
  g_rate = g_device.sampleRate;
  if (ma_device_start(&g_device) != MA_SUCCESS) {
    LOG("music: loopback capture failed to start");
    ma_device_uninit(&g_device);
    return false;
  }
  g_device_ok = true;
  LOG("music: listening to the PC's output (%s, %u Hz)", g_device.capture.name, g_device.sampleRate);
  return true;
}

bool StartTestFile(const std::string& path) {
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
  ma_uint64 frames = 0;
  void* data = nullptr;
  if (ma_decode_file(path.c_str(), &config, &frames, &data) != MA_SUCCESS) {
    LOG("music: can't decode %s", path.c_str());
    return false;
  }
  g_test.assign(static_cast<float*>(data), static_cast<float*>(data) + frames);
  ma_free(data, nullptr);
  g_rate = 48000;
  g_testing = g_started = g_device_ok = true;
  LOG("music: test file %s (%.1f s)", path.c_str(), double(frames) / 48000);
  return true;
}

bool Active() { return g_device_ok; }

Frame Analyse(double dt) {
  if (g_testing) {  // feed the next dt seconds of the file
    size_t from = size_t(g_test_pos), to = std::min(g_test.size(), size_t(g_test_pos + dt * 48000));
    if (to > from) Push(g_test.data() + from, uint32_t(to - from), 1);
    else {
      static const float zeros[960] = {};
      Push(zeros, uint32_t(std::min(960.0, dt * 48000)), 1);
    }
    g_test_pos += dt * 48000;
  }
  return g_analyser.Run(dt);
}

}  // namespace music
