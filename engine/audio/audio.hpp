// engine/audio/audio.hpp
//
// Audio system for reSF2.
//
// The original Shadow Fight 2 (Marmalade SDK) uses:
//   - WAV files for sound effects (assets/sounds/*.wav)
//   - MP3 files for music (assets/music/*.mp3)
//   - s3eAudioChannel API for playback
//
// This engine provides:
//   - WavSound: loads PCM WAV files (8/16-bit, mono/stereo, any sample rate)
//   - AudioEngine: manages sound channels, plays WavSounds, mixes output
//   - MusicTrack: stub for MP3 playback (requires minmp3 or libmpg123)
//
// [HEURISTIC-TODO] MP3 playback not implemented (requires decoder library).
// WAV playback is sufficient for sound effects. For full audio, integrate
// minimp3 (single-header, MIT) for music.
//
// Output: the AudioEngine writes mixed PCM to a backend (OpenAL on desktop,
// ALSA/AudioTrack on Android, or a null sink for headless tests). The backend
// is abstracted via AudioBackend interface.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace resf2::audio {

// PCM audio sample data (interleaved channels, float32 -1..1)
struct PcmData {
    std::vector<float> samples;   // interleaved L,R,L,R...
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint32_t frame_count = 0;     // samples per channel

    bool valid() const { return !samples.empty() && sample_rate > 0 && channels > 0; }
    float duration_seconds() const {
        return sample_rate > 0 ? float(frame_count) / float(sample_rate) : 0.0f;
    }
};

// Load a WAV file from raw bytes (RIFF/WAVE format).
// Supports: PCM (format 1), 8-bit and 16-bit, mono and stereo.
// Returns empty PcmData on failure.
// [ORIGINAL] WAV format is standard RIFF; the original game's WAVs are
// 16-bit PCM mono/stereo at 22050/44100 Hz (Marmalade s3eAudio default).
PcmData load_wav(const uint8_t* data, size_t size);

// Load a WAV file from disk.
PcmData load_wav_file(const std::string& path);

// [HEURISTIC-TODO] MP3 loading — requires minmp3 or libmpg123.
// Stub returns empty; integrate minmp3 for full music support.
PcmData load_mp3_file(const std::string& path);

// A sound effect (loaded WAV, ready to play).
class WavSound {
public:
    bool load(const std::string& name, const PcmData& pcm);
    const std::string& name() const { return name_; }
    const PcmData& pcm() const { return pcm_; }
private:
    std::string name_;
    PcmData pcm_;
};

// A playing sound instance.
struct SoundInstance {
    uint32_t id = 0;
    const WavSound* sound = nullptr;
    uint32_t cursor = 0;          // current frame
    float volume = 1.0f;
    float pitch = 1.0f;           // 1.0 = normal, 2.0 = octave up
    bool looping = false;
    bool finished = false;
};

// Audio backend interface (OpenAL, ALSA, null).
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    // Open the backend with given output format.
    virtual bool open(uint32_t sample_rate, uint16_t channels) = 0;
    virtual void close() = 0;
    // Write interleaved float32 samples to the output device.
    virtual void write(const float* samples, uint32_t frame_count) = 0;
    virtual bool is_open() const = 0;
};

// Null audio backend (headless / no audio device).
class NullAudioBackend : public AudioBackend {
public:
    bool open(uint32_t, uint16_t) override { open_ = true; return true; }
    void close() override { open_ = false; }
    void write(const float*, uint32_t) override {}
    bool is_open() const override { return open_; }
private:
    bool open_ = false;
};

// Audio engine: manages sounds, plays instances, mixes output.
class AudioEngine {
public:
    static AudioEngine& instance();

    // Initialize with a backend (defaults to NullAudioBackend).
    bool init(std::unique_ptr<AudioBackend> backend = nullptr);
    void shutdown();

    // Load a WAV sound from raw bytes. Returns the sound or nullptr.
    std::shared_ptr<WavSound> load_sound(const std::string& name, const uint8_t* data, size_t size);
    std::shared_ptr<WavSound> load_sound_file(const std::string& name, const std::string& path);
    std::shared_ptr<WavSound> get_sound(const std::string& name) const;

    // Play a sound. Returns the instance id (0 on failure).
    uint32_t play(const std::string& name, float volume = 1.0f, bool looping = false);
    void stop(uint32_t instance_id);
    void stop_all();

    // Set master volume (0..1).
    void set_master_volume(float v) { master_volume_ = v; }
    float master_volume() const { return master_volume_; }

    // Advance the audio system by dt seconds: mix playing sounds, write to backend.
    void update(float dt);

private:
    std::unordered_map<std::string, std::shared_ptr<WavSound>> sounds_;
    std::vector<SoundInstance> instances_;
    std::unique_ptr<AudioBackend> backend_;
    uint32_t next_instance_id_ = 1;
    float master_volume_ = 1.0f;
    uint32_t output_sample_rate_ = 44100;
    uint16_t output_channels_ = 2;
    std::mutex mutex_;
    bool initialized_ = false;

    // Resample a sound's PCM to the output format (simple linear interpolation).
    void resample_mix(const SoundInstance& inst, float* out, uint32_t frames);
};

} // namespace resf2::audio
