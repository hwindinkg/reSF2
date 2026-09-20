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
            // `ta.WBa` is name -> ONE asset id: resolve the row's wav stem.
            // A row whose stem is missing on disk has no JS-playable sample
            // (`sfx_stem_for_js` returns nullptr exactly where the JS plays
            // nothing), so it is NOT registered — `play(name)` then finds no
            // event and `played(name)` stays 0.
            const char* stem = sfx_stem_for_js(groups[g].event);
            if (stem == nullptr) continue;
            EventDef e;
            e.name = groups[g].event;
            e.files.push_back(stem);
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

namespace {

// [latency probe] Enabled by env `SF2_AUDIO_LATENCY=1`. Measurement only — it
// adds NO latency to the audio path. The trigger logs the engine clock and the
// sound's `ma_sound_get_cursor_in_pcm_frames` read cursor; the per-frame
// `latency_tick()` then reports how many 60 Hz frames pass before the device
// actually starts feeding the sound (the port-side latency the JS WebAudio
// path `ta.ak` L1264 does not add).
struct LatencyProbe {
    bool pending = false;
    ma_sound* node = nullptr;
    int ticks = 0;
    ma_uint64 t0_ms = 0;
    std::string kind;
    std::string name;
};
LatencyProbe g_latency;

bool latency_probe_enabled() {
    static const bool on = std::getenv("SF2_AUDIO_LATENCY") != nullptr;
    return on;
}

void log_latency(const ma_engine& engine, bool engine_ok, const char* kind,
                 const std::string& name, ma_sound* sound) {
    if (!latency_probe_enabled()) return;
    if (!engine_ok) {
        std::fprintf(stdout,
                     "[latency] %s '%s' engine=off (no device; unmeasurable)\n",
                     kind, name.c_str());
        std::fflush(stdout);
        return;
    }
    const ma_uint32 sr = ma_engine_get_sample_rate(&engine);
    const ma_uint64 t_ms = ma_engine_get_time_in_milliseconds(&engine);
    ma_uint64 cursor = 0;
    if (sound != nullptr) ma_sound_get_cursor_in_pcm_frames(sound, &cursor);
    const double cursor_ms =
        sr > 0 ? 1000.0 * static_cast<double>(cursor) / sr : 0.0;
    std::fprintf(stdout,
                 "[latency] TRIGGER %s '%s' engine_ms=%llu frame=%.1f "
                 "cursor=%.3f ms sr=%u\n",
                 kind, name.c_str(), static_cast<unsigned long long>(t_ms),
                 60.0 * static_cast<double>(t_ms) / 1000.0, cursor_ms, sr);
    std::fflush(stdout);
    g_latency.pending = true;
    g_latency.node = sound;
    g_latency.ticks = 0;
    g_latency.t0_ms = t_ms;
    g_latency.kind = kind;
    g_latency.name = name;
}

void latency_tick_impl(const ma_engine& engine, bool engine_ok) {
    if (!g_latency.pending) return;
    if (!engine_ok) {
        g_latency.pending = false;
        return;
    }
    ++g_latency.ticks;
    const ma_uint32 sr = ma_engine_get_sample_rate(&engine);
    ma_uint64 cursor = 0;
    if (g_latency.node != nullptr) {
        ma_sound_get_cursor_in_pcm_frames(g_latency.node, &cursor);
    }
    const ma_uint64 now = ma_engine_get_time_in_milliseconds(&engine);
    const double cursor_ms =
        sr > 0 ? 1000.0 * static_cast<double>(cursor) / sr : 0.0;
    const double elapsed = static_cast<double>(now - g_latency.t0_ms);
    if (cursor > 0 || g_latency.ticks >= 30) {
        std::fprintf(stdout,
                     "[latency] FEED %s '%s' after %d polls (%.1f ms) "
                     "cursor=%.3f ms\n",
                     g_latency.kind.c_str(), g_latency.name.c_str(),
                     g_latency.ticks, elapsed, cursor_ms);
        std::fflush(stdout);
        g_latency.pending = false;
        return;
    }
    std::fprintf(stdout, "[latency]   poll +%d cursor=%.3f ms\n",
                 g_latency.ticks, cursor_ms);
    std::fflush(stdout);
}

}  // namespace

// Called once per presented frame by the game loop (main.cpp driver tick).
void AudioEngine::latency_tick() {
    if (impl_ == nullptr) return;
    latency_tick_impl(impl_->engine, enabled_ && impl_->engine_ok);
}

void AudioEngine::play(const std::string& event) {
    // [latency probe] poll the pending trigger so the feed point is measured
    // from the audio events themselves (no app-loop hook required).
    if (impl_ != nullptr && latency_probe_enabled()) {
        latency_tick_impl(impl_->engine, enabled_ && impl_->engine_ok);
    }
    ++played_total_;
    const int e = find_event_index(event);
    if (e < 0) return;
    const EventDef& ev = events()[static_cast<std::size_t>(e)];
    ++impl_->played[static_cast<std::size_t>(e)];

    if (!enabled_ || !impl_->engine_ok) {
        // [latency probe] the off-device path still reports the (unmeasurable)
        // trigger for the FIRST play of each event.
        if (impl_->first_logged[static_cast<std::size_t>(e)] == 0) {
            impl_->first_logged[static_cast<std::size_t>(e)] = 1;
            log_latency(impl_->engine, false, "sfx", event, nullptr);
        }
        return;
    }

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
        log_latency(impl_->engine, impl_->engine_ok, "sfx", event, sound);
        std::fflush(stdout);
    }
}

std::uint64_t AudioEngine::played(const std::string& event) const {
    const int e = find_event_index(event);
    if (e < 0 || impl_ == nullptr) return 0;
    return impl_->played[static_cast<std::size_t>(e)];
}

// JS `ta.Jwb(a)` (L1264): `a=ta.WBa(a); a!=null && L.K.$f.stop(a)`. `WBa`
// resolves the event name to the ONE playing source; a miss stops nothing.
// Native: stop + rewind every voice of the event (the port has no
// per-instance handle, so a stop clears the whole pool — the JS `$f.stop`
// stops the single active source per name).
void AudioEngine::stop(const std::string& event) {
    const int e = find_event_index(event);
    if (e < 0 || impl_ == nullptr) return;
    std::fprintf(stdout, "[audio] stop '%s'\n", event.c_str());
    std::fflush(stdout);
    if (!enabled_ || !impl_->engine_ok) return;  // headless: logged only
    for (ma_sound& s : impl_->sounds[static_cast<std::size_t>(e)]) {
        ma_sound_stop(&s);
        ma_sound_seek_to_pcm_frame(&s, 0);
    }
}

void AudioEngine::play_music(const std::string& track, bool loop) {
    if (impl_ == nullptr || track.empty()) return;
    if (latency_probe_enabled()) {
        latency_tick_impl(impl_->engine, enabled_ && impl_->engine_ok);
    }
    ++impl_->music_plays;
    if (impl_->music_ok && impl_->music_current == track &&
        ma_sound_is_playing(&impl_->music)) {
        return;  // same track already playing
    }
    std::fprintf(stdout, "[music] play '%s'%s\n", track.c_str(),
                 loop ? "" : " (once)");
    std::fflush(stdout);
    if (!impl_->engine_ok) {
        log_latency(impl_->engine, false, "music", track, nullptr);
        return;  // counted + logged, silent headless
    }
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
    ma_sound_set_looping(&impl_->music, loop ? MA_TRUE : MA_FALSE);
    ma_sound_start(&impl_->music);
    log_latency(impl_->engine, impl_->engine_ok, "music", track, &impl_->music);
}

// JS `lb.OS(a,b)` (L1276): `b==null&&(b=!0); a==null&&(a="menu");
// lb.rJ||(lb.rJ=!0,ta.Ut(a,b))`. The guard makes the menu track survive
// Map/Shop/Profile/Dojo hops and only restart after a fight/act cleared it.
void AudioEngine::play_music_once(const std::string& track, bool loop) {
    if (music_guard_) return;
    music_guard_ = true;
    play_music(track, loop);
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