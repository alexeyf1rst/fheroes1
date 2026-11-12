#include "Internal.h"
#include "Media.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <future>
#include <optional>
#include <string>
#endif

namespace platform {

namespace {

SDL_AudioDeviceID gDevice = 0;

// ---------------------------------------------------------------- samples

// A looping voice refills its stream from the audio thread for as long as it
// plays, so it keeps its own copy of the sample: the game may free the
// sample's resource while it still plays (its StopSample does nothing while
// samples are suspended, as during a computer player's turn).
struct VoiceState {
    SDL_AudioStream* stream = nullptr;
    std::vector<u8> data;
    int bytes = 0;
    bool loop = false;
};

std::map<Voice, VoiceState> gVoices;
Voice gNextVoice = 1;

void SDLCALL RefillLoop(void* userdata, SDL_AudioStream* stream, int additional, int) {
    const VoiceState* voice = static_cast<const VoiceState*>(userdata);
    while (additional > 0 && voice->bytes > 0) {
        if (!SDL_PutAudioStreamData(stream, voice->data.data(), voice->bytes))
            break;
        additional -= voice->bytes;
    }
}

bool StreamBusy(SDL_AudioStream* stream) {
    return SDL_GetAudioStreamQueued(stream) > 0 || SDL_GetAudioStreamAvailable(stream) > 0;
}

void ReapVoices() {
    for (auto it = gVoices.begin(); it != gVoices.end();) {
        if (!it->second.loop && !StreamBusy(it->second.stream)) {
            SDL_DestroyAudioStream(it->second.stream);
            it = gVoices.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- music

// Advance the source by more than one output hop, then align and blend the
// overlapping waveform. Both channels use the same offset to preserve stereo.
class MusicTempo {
public:
    void Reset() {
        input.clear();
        tail.clear();
        base = written = 0;
    }

    void Convert(std::vector<i16>& buffer, int tempo, bool end) {
        constexpr int channels = media::kMediaChannels;
        constexpr int hop = media::kMediaRate * 15 / 1000;
        constexpr int search = media::kMediaRate * 8 / 1000;
        input.insert(input.end(), buffer.begin(), buffer.end());
        buffer.clear();
        const i64 available = base + static_cast<i64>(input.size() / channels);
        const i64 limit = available * 3 / tempo;
        while (written < limit) {
            const i64 expected = written * tempo / 3;
            if (!end && expected + search + 2 * hop > available)
                break;
            const int count = static_cast<int>(std::min<i64>(hop, limit - written));
            const i64 latest = available - count;
            i64 best = std::clamp(expected, base, latest);
            if (!tail.empty()) {
                double bestScore = -2;
                for (i64 frame = std::max(base, expected - search);
                     frame <= std::min(latest, expected + search); frame += 2) {
                    const i16* candidate = input.data() + (frame - base) * channels;
                    double dot = 0, leftEnergy = 0, rightEnergy = 0;
                    for (int j = 0; j < count * channels; j += 4 * channels) {
                        for (int channel = 0; channel < channels; ++channel) {
                            const double left = tail[j + channel];
                            const double right = candidate[j + channel];
                            dot += left * right;
                            leftEnergy += left * left;
                            rightEnergy += right * right;
                        }
                    }
                    if (leftEnergy == 0 || rightEnergy == 0)
                        continue;
                    const double similarity = dot / std::sqrt(leftEnergy * rightEnergy);
                    const double score = similarity - 0.01 * std::abs(frame - expected) / search;
                    if (score > bestScore) {
                        best = frame;
                        bestScore = score;
                    }
                }
            }
            const i16* source = input.data() + (best - base) * channels;
            for (int frame = 0; frame < count; ++frame) {
                for (int channel = 0; channel < channels; ++channel) {
                    const int index = frame * channels + channel;
                    const int value = tail.empty() ? source[index]
                        : (tail[index] * (hop - frame) + source[index] * frame) / hop;
                    buffer.push_back(static_cast<i16>(value));
                }
            }
            written += count;
            if (written < limit || !end) {
                // At EOF the search still needs a full overlap for the next hop.
                const int next = static_cast<int>(std::min<i64>(hop, available - best - count));
                tail.assign(source + count * channels, source + (count + next) * channels);
                tail.resize(hop * channels, 0);
            }
        }
        const i64 discard = std::clamp<i64>(written * tempo / 3 - search, base, available);
        input.erase(input.begin(), input.begin() + (discard - base) * channels);
        base = discard;
    }

private:
    std::vector<i16> input;
    std::vector<i16> tail;
    i64 base = 0;
    i64 written = 0;
};

struct MusicState {
    media::Decoder decoder;
    SDL_AudioStream* stream = nullptr;
    bool loop = false;
    bool finished = false;
    double positionFrames = 0;
    int tempo = 3;
    MusicTempo compression;
    std::vector<i16> buffer;

    ~MusicState() { SDL_DestroyAudioStream(stream); }
};

std::unique_ptr<MusicState> gMusic;

#ifdef _WIN32
struct MusicRequest {
    std::string path;
    bool loop;
    double startSeconds;
    int tempo;
};

std::future<std::unique_ptr<MusicState>> gMusicPreparation;
std::optional<MusicRequest> gNextMusic;
bool gUsePreparedMusic = false;
float gPendingMusicVolume = 1.0f;
#endif

void SDLCALL FeedMusic(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto& music = *static_cast<MusicState*>(userdata);
    bool rewound = false;
    while (additional > 0 && !music.finished) {
        music.buffer.clear();
        const bool decoded = music.decoder.Next(nullptr, nullptr, music.buffer);
        if (music.tempo > 3)
            music.compression.Convert(music.buffer, music.tempo, !decoded);
        if (!music.buffer.empty()) {
            const int bytes = static_cast<int>(music.buffer.size() * sizeof(i16));
            if (!SDL_PutAudioStreamData(stream, music.buffer.data(), bytes)) {
                music.finished = true;
                break;
            }
            music.positionFrames += music.buffer.size() / media::kMediaChannels * (music.tempo / 3.0);
            rewound = false;
            additional -= bytes;
        }
        if (!decoded) {
            if (music.loop && !rewound && music.decoder.SeekSeconds(0)) {
                music.positionFrames = 0;
                music.compression.Reset();
                rewound = true;
                continue;
            }
            SDL_FlushAudioStream(stream);
            music.finished = true;
            break;
        }
    }
}

SDL_AudioSpec MediaSpec() {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = media::kMediaChannels;
    spec.freq = media::kMediaRate;
    return spec;
}

std::unique_ptr<MusicState> PrepareMusic(const char* hostPath, bool loop, double startSeconds, int tempo) {
    auto music = std::make_unique<MusicState>();
    if (!music->decoder.Open(hostPath, false, true))
        return nullptr;
    if (startSeconds > 0 && !music->decoder.SeekSeconds(startSeconds)) {
        music->decoder.SeekSeconds(0);
        startSeconds = 0;
    }
    music->loop = loop;
    music->tempo = tempo;
    music->positionFrames = static_cast<i64>(startSeconds * media::kMediaRate);
    SDL_AudioSpec spec = MediaSpec();
    music->stream = SDL_CreateAudioStream(&spec, &spec);
    if (music->stream == nullptr)
        return nullptr;
    FeedMusic(music.get(), music->stream, 1, 0);
    if (!StreamBusy(music->stream) || !SDL_SetAudioStreamGetCallback(music->stream, FeedMusic, music.get()))
        return nullptr;
    return music;
}

bool BindMusic(std::unique_ptr<MusicState> music, float volume) {
    if (!music || !SDL_SetAudioStreamGain(music->stream, volume) || !SDL_BindAudioStream(gDevice, music->stream))
        return false;
    gMusic = std::move(music);
    return true;
}

}  // namespace

// ---------------------------------------------------------------- device

bool OpenAudio() {
    if (gDevice != 0)
        return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        Log("no audio: %s", SDL_GetError());
        return false;
    }
    gDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (gDevice == 0) {
        Log("no audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    return true;
}

void CloseAudio() {
    if (gDevice == 0)
        return;
    StopMusic();
#ifdef _WIN32
    if (gMusicPreparation.valid())
        gMusicPreparation.get();
#endif
    for (auto& entry : gVoices)
        SDL_DestroyAudioStream(entry.second.stream);
    gVoices.clear();
    SDL_CloseAudioDevice(gDevice);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    gDevice = 0;
}

bool AudioOpen() {
    return gDevice != 0;
}

SDL_AudioStream* OpenMediaStream() {
    if (gDevice == 0)
        return nullptr;
    SDL_AudioSpec spec = MediaSpec();
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
    if (stream != nullptr && !SDL_BindAudioStream(gDevice, stream)) {
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }
    return stream;
}

// ---------------------------------------------------------------- samples

Voice PlaySample(
    const void* data,
    std::size_t bytes,
    int rate,
    int channels,
    SampleFormat format,
    float volume,
    bool loop
) {
    if (gDevice == 0 || data == nullptr || bytes == 0 || rate <= 0 || channels <= 0
        || (!loop && volume <= 0))
        return NO_VOICE;
    ReapVoices();
    SDL_AudioSpec spec;
    spec.format = format == SAMPLE_U8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE;
    spec.channels = channels;
    spec.freq = rate;
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, &spec);
    if (stream == nullptr)
        return NO_VOICE;
    Voice id = gNextVoice++;
    VoiceState& voice = gVoices[id];
    voice.stream = stream;
    if (loop)
        voice.data.assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + bytes);
    voice.bytes = static_cast<int>(bytes);
    voice.loop = loop;
    if (!SDL_SetAudioStreamGain(stream, volume)
        || !SDL_PutAudioStreamData(stream, data, voice.bytes)
        || !(loop ? SDL_SetAudioStreamGetCallback(stream, RefillLoop, &voice)
                  : SDL_FlushAudioStream(stream))
        || !SDL_BindAudioStream(gDevice, stream)) {
        SDL_DestroyAudioStream(stream);
        gVoices.erase(id);
        return NO_VOICE;
    }
    return id;
}

void StopVoice(Voice voice) {
    auto found = gVoices.find(voice);
    if (found == gVoices.end())
        return;
    SDL_DestroyAudioStream(found->second.stream);
    gVoices.erase(found);
}

bool VoicePlaying(Voice voice) {
    auto found = gVoices.find(voice);
    if (found == gVoices.end())
        return false;
    return found->second.loop || StreamBusy(found->second.stream);
}

void SetVoiceVolume(Voice voice, float volume) {
    auto found = gVoices.find(voice);
    if (found != gVoices.end())
        SDL_SetAudioStreamGain(found->second.stream, volume);
}

// ---------------------------------------------------------------- music

bool PlayMusic(const char* hostPath, bool loop, double startSeconds, float volume, int tempo, bool background) {
    if (gDevice == 0)
        return false;
#ifdef _WIN32
    gNextMusic.reset();
    gUsePreparedMusic = false;
    if (background) {
        gNextMusic = MusicRequest{hostPath, loop, startSeconds, tempo};
        gPendingMusicVolume = volume;
        PumpMusic();
        return true;
    }
#endif
    return BindMusic(PrepareMusic(hostPath, loop, startSeconds, tempo), volume);
}

void PumpMusic() {
#ifdef _WIN32
    if (gMusicPreparation.valid()) {
        if (gMusicPreparation.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        auto music = gMusicPreparation.get();
        if (gUsePreparedMusic) {
            const bool trace = !Environment("HOMM1_TRACE_DISPLAY").empty();
            const u32 startedAt = trace ? Ticks() : 0;
            if (!BindMusic(std::move(music), gPendingMusicVolume))
                Log("cannot start prepared music");
            if (trace)
                Log("music binding: %u ms", Ticks() - startedAt);
        }
        gUsePreparedMusic = false;
    }
    if (gNextMusic) {
        gMusicPreparation = std::async(std::launch::async, [request = std::move(*gNextMusic)] {
            return PrepareMusic(request.path.c_str(), request.loop, request.startSeconds, request.tempo);
        });
        gNextMusic.reset();
        gUsePreparedMusic = true;
    }
#endif
}

void StopMusic() {
#ifdef _WIN32
    gNextMusic.reset();
    gUsePreparedMusic = false;
#endif
    gMusic.reset();
}

bool MusicPlaying() {
#ifdef _WIN32
    if (gUsePreparedMusic || gNextMusic)
        return true;
#endif
    if (!gMusic)
        return false;
    // SDL already holds the stream lock when it calls FeedMusic.
    SDL_LockAudioStream(gMusic->stream);
    const bool playing = !gMusic->finished || StreamBusy(gMusic->stream);
    SDL_UnlockAudioStream(gMusic->stream);
    return playing;
}

double MusicPosition() {
    if (!gMusic)
        return 0;
    SDL_LockAudioStream(gMusic->stream);
    constexpr int frameBytes = sizeof(i16) * media::kMediaChannels;
    const int queuedFrames = std::max(0, SDL_GetAudioStreamQueued(gMusic->stream)) / frameBytes;
    const double playedFrames = std::max(0.0, gMusic->positionFrames - queuedFrames * (gMusic->tempo / 3.0));
    SDL_UnlockAudioStream(gMusic->stream);
    return static_cast<double>(playedFrames) / media::kMediaRate;
}

void SetMusicVolume(float volume) {
#ifdef _WIN32
    gPendingMusicVolume = volume;
#endif
    if (gMusic)
        SDL_SetAudioStreamGain(gMusic->stream, volume);
}

// ---------------------------------------------------------------- movies

struct Movie {
    media::Decoder decoder;
    MovieInfo info;
    std::vector<u8> pixels;
    std::vector<u8> palette;
    std::vector<i16> audio;
    SDL_AudioStream* stream = nullptr;
};

Movie* OpenMovie(const char* hostPath, bool withAudio, float volume) {
    auto movie = std::make_unique<Movie>();
    if (!movie->decoder.Open(hostPath, true, withAudio && gDevice != 0))
        return nullptr;
    movie->info.width = movie->decoder.Width();
    movie->info.height = movie->decoder.Height();
    movie->info.frames = movie->decoder.FrameCount();
    movie->info.framesPerSecond = movie->decoder.FramesPerSecond();
    movie->pixels.assign(static_cast<size_t>(movie->info.width * movie->info.height), 0);
    movie->palette.assign(256 * 3, 0);
    if (movie->decoder.HasAudio()) {
        movie->stream = OpenMediaStream();
        if (movie->stream != nullptr)
            SDL_SetAudioStreamGain(movie->stream, volume);
    }
    return movie.release();
}

void CloseMovie(Movie* movie) {
    if (movie == nullptr)
        return;
    if (movie->stream != nullptr)
        SDL_DestroyAudioStream(movie->stream);
    delete movie;
}

const MovieInfo& MovieDetails(const Movie* movie) {
    return movie->info;
}

bool DecodeMovieFrame(Movie* movie) {
    movie->audio.clear();
    bool decoded = movie->decoder.Next(&movie->pixels, &movie->palette, movie->audio);
    if (movie->stream != nullptr && !movie->audio.empty())
        SDL_PutAudioStreamData(movie->stream, movie->audio.data(),
                               static_cast<int>(movie->audio.size() * sizeof(i16)));
    return decoded;
}

const u8* MovieFramePixels(const Movie* movie) {
    return movie->pixels.data();
}

const u8* MovieFramePalette(const Movie* movie) {
    return movie->palette.data();
}

}  // namespace platform
