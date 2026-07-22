// engine/audio/al_audio.cpp
//
// OpenAL audio backend implementation.
//
// Uses OpenAL-soft for desktop audio output. Provides a source pool for
// sound effects (8 sources) and a dedicated source for music playback.
//
// When RESF2_ENABLE_AUDIO is OFF (headless), AlAudioBackend is not compiled
// and the engine falls back to NullAudioBackend.

#include "audio.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <mutex>

// OpenAL headers
#include <AL/al.h>
#include <AL/alc.h>

namespace resf2::audio {

// ---------- AlAudioBackend implementation ----------

struct AlAudioBackend::Impl {
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;

    // Source pool
    static constexpr int kNumSfxSources = 12;
    static constexpr int kNumMusicSources = 1;
    ALuint sfx_sources[kNumSfxSources]{};
    ALuint music_source = 0;

    // Per-source state
    struct SfxState {
        ALuint buffer = 0;
        bool in_use = false;
        uint32_t source_index = 0;
    };
    SfxState sfx_states[kNumSfxSources]{};

    // Music state
    ALuint music_buffer = 0;
    bool music_playing = false;

    // Volume
    float music_volume = 1.0f;
    float sfx_volume = 1.0f;

    // Mutex for OpenAL calls (context is not thread-safe)
    std::mutex al_mutex;

    bool init() {
        std::lock_guard<std::mutex> lk(al_mutex);

        // Open default device
        device = alcOpenDevice(nullptr);
        if (!device) {
            std::fprintf(stderr, "[al_audio] Failed to open OpenAL device\n");
            return false;
        }

        // Create context
        context = alcCreateContext(device, nullptr);
        if (!context) {
            std::fprintf(stderr, "[al_audio] Failed to create OpenAL context\n");
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        if (!alcMakeContextCurrent(context)) {
            std::fprintf(stderr, "[al_audio] Failed to make context current\n");
            alcDestroyContext(context);
            context = nullptr;
            alcCloseDevice(device);
            device = nullptr;
            return false;
        }

        // Generate SFX sources
        alGenSources(kNumSfxSources, sfx_sources);
        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::fprintf(stderr, "[al_audio] Failed to generate sources: 0x%x\n", err);
            cleanup();
            return false;
        }

        // Configure SFX sources
        for (int i = 0; i < kNumSfxSources; i++) {
            alSourcef(sfx_sources[i], AL_GAIN, 1.0f);
            alSourcei(sfx_sources[i], AL_LOOPING, AL_FALSE);
            alSourcei(sfx_sources[i], AL_SOURCE_RELATIVE, AL_TRUE);
            sfx_states[i].source_index = (uint32_t)i;
            sfx_states[i].in_use = false;
            sfx_states[i].buffer = 0;
        }

        // Generate music source
        alGenSources(kNumMusicSources, &music_source);
        alSourcef(music_source, AL_GAIN, music_volume);
        alSourcei(music_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(music_source, AL_POSITION, 0.0f, 0.0f, 0.0f);

        std::printf("[al_audio] OpenAL initialized: %d SFX sources\n", kNumSfxSources);
        return true;
    }

    void cleanup() {
        std::lock_guard<std::mutex> lk(al_mutex);

        // Stop all sources
        for (int i = 0; i < kNumSfxSources; i++) {
            if (sfx_sources[i]) {
                alSourceStop(sfx_sources[i]);
                alSourcei(sfx_sources[i], AL_BUFFER, 0);
            }
            if (sfx_states[i].buffer) {
                alDeleteBuffers(1, &sfx_states[i].buffer);
                sfx_states[i].buffer = 0;
            }
            sfx_states[i].in_use = false;
        }

        if (music_source) {
            alSourceStop(music_source);
            alSourcei(music_source, AL_BUFFER, 0);
        }
        if (music_buffer) {
            alDeleteBuffers(1, &music_buffer);
            music_buffer = 0;
        }

        alDeleteSources(kNumSfxSources, sfx_sources);
        std::memset(sfx_sources, 0, sizeof(sfx_sources));

        if (music_source) {
            alDeleteSources(1, &music_source);
            music_source = 0;
        }

        if (context) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
            context = nullptr;
        }
        if (device) {
            alcCloseDevice(device);
            device = nullptr;
        }

        music_playing = false;
    }

    // Upload PCM data to an OpenAL buffer
    ALuint upload_buffer(const PcmData& pcm) {
        ALuint buf = 0;
        alGenBuffers(1, &buf);
        if (alGetError() != AL_NO_ERROR) return 0;

        ALenum format = AL_FORMAT_MONO16;
        if (pcm.channels == 2) {
            format = AL_FORMAT_STEREO16;
        }

        // Convert float32 samples to int16 for OpenAL
        size_t sample_count = pcm.samples.size();
        std::vector<int16_t> int16_samples(sample_count);
        for (size_t i = 0; i < sample_count; i++) {
            float s = pcm.samples[i];
            if (s < -1.0f) s = -1.0f;
            if (s > 1.0f) s = 1.0f;
            int16_samples[i] = (int16_t)(s * 32767.0f);
        }

        ALsizei data_size = (ALsizei)(sample_count * sizeof(int16_t));
        alBufferData(buf, format, int16_samples.data(), data_size, (ALsizei)pcm.sample_rate);

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::fprintf(stderr, "[al_audio] Buffer upload failed: 0x%x\n", err);
            alDeleteBuffers(1, &buf);
            return 0;
        }

        return buf;
    }

    // Find a free SFX source
    int find_free_source() {
        // First pass: check AL_STOPPED sources (finished playing)
        for (int i = 0; i < kNumSfxSources; i++) {
            if (!sfx_states[i].in_use) {
                sfx_states[i].in_use = true;
                return i;
            }
        }

        // Second pass: check if any source has finished playing
        for (int i = 0; i < kNumSfxSources; i++) {
            ALint state;
            alGetSourcei(sfx_sources[i], AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                // Clean up old buffer
                if (sfx_states[i].buffer) {
                    alSourcei(sfx_sources[i], AL_BUFFER, 0);
                    alDeleteBuffers(1, &sfx_states[i].buffer);
                    sfx_states[i].buffer = 0;
                }
                sfx_states[i].in_use = true;
                return i;
            }
        }

        // No free source — return -1, caller should skip
        return -1;
    }

    void play_sound_impl(const PcmData& pcm, float volume, float pan) {
        std::lock_guard<std::mutex> lk(al_mutex);

        int idx = find_free_source();
        if (idx < 0) {
            // No free source — silently drop
            return;
        }

        ALuint buf = upload_buffer(pcm);
        if (!buf) {
            sfx_states[idx].in_use = false;
            return;
        }

        sfx_states[idx].buffer = buf;

        ALuint source = sfx_sources[idx];
        alSourcei(source, AL_BUFFER, (ALint)buf);
        alSourcef(source, AL_GAIN, volume * sfx_volume);

        // Pan using OpenAL position
        ALfloat pos[3] = { pan, 0.0f, 0.0f };
        alSourcefv(source, AL_POSITION, pos);

        alSourcePlay(source);

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::fprintf(stderr, "[al_audio] Source play failed: 0x%x\n", err);
            alSourcei(source, AL_BUFFER, 0);
            alDeleteBuffers(1, &buf);
            sfx_states[idx].buffer = 0;
            sfx_states[idx].in_use = false;
        }
    }

    void play_music_impl(const PcmData& pcm, float volume, bool loop) {
        std::lock_guard<std::mutex> lk(al_mutex);

        // Stop any current music
        alSourceStop(music_source);
        alSourcei(music_source, AL_BUFFER, 0);
        if (music_buffer) {
            alDeleteBuffers(1, &music_buffer);
            music_buffer = 0;
        }

        ALuint buf = upload_buffer(pcm);
        if (!buf) return;

        music_buffer = buf;
        alSourcei(music_source, AL_BUFFER, (ALint)buf);
        alSourcef(music_source, AL_GAIN, volume * music_volume);
        alSourcei(music_source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        alSourcePlay(music_source);
        music_playing = true;

        ALenum err = alGetError();
        if (err != AL_NO_ERROR) {
            std::fprintf(stderr, "[al_audio] Music play failed: 0x%x\n", err);
            alSourcei(music_source, AL_BUFFER, 0);
            alDeleteBuffers(1, &buf);
            music_buffer = 0;
            music_playing = false;
        }
    }

    void stop_music_impl() {
        std::lock_guard<std::mutex> lk(al_mutex);
        if (music_source) {
            alSourceStop(music_source);
            alSourcei(music_source, AL_BUFFER, 0);
        }
        if (music_buffer) {
            alDeleteBuffers(1, &music_buffer);
            music_buffer = 0;
        }
        music_playing = false;
    }

    void set_music_volume_impl(float v) {
        music_volume = std::clamp(v, 0.0f, 1.0f);
        std::lock_guard<std::mutex> lk(al_mutex);
        if (music_source) {
            alSourcef(music_source, AL_GAIN, music_volume);
        }
    }

    void set_sfx_volume_impl(float v) {
        sfx_volume = std::clamp(v, 0.0f, 1.0f);
        std::lock_guard<std::mutex> lk(al_mutex);
        for (int i = 0; i < kNumSfxSources; i++) {
            alSourcef(sfx_sources[i], AL_GAIN, sfx_volume);
        }
    }

    void update_impl(float /*dt*/) {
        std::lock_guard<std::mutex> lk(al_mutex);

        // Recycle finished SFX sources
        for (int i = 0; i < kNumSfxSources; i++) {
            if (!sfx_states[i].in_use) continue;

            ALint state;
            alGetSourcei(sfx_sources[i], AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                // Free buffer
                if (sfx_states[i].buffer) {
                    alSourcei(sfx_sources[i], AL_BUFFER, 0);
                    alDeleteBuffers(1, &sfx_states[i].buffer);
                    sfx_states[i].buffer = 0;
                }
                sfx_states[i].in_use = false;
            }
        }
    }
};

// ---------- AlAudioBackend public API ----------

AlAudioBackend::AlAudioBackend()
    : impl_(std::make_unique<Impl>()) {}

AlAudioBackend::~AlAudioBackend() {
    shutdown();
}

bool AlAudioBackend::init() {
    return impl_->init();
}

void AlAudioBackend::shutdown() {
    impl_->cleanup();
}

void AlAudioBackend::play_sound(const PcmData& pcm, float volume, float pan) {
    impl_->play_sound_impl(pcm, volume, pan);
}

void AlAudioBackend::play_music(const PcmData& pcm, float volume, bool loop) {
    impl_->play_music_impl(pcm, volume, loop);
}

void AlAudioBackend::stop_music() {
    impl_->stop_music_impl();
}

void AlAudioBackend::set_music_volume(float volume) {
    impl_->set_music_volume_impl(volume);
}

void AlAudioBackend::set_sfx_volume(float volume) {
    impl_->set_sfx_volume_impl(volume);
}

void AlAudioBackend::update(float dt) {
    impl_->update_impl(dt);
}

} // namespace resf2::audio
