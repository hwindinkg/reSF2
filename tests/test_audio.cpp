// tests/test_audio.cpp
//
// Unit tests for the audio system:
//   - WAV loading from synthetic data
//   - MP3 detection / decode attempt
//   - AudioEngine init/shutdown with NullAudioBackend
//   - Sound loading and playback (null backend)
//   - Volume control
//
// The OpenAL backend tests require runtime hardware and are excluded from
// automated unit tests (they run in functional tests only).

#include "../engine/audio/audio.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace resf2::audio;

static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",             \
                         __FILE__, __LINE__, #cond);                    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_tests;                                                      \
        if (!((a) == (b))) {                                            \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",      \
                         __FILE__, __LINE__, #a, #b);                   \
        }                                                               \
    } while (0)

// ---------- WAV tests ----------

// Create a synthetic 16-bit mono WAV with a simple sine wave
static std::vector<uint8_t> create_sine_wav(uint16_t channels = 1,
                                             uint32_t sample_rate = 22050,
                                             uint16_t bits = 16,
                                             float duration_sec = 0.1f) {
    uint32_t num_samples = (uint32_t)(sample_rate * duration_sec);
    uint32_t bytes_per_sample = bits / 8;
    uint32_t block_align = channels * bytes_per_sample;
    uint32_t byte_rate = sample_rate * block_align;
    uint32_t data_size = num_samples * block_align;
    uint32_t file_size = 36 + data_size; // RIFF header (12) + fmt (24) + data (8) + data

    std::vector<uint8_t> wav(file_size + 8);

    // RIFF header
    std::memcpy(wav.data(), "RIFF", 4);
    wav[4] = (uint8_t)(file_size & 0xFF);
    wav[5] = (uint8_t)((file_size >> 8) & 0xFF);
    wav[6] = (uint8_t)((file_size >> 16) & 0xFF);
    wav[7] = (uint8_t)((file_size >> 24) & 0xFF);
    std::memcpy(wav.data() + 8, "WAVE", 4);

    // fmt chunk
    std::memcpy(wav.data() + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    wav[16] = (uint8_t)(fmt_size & 0xFF);
    wav[17] = (uint8_t)((fmt_size >> 8) & 0xFF);
    wav[18] = (uint8_t)((fmt_size >> 16) & 0xFF);
    wav[19] = (uint8_t)((fmt_size >> 24) & 0xFF);
    // Audio format = 1 (PCM)
    wav[20] = 1; wav[21] = 0;
    // Channels
    wav[22] = (uint8_t)channels; wav[23] = 0;
    // Sample rate
    wav[24] = (uint8_t)(sample_rate & 0xFF);
    wav[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    wav[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    wav[27] = (uint8_t)((sample_rate >> 24) & 0xFF);
    // Byte rate
    wav[28] = (uint8_t)(byte_rate & 0xFF);
    wav[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    wav[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    wav[31] = (uint8_t)((byte_rate >> 24) & 0xFF);
    // Block align
    wav[32] = (uint8_t)(block_align & 0xFF);
    wav[33] = (uint8_t)((block_align >> 8) & 0xFF);
    // Bits per sample
    wav[34] = (uint8_t)bits; wav[35] = 0;

    // data chunk
    std::memcpy(wav.data() + 36, "data", 4);
    wav[40] = (uint8_t)(data_size & 0xFF);
    wav[41] = (uint8_t)((data_size >> 8) & 0xFF);
    wav[42] = (uint8_t)((data_size >> 16) & 0xFF);
    wav[43] = (uint8_t)((data_size >> 24) & 0xFF);

    // Fill with a sine wave
    if (bits == 16) {
        auto* samples = reinterpret_cast<int16_t*>(wav.data() + 44);
        for (uint32_t i = 0; i < num_samples; i++) {
            for (uint16_t ch = 0; ch < channels; ch++) {
                float val = std::sin(2.0f * 3.14159f * 440.0f * (float)i / (float)sample_rate);
                samples[i * channels + ch] = (int16_t)(val * 16000.0f);
            }
        }
    } else {
        // 8-bit
        for (uint32_t i = 0; i < num_samples * channels; i++) {
            float val = std::sin(2.0f * 3.14159f * 440.0f * (float)i / (float)(sample_rate * channels));
            wav[44 + i] = (uint8_t)((val * 127.0f) + 128);
        }
    }

    return wav;
}

static void test_wav_synthetic_16bit_mono() {
    auto wav = create_sine_wav(1, 22050, 16, 0.1f);
    PcmData pcm = load_wav(wav.data(), wav.size());
    CHECK(pcm.valid());
    if (!pcm.valid()) return;
    CHECK_EQ(pcm.sample_rate, 22050u);
    CHECK_EQ(pcm.channels, 1u);
    CHECK_EQ(pcm.frame_count, 2205u); // 0.1s at 22050 Hz
    CHECK(!pcm.samples.empty());
    // Check that samples are in float range [-1, 1]
    for (auto s : pcm.samples) {
        CHECK(s >= -1.0f && s <= 1.0f);
    }
}

static void test_wav_synthetic_16bit_stereo() {
    auto wav = create_sine_wav(2, 44100, 16, 0.05f);
    PcmData pcm = load_wav(wav.data(), wav.size());
    CHECK(pcm.valid());
    if (!pcm.valid()) return;
    CHECK_EQ(pcm.sample_rate, 44100u);
    CHECK_EQ(pcm.channels, 2u);
    CHECK_EQ(pcm.frame_count, 2205u); // 0.05s at 44100 Hz
}

static void test_wav_synthetic_8bit_mono() {
    auto wav = create_sine_wav(1, 8000, 8, 0.1f);
    PcmData pcm = load_wav(wav.data(), wav.size());
    CHECK(pcm.valid());
    if (!pcm.valid()) return;
    CHECK_EQ(pcm.sample_rate, 8000u);
    CHECK_EQ(pcm.channels, 1u);
    CHECK_EQ(pcm.frame_count, 800u);
}

static void test_wav_invalid_rejected() {
    // Empty
    PcmData pcm = load_wav(nullptr, 0);
    CHECK(!pcm.valid());

    // Too small
    uint8_t tiny[] = {0, 1, 2};
    pcm = load_wav(tiny, 3);
    CHECK(!pcm.valid());

    // Not RIFF
    uint8_t not_riff[] = "NOTRIFF........";
    pcm = load_wav(not_riff, 16);
    CHECK(!pcm.valid());
}

// ---------- MP3 tests ----------

static void test_mp3_real_file() {
    // Try to find a real MP3 file in the assets
    fs::path candidates[] = {
        "assets/assets/music/menu.mp3",
        "assets/music/menu.mp3",
        "assets/music/fight1_samurai_spirit.mp3",
    };
    for (const auto& path : candidates) {
        if (!fs::exists(path)) continue;
        auto pcm = load_mp3_file(path.string());
        CHECK(pcm.valid());
        if (!pcm.valid()) {
            std::printf("  MP3 decode failed for %s\n", path.string().c_str());
            continue;
        }
        CHECK(pcm.sample_rate > 0);
        CHECK(pcm.channels == 1 || pcm.channels == 2);
        CHECK(pcm.frame_count > 0);
        CHECK(!pcm.samples.empty());
        std::printf("  [mp3] %s: %u Hz, %u ch, %u frames (%.2fs)\n",
                    path.string().c_str(),
                    pcm.sample_rate, pcm.channels,
                    pcm.frame_count, pcm.duration_seconds());
        return;
    }
    std::printf("SKIP test_mp3_real_file (no MP3 fixtures found)\n");
}

static void test_mp3_nonexistent_file() {
    auto pcm = load_mp3_file("nonexistent_file.mp3");
    CHECK(!pcm.valid());
}

// ---------- AudioEngine tests ----------

static void test_engine_init_shutdown() {
    auto& eng = AudioEngine::instance();
    // init with null backend (default)
    bool ok = eng.init();
    CHECK(ok);
    // second init should be no-op
    ok = eng.init();
    CHECK(ok);
    eng.shutdown();
    // reinit after shutdown
    ok = eng.init();
    CHECK(ok);
    eng.shutdown();
}

static void test_engine_load_and_play() {
    auto& eng = AudioEngine::instance();
    eng.init();

    auto wav = create_sine_wav(1, 22050, 16, 0.05f);
    auto snd = eng.load_sound("test_beep", wav.data(), wav.size());
    CHECK(snd != nullptr);
    CHECK_EQ(snd->name(), "test_beep");

    // Play the sound (should succeed with null backend)
    uint32_t id = eng.play("test_beep", 0.5f, false);
    CHECK(id > 0);

    // Update should not crash
    eng.update(0.016f);
    eng.update(0.1f);

    // Play non-existent sound
    id = eng.play("nonexistent", 1.0f);
    CHECK_EQ(id, 0u);

    eng.shutdown();
}

static void test_engine_volume_control() {
    auto& eng = AudioEngine::instance();
    eng.init();

    // Default volumes
    CHECK_EQ(eng.music_volume(), 1.0f);
    CHECK_EQ(eng.sfx_volume(), 1.0f);

    // Set music volume
    eng.set_music_volume(0.5f);
    CHECK_EQ(eng.music_volume(), 0.5f);

    // Set SFX volume
    eng.set_sfx_volume(0.75f);
    CHECK_EQ(eng.sfx_volume(), 0.75f);

    // Clamp to [0, 1]
    eng.set_music_volume(1.5f);
    CHECK_EQ(eng.music_volume(), 1.0f);

    eng.set_sfx_volume(-0.5f);
    CHECK_EQ(eng.sfx_volume(), 0.0f);

    eng.shutdown();
}

static void test_sound_load_file() {
    auto& eng = AudioEngine::instance();
    eng.init();

    // Try loading a real WAV sound file
    fs::path candidates[] = {
        "assets/assets/sounds/f_pl_attack1.wav",
        "assets/sounds/f_pl_attack1.wav",
    };
    bool loaded = false;
    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            auto snd = eng.load_sound_file("attack1", path.string());
            if (snd) {
                loaded = true;
                CHECK_EQ(snd->name(), "attack1");
                CHECK(snd->pcm().valid());
                std::printf("  [wav] loaded %s: %u Hz, %u ch, %u frames\n",
                            path.string().c_str(),
                            snd->pcm().sample_rate, snd->pcm().channels,
                            snd->pcm().frame_count);
                break;
            }
        }
    }
    if (!loaded) {
        // Create a temp WAV and test
        auto wav_data = create_sine_wav(1, 22050, 16, 0.1f);
        auto tmp_path = std::filesystem::temp_directory_path() / "resf2_test_sound.wav";
        {
            std::ofstream f(tmp_path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(wav_data.data()), wav_data.size());
        }
        auto snd = eng.load_sound_file("test_temp", tmp_path.string());
        CHECK(snd != nullptr);
        if (snd) {
            CHECK(snd->pcm().valid());
        }
        std::filesystem::remove(tmp_path);
    }

    eng.shutdown();
}

int main() {
    // WAV tests
    test_wav_synthetic_16bit_mono();
    test_wav_synthetic_16bit_stereo();
    test_wav_synthetic_8bit_mono();
    test_wav_invalid_rejected();

    // MP3 tests
    test_mp3_real_file();
    test_mp3_nonexistent_file();

    // AudioEngine tests
    test_engine_init_shutdown();
    test_engine_load_and_play();
    test_engine_volume_control();
    test_sound_load_file();

    std::printf("\n%d tests, %d failures\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
