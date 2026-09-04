// SFX engine implementation (miniaudio backend, Phase A3).
//
// miniaudio is a single-file public-domain audio library (core/data/
// third_party/miniaudio.h, v0.11.25). `MINIAUDIO_IMPLEMENTATION` in this
// one TU instantiates it; the bundled dr_wav decoder handles the game's
// plain PCM16 wavs. The device + its audio thread live here, so the game
// thread's play() is a couple of command pushes (thread-safe in miniaudio).
//
// The sfx directory is resolved from the app's res_root (reference/www/res
// does NOT hold the sfx — the APK's assets/sounds does):
//   1. $SF2_SFX_DIR                       (explicit override, if set)
//   2. <res_root>/sounds                  (a future res layout)
//   3. assets/sounds                      (CWD = the repo root)
//   4. ../assets/sounds                   (CWD = build/app/game/Release)
//   5. <res_root>/../../assets/sounds     (res_root reference/www/res)
// The first existing directory wins; if none exist the engine falls back
// to a generated sine beep so the events are still audible end-to-end.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio/audio.hpp"
#include "audio/sfx_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace sf2::audio {

namespace {

// One event's sample pool. The rows come from the JS `ta.WBa()` table
// (see audio/sfx_table.hpp): `files` are wav STEM names under the sfx dir,
// the game's real samples (hit1-6 = punch/weapon impacts, f_pl_/m_pl_* =
// the fighter voice/jump sets, swish* = weapon/body movement, buy = the
// menu button tick, magic_* = the spell sets, ...).
struct EventDef {
    const char* name;                // play("name")
    std::vector<const char*> files;  // candidate wav stems (round-robin)
    float volume = 1.0f;             // the event's loudness (steps/clicks quieter)
    int voices = 1;                  // overlapping copies per event
};

// The table-driven event list (Phase 7.1): built once from sfx_table.hpp so
// the JS mapping stays in exactly one place. Stable after construction.
const std::vector<EventDef>& events() {
    static const std::vector<EventDef> kBuilt = [] {
        std::vector<EventDef> out;
        std::size_t n = 0;
        const SfxGroup* groups = sfx_groups(n);
        for (std::size_t g = 0; g < n; ++g) {
            EventDef e;
            e.name = groups[g].event;
            e.files.assign(groups[g].files, groups[g].files + groups[g].count);
            e.volume = groups[g].volume;
            e.voices = groups[g].voices;
            out.push_back(e);
        }
        return out;
    }();
    return kBuilt;
}

int find_event_index(const std::string& name) {
    const std::vector<EventDef>& evs = events();
    for (std::size_t i = 0; i < evs.size(); ++i) {
        if (name == evs[i].name) return static_cast<int>(i);
    }
    return -1;
}

// First existing candidate directory (see the module comment), else "".
std::string resolve_sfx_dir(const std::string& res_root) {
    const char* env = std::getenv("SF2_SFX_DIR");
    if (env != nullptr && *env != '\0' && std::filesystem::is_directory(env)) {
        return env;
    }
    const char* kCandidates[] = {
        "/sounds",
        "/../../assets/sounds",
    };
    for (const char* c : kCandidates) {
        const std::string p = res_root + c;
        if (std::filesystem::is_directory(p)) return p;
    }
    for (const char* c : {"assets/sounds", "../assets/sounds"}) {
        if (std::filesystem::is_directory(c)) return c;
    }
    return "";
}

}  // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    bool engine_ok = false;
    // Per event: one ma_sound per voice (voice v plays files[v % n]).
    std::vector<std::vector<ma_sound>> sounds;
    std::vector<int> next_voice;      // round-robin cursor per event
    std::vector<unsigned char> first_logged;  // 0/1: log once per event
    std::vector<std::uint64_t> played;        // per-event counters
    // Beep fallback (only when no real sample could be loaded).
    ma_audio_buffer beep{};
    bool beep_ok = false;
    ma_sound beep_sound{};
    bool beep_sound_ok = false;
    // Streaming music (JS `ta.Ut`): one streamed slot, looped.
    ma_sound music{};
    bool music_ok = false;
    std::string music_current;
    std::uint64_t music_plays = 0;
};

AudioEngine::AudioEngine() : impl_(new Impl()) {
    const std::size_t n = events().size();
    impl_->next_voice.assign(n, 0);
    impl_->first_logged.assign(n, 0);
    impl_->played.assign(n, 0);
}

AudioEngine::~AudioEngine() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

AudioEngine& AudioEngine::instance() {
    static AudioEngine s_engine;
    return s_engine;
}

bool AudioEngine::init(const std::string& res_root) {
    shutdown();
    if (impl_ == nullptr) return false;

    // The device + its audio thread (async — the game loop never blocks).
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
        std::fprintf(stderr,
                     "[audio] miniaudio engine init FAILED (no audio device?) — sfx "
                     "counted but silent\n");
        return false;
    }
    impl_->engine_ok = true;

    const std::string sfx_dir = resolve_sfx_dir(res_root);
    std::size_t loaded = 0;
    std::size_t total = 0;
    impl_->sounds.assign(events().size(), {});
    for (std::size_t e = 0; e < events().size(); ++e) {
        const EventDef& ev = events()[e];
        impl_->sounds[e].resize(static_cast<std::size_t>(ev.voices));
        for (int v = 0; v < ev.voices; ++v) {
            ++total;
            ma_sound& sound = impl_->sounds[e][static_cast<std::size_t>(v)];
            const std::string path =
                sfx_dir + "/" + ev.files[static_cast<std::size_t>(v) % ev.files.size()] +
                ".wav";
            const ma_result r = ma_sound_init_from_file(
                &impl_->engine, path.c_str(),
                MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_PITCH |
                    MA_SOUND_FLAG_NO_SPATIALIZATION,
                NULL, NULL, &sound);
            if (r == MA_SUCCESS) {
                ma_sound_set_volume(&sound, ev.volume);
                ++loaded;
            } else {
                std::fprintf(stderr, "[audio] load failed: %s (%d)\n", path.c_str(),
                             static_cast<int>(r));
            }
        }
    }

    if (loaded == 0) {
        // No real samples (or all failed): generate an 880 Hz sine beep
        // (150 ms) and point every event at it — better than silence.
        constexpr ma_uint32 kBeepRate = 22050;
        const ma_uint32 frames = kBeepRate * 3 / 20;
        std::vector<float> pcm(static_cast<std::size_t>(frames));
        for (ma_uint32 i = 0; i < frames; ++i) {
            const float env = 1.0f - static_cast<float>(i) / static_cast<float>(frames);
            pcm[static_cast<std::size_t>(i)] =
                0.4f * std::sin(2.0f * 3.14159265358979f * 880.0f *
                                static_cast<float>(i) / static_cast<float>(kBeepRate)) *
                env;
        }
        ma_audio_buffer_config bc =
            ma_audio_buffer_config_init(ma_format_f32, 1, frames, pcm.data(), NULL);
        if (ma_audio_buffer_init(&bc, &impl_->beep) == MA_SUCCESS) {
            impl_->beep_ok = true;
            // ma_audio_buffer IS a data source in v0.11 (no `ds` member).
            if (ma_sound_init_from_data_source(
                    &impl_->engine, static_cast<ma_data_source*>(&impl_->beep),
                    MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL,
                    &impl_->beep_sound) == MA_SUCCESS) {
                impl_->beep_sound_ok = true;
                ma_sound_set_volume(&impl_->beep_sound, 0.4f);
            }
        }
        std::fprintf(stdout, "[audio] NO wav samples in '%s' — beep fallback %s\n",
                     sfx_dir.c_str(), impl_->beep_sound_ok ? "OK" : "FAILED");
    }

    std::fprintf(stdout, "[audio] init: sfx_dir='%s' samples=%zu/%zu events=%zu\n",
                 sfx_dir.c_str(), loaded, total, events().size());
    std::fflush(stdout);
    enabled_ = impl_->engine_ok;
    return impl_->engine_ok;
}

void AudioEngine::shutdown() {
    if (impl_ == nullptr) return;
    if (impl_->engine_ok) {
        std::fprintf(stdout, "[audio] shutdown: total=%llu",
                     static_cast<unsigned long long>(played_total_));
        for (std::size_t e = 0; e < events().size(); ++e) {
            std::fprintf(stdout, " %s=%llu", events()[e].name,
                         static_cast<unsigned long long>(impl_->played[e]));
        }
        std::fprintf(stdout, " music=%llu\n",
                     static_cast<unsigned long long>(impl_->music_plays));
        for (std::size_t e = 0; e < impl_->sounds.size(); ++e) {
            for (ma_sound& s : impl_->sounds[e]) {
                ma_sound_uninit(&s);
            }
        }
        impl_->sounds.clear();
        if (impl_->music_ok) {
            ma_sound_stop(&impl_->music);
            ma_sound_uninit(&impl_->music);
            impl_->music_ok = false;
            impl_->music_current.clear();
        }
        if (impl_->beep_sound_ok) {
            ma_sound_uninit(&impl_->beep_sound);
            impl_->beep_sound_ok = false;
        }
        if (impl_->beep_ok) {
            ma_audio_buffer_uninit(&impl_->beep);
            impl_->beep_ok = false;
        }
        ma_engine_uninit(&impl_->engine);
        impl_->engine_ok = false;
        std::fflush(stdout);
    }
    enabled_ = false;
}

void AudioEngine::play(const std::string& event) {
    ++played_total_;
    const int e = find_event_index(event);
    if (e < 0) return;
    const EventDef& ev = events()[static_cast<std::size_t>(e)];
    ++impl_->played[static_cast<std::size_t>(e)];

    if (!enabled_ || !impl_->engine_ok) return;

    // Round-robin over the event's voices: voice v always holds the sample
    // files[v % n], so consecutive plays walk the event's file pool.
    const int v = impl_->next_voice[static_cast<std::size_t>(e)];
    impl_->next_voice[static_cast<std::size_t>(e)] = (v + 1) % ev.voices;

    ma_sound* sound = nullptr;
    if (static_cast<std::size_t>(v) < impl_->sounds[static_cast<std::size_t>(e)].size()) {
        sound = &impl_->sounds[static_cast<std::size_t>(e)][static_cast<std::size_t>(v)];
    } else if (impl_->beep_sound_ok) {
        sound = &impl_->beep_sound;
    }
    if (sound == nullptr) return;

    // Restart the clip (miniaudio: stop + rewind + start; thread-safe —
    // the engine thread picks the commands up asynchronously).
    ma_sound_stop(sound);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);

    // Log each event once — the headless "audio::enqueue did not crash and
    // the counter moved" proof.
    if (impl_->first_logged[static_cast<std::size_t>(e)] == 0) {
        impl_->first_logged[static_cast<std::size_t>(e)] = 1;
        const std::size_t fi =
            static_cast<std::size_t>(v) % ev.files.size();
        std::fprintf(stdout, "[audio] play '%s' (voice %d -> %s.wav)\n", ev.name, v,
                     ev.files[fi]);
        std::fflush(stdout);
    }
}

std::uint64_t AudioEngine::played(const std::string& event) const {
    const int e = find_event_index(event);
    if (e < 0 || impl_ == nullptr) return 0;
    return impl_->played[static_cast<std::size_t>(e)];
}

void AudioEngine::play_music(const std::string& track) {
    if (impl_ == nullptr || track.empty()) return;
    ++impl_->music_plays;
    if (impl_->music_ok && impl_->music_current == track &&
        ma_sound_is_playing(&impl_->music)) {
        return;  // same track already playing
    }
    std::fprintf(stdout, "[music] play '%s'\n", track.c_str());
    std::fflush(stdout);
    if (!impl_->engine_ok) return;  // counted + logged, silent headless
    if (impl_->music_ok) {
        ma_sound_stop(&impl_->music);
        ma_sound_uninit(&impl_->music);
        impl_->music_ok = false;
    }
    const std::string path = std::string("assets/music/") + track + ".mp3";
    const ma_result r = ma_sound_init_from_file(
        &impl_->engine, path.c_str(),
        MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_PITCH |
            MA_SOUND_FLAG_NO_SPATIALIZATION,
        NULL, NULL, &impl_->music);
    if (r != MA_SUCCESS) {
        std::fprintf(stderr, "[music] load failed: %s (%d)\n", path.c_str(),
                     static_cast<int>(r));
        return;
    }
    impl_->music_ok = true;
    impl_->music_current = track;
    ma_sound_set_volume(&impl_->music, 0.7f);
    ma_sound_set_looping(&impl_->music, MA_TRUE);
    ma_sound_start(&impl_->music);
}

void AudioEngine::stop_music() {
    if (impl_ == nullptr) return;
    if (impl_->music_ok) {
        ma_sound_stop(&impl_->music);
        ma_sound_uninit(&impl_->music);
        impl_->music_ok = false;
        impl_->music_current.clear();
        std::fprintf(stdout, "[music] stop\n");
        std::fflush(stdout);
    }
}

std::string AudioEngine::music_track() const {
    if (impl_ == nullptr) return {};
    return impl_->music_ok ? impl_->music_current : std::string();
}

std::uint64_t AudioEngine::music_plays() const {
    if (impl_ == nullptr) return 0;
    return impl_->music_plays;
}

}  // namespace sf2::audio