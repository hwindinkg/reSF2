// engine/audio/audio.cpp
//
// Audio system implementation: WAV loader + mixer.

#include "audio.hpp"
#include <cstdio>
#include <fstream>
#include <cmath>
#include <algorithm>

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

} // namespace

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

PcmData load_mp3_file(const std::string& path) {
    // [HEURISTIC-TODO] MP3 loading not implemented.
    // Integrate minmp3 (https://github.com/lieff/minimp3) for full music support.
    // The original game uses Marmalade's s3eAudio MP3 decoder (libavcodec/ffmpeg).
    std::fprintf(stderr, "[audio] MP3 not supported yet: %s\n", path.c_str());
    return PcmData();
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
    if (!backend->open(output_sample_rate_, output_channels_)) {
        std::fprintf(stderr, "[audio] Backend open failed, using null\n");
        backend = std::make_unique<NullAudioBackend>();
        backend->open(output_sample_rate_, output_channels_);
    }
    backend_ = std::move(backend);
    initialized_ = true;
    std::printf("[audio] Engine initialized: %u Hz, %u ch\n",
                output_sample_rate_, output_channels_);
    return true;
}

void AudioEngine::shutdown() {
    if (backend_) backend_->close();
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

std::shared_ptr<WavSound> AudioEngine::get_sound(const std::string& name) const {
    auto it = sounds_.find(name);
    return it != sounds_.end() ? it->second : nullptr;
}

uint32_t AudioEngine::play(const std::string& name, float volume, bool looping) {
    auto snd = get_sound(name);
    if (!snd) {
        std::fprintf(stderr, "[audio] Sound not found: %s\n", name.c_str());
        return 0;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    SoundInstance inst;
    inst.id = next_instance_id_++;
    inst.sound = snd.get();
    inst.volume = volume;
    inst.looping = looping;
    instances_.push_back(inst);
    return inst.id;
}

void AudioEngine::stop(uint32_t instance_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& inst : instances_) {
        if (inst.id == instance_id) { inst.finished = true; break; }
    }
}

void AudioEngine::stop_all() {
    std::lock_guard<std::mutex> lk(mutex_);
    instances_.clear();
}

void AudioEngine::resample_mix(const SoundInstance& inst, float* out, uint32_t frames) {
    if (!inst.sound || inst.finished) return;
    const PcmData& pcm = inst.sound->pcm();
    if (!pcm.valid()) return;

    double step = double(pcm.sample_rate) / double(output_sample_rate_) * inst.pitch;
    double cursor = inst.cursor;
    for (uint32_t i = 0; i < frames; ++i) {
        if (cursor >= double(pcm.frame_count)) {
            if (inst.looping) {
                cursor = std::fmod(cursor, double(pcm.frame_count));
            } else {
                break;
            }
        }
        uint32_t idx = uint32_t(cursor);
        double frac = cursor - double(idx);
        // Linear interpolation between frame idx and idx+1
        uint32_t idx2 = (idx + 1 < pcm.frame_count) ? idx + 1 : idx;
        for (uint16_t ch = 0; ch < output_channels_; ++ch) {
            uint16_t src_ch = (ch < pcm.channels) ? ch : (pcm.channels - 1);
            float s0 = pcm.samples[size_t(idx) * pcm.channels + src_ch];
            float s1 = pcm.samples[size_t(idx2) * pcm.channels + src_ch];
            float s = float(s0 + (s1 - s0) * frac) * inst.volume * master_volume_;
            out[i * output_channels_ + ch] += s;
        }
        cursor += step;
    }
}

void AudioEngine::update(float dt) {
    if (!initialized_ || !backend_ || !backend_->is_open()) return;
    uint32_t frames = uint32_t(dt * output_sample_rate_);
    if (frames == 0) frames = 1;
    if (frames > 4096) frames = 4096; // cap to avoid huge buffers
    std::vector<float> buffer(size_t(frames) * output_channels_, 0.0f);

    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& inst : instances_) {
        if (inst.finished) continue;
        // Mix into buffer, advance cursor
        double step = double(inst.sound->pcm().sample_rate) / double(output_sample_rate_) * inst.pitch;
        double cursor = inst.cursor;
        const PcmData& pcm = inst.sound->pcm();
        for (uint32_t i = 0; i < frames; ++i) {
            if (cursor >= double(pcm.frame_count)) {
                if (inst.looping) {
                    cursor = std::fmod(cursor, double(pcm.frame_count));
                } else {
                    inst.finished = true;
                    break;
                }
            }
            uint32_t idx = uint32_t(cursor);
            double frac = cursor - double(idx);
            uint32_t idx2 = (idx + 1 < pcm.frame_count) ? idx + 1 : idx;
            for (uint16_t ch = 0; ch < output_channels_; ++ch) {
                uint16_t src_ch = (ch < pcm.channels) ? ch : (pcm.channels - 1);
                float s0 = pcm.samples[size_t(idx) * pcm.channels + src_ch];
                float s1 = pcm.samples[size_t(idx2) * pcm.channels + src_ch];
                float s = float(s0 + (s1 - s0) * frac) * inst.volume * master_volume_;
                // Soft clip to prevent clipping
                buffer[i * output_channels_ + ch] += s;
            }
            cursor += step;
        }
        inst.cursor = uint32_t(cursor);
    }
    // Soft clip and write
    for (auto& s : buffer) {
        if (s > 1.0f) s = 1.0f;
        else if (s < -1.0f) s = -1.0f;
    }
    backend_->write(buffer.data(), frames);
    // Remove finished instances
    instances_.erase(
        std::remove_if(instances_.begin(), instances_.end(),
                       [](const SoundInstance& i) { return i.finished; }),
        instances_.end());
}

} // namespace resf2::audio
