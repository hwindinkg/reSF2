// engine/audio/audio.cpp
//
// Audio system implementation: WAV loader, MP3 loader (minimp3), AudioEngine.

#include "audio.hpp"

// Define implementation before including minimp3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "minimp3.h"

#include <cstdio>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace resf2::audio {

// ---------- WAV loading ----------
// [ORIGINAL] Standard RIFF/WAVE parser. SF2's WAVs are PCM (format 1),
// 16-bit, mono or stereo, at 22050 or 44100 Hz (Marmalade s3eAudio default).

namespace {

struct WavHeader {
    char riff[4];        // "RIFF"
    uint32_t file_size;
    char wave[4];        // "WAVE"
};

struct WavChunkHeader {
    char id[4];
    uint32_t size;
};

struct WavFmt {
    uint16_t audio_format;    // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

uint16_t rd_u16le(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t rd_u32le(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // anonymous namespace

PcmData load_wav(const uint8_t* data, size_t size) {
    PcmData out;
    if (size < 12) return out;
    // Validate RIFF/WAVE
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        std::fprintf(stderr, "[audio] WAV: not RIFF/WAVE\n");
        return out;
    }
    size_t pos = 12;
    WavFmt fmt = {};
    const uint8_t* pcm_data = nullptr;
    uint32_t pcm_size = 0;
    while (pos + 8 <= size) {
        WavChunkHeader ch;
        std::memcpy(ch.id, data + pos, 4);
        ch.size = rd_u32le(data + pos + 4);
        pos += 8;
        if (pos + ch.size > size) break;
        if (std::memcmp(ch.id, "fmt ", 4) == 0) {
            if (ch.size < 16) return out;
            fmt.audio_format     = rd_u16le(data + pos + 0);
            fmt.num_channels     = rd_u16le(data + pos + 2);
            fmt.sample_rate      = rd_u32le(data + pos + 4);
            fmt.byte_rate        = rd_u32le(data + pos + 8);
            fmt.block_align      = rd_u16le(data + pos + 12);
            fmt.bits_per_sample  = rd_u16le(data + pos + 14);
        } else if (std::memcmp(ch.id, "data", 4) == 0) {
            pcm_data = data + pos;
            pcm_size = ch.size;
        }
        pos += ch.size + (ch.size & 1); // chunks are word-aligned
    }
    if (fmt.audio_format != 1) {
        std::fprintf(stderr, "[audio] WAV: unsupported format %u (only PCM=1)\n", fmt.audio_format);
        return out;
    }
    if (fmt.bits_per_sample != 8 && fmt.bits_per_sample != 16) {
        std::fprintf(stderr, "[audio] WAV: unsupported bits %u (only 8/16)\n", fmt.bits_per_sample);
        return out;
    }
    if (fmt.num_channels < 1 || fmt.num_channels > 2) {
        std::fprintf(stderr, "[audio] WAV: unsupported channels %u\n", fmt.num_channels);
        return out;
    }
    if (!pcm_data || pcm_size == 0) return out;

    out.sample_rate = fmt.sample_rate;
    out.channels = fmt.num_channels;
    uint32_t bytes_per_frame = uint32_t(fmt.num_channels) * (fmt.bits_per_sample / 8);
    out.frame_count = pcm_size / bytes_per_frame;
    out.samples.resize(size_t(out.frame_count) * out.channels);

    if (fmt.bits_per_sample == 8) {
        // 8-bit WAV is unsigned (0..255), center at 128
        for (uint32_t i = 0; i < out.frame_count * out.channels; ++i) {
            out.samples[i] = (float(pcm_data[i]) - 128.0f) / 128.0f;
        }
    } else {
        // 16-bit WAV is signed, little-endian
        const int16_t* p16 = reinterpret_cast<const int16_t*>(pcm_data);
        for (uint32_t i = 0; i < out.frame_count * out.channels; ++i) {
            out.samples[i] = float(p16[i]) / 32768.0f;
        }
    }
    return out;
}

PcmData load_wav_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[audio] Cannot open WAV: %s\n", path.c_str());
        return PcmData();
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
    return load_wav(data.data(), data.size());
}

// ---------- MP3 loading via minimp3 ----------

PcmData load_mp3_file(const std::string& path) {
    // Read the entire MP3 file into memory
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[audio] Cannot open MP3: %s\n", path.c_str());
        return PcmData();
    }
    auto sz = static_cast<size_t>(f.tellg());
    if (sz < 128) {
        std::fprintf(stderr, "[audio] MP3 too small: %s\n", path.c_str());
        return PcmData();
    }
    f.seekg(0);
    std::vector<uint8_t> mp3_data(sz);
    f.read(reinterpret_cast<char*>(mp3_data.data()), static_cast<std::streamsize>(sz));

    // Check for ID3v2 tag header at offset 0
    size_t data_offset = 0;
    if (sz >= 10 && std::memcmp(mp3_data.data(), "ID3", 3) == 0) {
        // Skip ID3v2 header (10 bytes) + tag size (syncsafe int)
        uint32_t id3_size = 0;
        id3_size = (uint32_t(mp3_data[6]) << 21) |
                   (uint32_t(mp3_data[7]) << 14) |
                   (uint32_t(mp3_data[8]) << 7)  |
                   (uint32_t(mp3_data[9]));
        data_offset = 10 + id3_size;
        if (data_offset >= sz) {
            std::fprintf(stderr, "[audio] MP3 ID3v2 header extends past file: %s\n", path.c_str());
            return PcmData();
        }
    }

    // Initialize minimp3 decoder
    mp3dec_t dec;
    mp3dec_init(&dec);

    // Decode all frames into a temporary vector
    std::vector<float> all_samples;
    all_samples.reserve(sz / 2); // rough estimate: ~2 bytes per sample for stereo MP3

    uint32_t decoded_sample_rate = 0;
    uint16_t decoded_channels = 0;

    // Buffer for one frame of decoded samples (minimp3 float output)
    // MINIMP3_MAX_SAMPLES_PER_FRAME = 1152*2 = 2304 samples max for stereo
    std::vector<float> frame_buf(MINIMP3_MAX_SAMPLES_PER_FRAME);

    size_t offset = data_offset;
    while (offset < sz) {
        mp3dec_frame_info_t info;
        std::memset(&info, 0, sizeof(info));

        int samples = mp3dec_decode_frame(&dec,
                                           mp3_data.data() + offset,
                                           (int)(sz - offset),
                                           frame_buf.data(),
                                           &info);

        if (samples <= 0) {
            // No valid frame found — skip forward until we find a sync
            offset++;
            continue;
        }

        decoded_sample_rate = (uint32_t)info.hz;
        decoded_channels = (uint16_t)info.channels;

        int total_samples = samples * info.channels;
        all_samples.insert(all_samples.end(),
                           frame_buf.begin(),
                           frame_buf.begin() + total_samples);

        offset += info.frame_bytes;
        if (info.frame_bytes <= 0) {
            // Safety: prevent infinite loop
            offset++;
            if (offset >= sz) break;
        }
    }

    if (all_samples.empty()) {
        std::fprintf(stderr, "[audio] MP3: no samples decoded from %s\n", path.c_str());
        return PcmData();
    }

    PcmData out;
    out.sample_rate = decoded_sample_rate;
    out.channels = decoded_channels;
    out.frame_count = (uint32_t)(all_samples.size() / decoded_channels);
    out.samples = std::move(all_samples);

    std::printf("[audio] MP3 decoded: %s (%u Hz, %u ch, %u frames, %.2fs)\n",
                path.c_str(), out.sample_rate, out.channels,
                out.frame_count, out.duration_seconds());

    return out;
}

// ---------- WavSound ----------
bool WavSound::load(const std::string& name, const PcmData& pcm) {
    if (!pcm.valid()) return false;
    name_ = name;
    pcm_ = pcm;
    return true;
}

// ---------- AudioEngine ----------
AudioEngine& AudioEngine::instance() {
    static AudioEngine eng;
    return eng;
}

bool AudioEngine::init(std::unique_ptr<AudioBackend> backend) {
    if (initialized_) return true;
    if (!backend) backend = std::make_unique<NullAudioBackend>();
    if (!backend->init()) {
        std::fprintf(stderr, "[audio] Backend init failed, using null\n");
        backend = std::make_unique<NullAudioBackend>();
        backend->init();
    }
    backend_ = std::move(backend);
    initialized_ = true;
    std::printf("[audio] Engine initialized\n");
    return true;
}

void AudioEngine::shutdown() {
    if (backend_) backend_->shutdown();
    backend_.reset();
    sounds_.clear();
    instances_.clear();
    initialized_ = false;
}

std::shared_ptr<WavSound> AudioEngine::load_sound(const std::string& name,
                                                    const uint8_t* data, size_t size) {
    PcmData pcm = load_wav(data, size);
    if (!pcm.valid()) return nullptr;
    auto snd = std::make_shared<WavSound>();
    if (!snd->load(name, pcm)) return nullptr;
    std::lock_guard<std::mutex> lk(mutex_);
    sounds_[name] = snd;
    return snd;
}

std::shared_ptr<WavSound> AudioEngine::load_sound_file(const std::string& name,
                                                         const std::string& path) {
    PcmData pcm = load_wav_file(path);
    if (!pcm.valid()) return nullptr;
    auto snd = std::make_shared<WavSound>();
    if (!snd->load(name, pcm)) return nullptr;
    std::lock_guard<std::mutex> lk(mutex_);
    sounds_[name] = snd;
    std::printf("[audio] Loaded '%s' from %s (%u frames, %.2fs)\n",
                name.c_str(), path.c_str(), pcm.frame_count, pcm.duration_seconds());
    return snd;
}

std::shared_ptr<WavSound> AudioEngine::load_music_file(const std::string& name,
                                                         const std::string& path) {
    PcmData pcm = load_mp3_file(path);
    if (!pcm.valid()) {
        // Fallback: try WAV
        pcm = load_wav_file(path);
        if (!pcm.valid()) return nullptr;
    }
    auto snd = std::make_shared<WavSound>();
    if (!snd->load(name, pcm)) return nullptr;
    std::lock_guard<std::mutex> lk(mutex_);
    sounds_[name] = snd;
    std::printf("[audio] Loaded music '%s' from %s (%u frames, %.2fs)\n",
                name.c_str(), path.c_str(), pcm.frame_count, pcm.duration_seconds());
    return snd;
}

std::shared_ptr<WavSound> AudioEngine::get_sound(const std::string& name) const {
    auto it = sounds_.find(name);
    return it != sounds_.end() ? it->second : nullptr;
}

uint32_t AudioEngine::play(const std::string& name, float volume, bool looping) {
    auto snd = get_sound(name);
    if (!snd || !snd->pcm().valid()) {
        std::fprintf(stderr, "[audio] Sound not found or invalid: %s\n", name.c_str());
        return 0;
    }
    if (!backend_) return 0;

    backend_->play_sound(snd->pcm(), volume * sfx_volume_, 0.0f);
    last_played_name_ = name;
    return next_instance_id_++;
}

void AudioEngine::stop(uint32_t instance_id) {
    (void)instance_id;
    // With OpenAL backend, individual instance stop is managed by source pool
    // For now, this is a no-op for individual stops
}

void AudioEngine::stop_all() {
    if (backend_) {
        backend_->stop_music();
    }
}

void AudioEngine::play_music(const std::string& name, float volume, bool loop) {
    auto snd = get_sound(name);
    if (!snd || !snd->pcm().valid()) {
        std::fprintf(stderr, "[audio] Music not found: %s\n", name.c_str());
        return;
    }
    if (!backend_) return;
    backend_->play_music(snd->pcm(), volume * music_volume_, loop);
}

void AudioEngine::stop_music() {
    if (backend_) backend_->stop_music();
}

void AudioEngine::set_music_volume(float v) {
    music_volume_ = std::clamp(v, 0.0f, 1.0f);
    if (backend_) backend_->set_music_volume(music_volume_);
}

void AudioEngine::set_sfx_volume(float v) {
    sfx_volume_ = std::clamp(v, 0.0f, 1.0f);
    if (backend_) backend_->set_sfx_volume(sfx_volume_);
}

void AudioEngine::update(float dt) {
    if (!initialized_ || !backend_) return;
    backend_->update(dt);
}

} // namespace resf2::audio
