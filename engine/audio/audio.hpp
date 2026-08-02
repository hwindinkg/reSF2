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
//   - MusicTrack: MP3 playback via minimp3 (single-header, MIT)
//
// Output: the AudioEngine delegates to a backend (OpenAL on desktop,
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

// Load an MP3 file from disk (uses minimp3 decoder).
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

// Audio backend interface (OpenAL, null).
class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    // Initialize the backend (open device, create context/sources).
    virtual bool init() = 0;

    // Shut down the backend (close device, free resources).
    virtual void shutdown() = 0;

    // Play a sound effect from PCM data.
    virtual void play_sound(const PcmData& pcm, float volume, float pan) = 0;

    // Play music from PCM data.
    virtual void play_music(const PcmData& pcm, float volume, bool loop) = 0;

    // Stop the currently playing music.
    virtual void stop_music() = 0;

    // Set volume (0..1).
    virtual void set_music_volume(float volume) = 0;
    virtual void set_sfx_volume(float volume) = 0;

    // Per-frame update (recycle finished sources, manage streams).
    virtual void update(float dt) = 0;
};

// Null audio backend (headless / no audio device).
// All operations are no-ops.
class NullAudioBackend : public AudioBackend {
public:
    bool init() override { return true; }
    void shutdown() override {}
    void play_sound(const PcmData&, float, float) override {}
    void play_music(const PcmData&, float, bool) override {}
    void stop_music() override {}
    void set_music_volume(float) override {}
    void set_sfx_volume(float) override {}
    void update(float) override {}
};

// OpenAL audio backend (desktop). Only available when RESF2_ENABLE_AUDIO=ON.
// Falls back to NullAudioBackend if OpenAL is not available.
class AlAudioBackend : public AudioBackend {
public:
    AlAudioBackend();
    ~AlAudioBackend() override;

    bool init() override;
    void shutdown() override;
    void play_sound(const PcmData& pcm, float volume, float pan) override;
    void play_music(const PcmData& pcm, float volume, bool loop) override;
    void stop_music() override;
    void set_music_volume(float volume) override;
    void set_sfx_volume(float volume) override;
    void update(float dt) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
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

    // Load an MP3 music file from disk.
    std::shared_ptr<WavSound> load_music_file(const std::string& name, const std::string& path);

    // Play a sound. Returns the instance id (0 on failure).
    uint32_t play(const std::string& name, float volume = 1.0f, bool looping = false);
    void stop(uint32_t instance_id);
    void stop_all();

    // Play loaded music (by name). Only one music track at a time.
    void play_music(const std::string& name, float volume = 1.0f, bool loop = true);
    void stop_music();
    void set_music_volume(float v);
    void set_sfx_volume(float v);

    // Volume getters.
    float music_volume() const { return music_volume_; }
    float sfx_volume() const { return sfx_volume_; }

    // Advance the audio system by dt seconds.
    void update(float dt);

    // Access the backend directly.
    AudioBackend* backend() const { return backend_.get(); }

    // Name of the last sound successfully played (empty if none yet).
    // Observability seam for behavioral tests: after a gameplay action the
    // caller can assert WHICH sound the engine actually played.
    const std::string& last_played_name() const { return last_played_name_; }

    // Total number of successful play() calls. With last_played_name() this
    // lets a test distinguish a replay of the same name from no play at all.
    uint64_t play_count() const { return play_count_; }

private:
    std::unordered_map<std::string, std::shared_ptr<WavSound>> sounds_;
    std::vector<SoundInstance> instances_;
    std::unique_ptr<AudioBackend> backend_;
    uint32_t next_instance_id_ = 1;
    float music_volume_ = 1.0f;
    float sfx_volume_ = 1.0f;
    std::mutex mutex_;
    bool initialized_ = false;
    std::string last_played_name_;
    uint64_t play_count_ = 0;
};

} // namespace resf2::audio
