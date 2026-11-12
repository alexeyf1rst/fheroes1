// The native sound host: the counterpart of src/BASE/Audio.cpp (Audiere for
// effects and music, Miles for movie sound). The policy the original host
// carried - which tracks repeat, where a track resumes, the volume steps, the
// CD track numbers - is kept as it was; only the device side changes.

#include <H1/Ints.h>

#include <BASE/audio.h>
#include <BASE/resourceManager.h>
#include <BASE/sample.h>
#include <SOURCE/KB.h>
#include <SOURCE/NOOPT.h>
#include <SOURCE/smackManager.h>

#include <PLATFORM/File.h>
#include <PLATFORM/Platform.h>

#include "../PortHost.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<sample*, platform::Voice> gVoices;
int gSampleSuspensions = 0;
int gMusicSuspensions = 0;
int gCurrentTrack = -1;
int gCurrentMusicTempo = 3;
int gMusicSource = 0;
double gMusicPositions[100];
bool gAudioReady = false;
bool gMovieSoundReady = false;
float gMovieVolume = 1.0f;
float gEffectsVolume = 1.0f;
float gMusicVolume = 1.0f;

constexpr float kVolumeLevels[11] =
    {0.0f, 1.0f, 0.8f, 0.65f, 0.5f, 0.4f, 0.3f, 0.2f, 0.15f, 0.1f, 0.05f};

// The CD's audio track for each game track (-1: none).
constexpr int kCDTrackMap[100] = {
    2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, -1, -1, -1, -1, -1, -1, -1,
    35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 50,
};

bool ValidTrack(int track) {
    return track >= 0 && track < 100;
}

// Prefer the selected CD soundtrack, then the game's digital tracks.
std::string TrackPath(int track) {
    char name[FILE_PATH_CAPACITY];
    char resolved[FILE_PATH_CAPACITY];
    const int discTrack = kCDTrackMap[track];
    if (gMusicSource == SOUND_MUSIC_SOURCE_CD && !CdRoot().empty() && discTrack >= 0) {
        const std::string root = FileRoot();
        FileSetRoot(CdRoot().c_str());
        std::snprintf(name, sizeof(name), "%s%02d-AudioTrack %02d.ogg", gTracksPath, discTrack, discTrack);
        const bool found = FileResolve(name, FILE_OPEN_READ, resolved, sizeof(resolved));
        FileSetRoot(root.c_str());
        if (found)
            return resolved;
    }
    for (const char* extension : {"ogg", "62s", "82m"}) {
        std::snprintf(name, sizeof(name), "%sHeroes%02d.%s", gSoundPath, track, extension);
        if (FileResolve(name, FILE_OPEN_READ, resolved, sizeof(resolved)))
            return resolved;
    }
    return {};
}

bool IsBattleMusic(int track) {
    return (track >= MUSIC_TRACK_BATTLE_FIRST && track <= MUSIC_TRACK_BATTLE_LAST)
        || track == MUSIC_TRACK_BATTLE_4;
}

void RememberPosition() {
    if (gCurrentTrack < 0)
        return;
    gMusicPositions[gCurrentTrack] =
        ShouldRepeatMusic(gCurrentTrack) ? platform::MusicPosition() : 0.0;
}

}  // namespace

// ---------------------------------------------------------------- device

bool InitAudio() {
    gAudioReady = platform::OpenAudio();
    return true;
}

void ShutdownAudio() {
    StopAllAudio();
    gAudioReady = false;
}

float VolumeLevel(int level) {
    if (level < 0 || level > 10)
        return 0.0f;
    return kVolumeLevels[level];
}

float GetEffectsVolume() {
    return gEffectsVolume;
}

float ScaleSampleVolume(int volume) {
    return GetEffectsVolume() * static_cast<float>(volume) / 127.0f;
}

float GetMusicVolume() {
    return gMusicVolume;
}

void StopAllAudio() {
    StopAllSamples();
    StopMusic();
}

void SetEffectsVolume(int level) {
    gEffectsVolume = VolumeLevel(level);
    UpdateAllSampleVolumes();
}

void SetMusicVolume(int level) {
    gMusicVolume = VolumeLevel(level);
    UpdateMusicVolume();
}

void SetVolumes(int effects, int music) {
    SetEffectsVolume(effects);
    SetMusicVolume(music);
}

// ---------------------------------------------------------------- music

bool ShouldRepeatMusic(int track) {
    if (track < MUSIC_TRACK_TERRAIN_END
        || IsBattleMusic(track) || track == MUSIC_TRACK_CONGRATULATIONS
        || track == MUSIC_TRACK_TAVERN || track == MUSIC_TRACK_MAIN_MENU
        || track == MUSIC_TRACK_AI_TURN
        || (track >= MUSIC_TRACK_TOWN_FIRST && track <= MUSIC_TRACK_TOWN_LAST))
        return true;
    return false;
}

void PlayMusic(int track) {
    if (!gAudioReady || MusicSuspended())
        return;
    const int tempo = IsBattleMusic(track) ? CombatAudioTempo() : 3;
    const bool background = IsBattleMusic(track)
                            || (track >= MUSIC_TRACK_TOWN_FIRST && track <= MUSIC_TRACK_TOWN_LAST);
    if (track == gCurrentTrack && tempo == gCurrentMusicTempo) {
        if (!platform::MusicPlaying() && ValidTrack(track))
            platform::PlayMusic(TrackPath(track).c_str(), ShouldRepeatMusic(track),
                                gMusicPositions[track], GetMusicVolume(), tempo, background);
        return;
    }
    if (!ValidTrack(track)) {
        StopMusic();
        return;
    }
    std::string path = TrackPath(track);
    RememberPosition();
    if (path.empty()
        || !platform::PlayMusic(path.c_str(), ShouldRepeatMusic(track), gMusicPositions[track],
                                GetMusicVolume(), tempo, background)) {
        platform::Log("cannot play music track %d (%s)", track,
                      path.empty() ? "no file" : path.c_str());
        platform::StopMusic();
        gCurrentTrack = -1;
        return;
    }
    gCurrentTrack = track;
    gCurrentMusicTempo = tempo;
}

void RefreshBattleMusicTempo() {
    if (IsBattleMusic(gCurrentTrack))
        PlayMusic(gCurrentTrack);
}

int GetCurrentTrack() {
    return gCurrentTrack;
}

void StopMusic() {
    if (MusicSuspended())
        return;
    RememberPosition();
    platform::StopMusic();
    gCurrentTrack = -1;
}

void UpdateMusicVolume() {
    if (MusicSuspended())
        return;
    platform::SetMusicVolume(GetMusicVolume());
}

void SetMusicSource(int source) {
    platform::StopMusic();
    for (double& position : gMusicPositions)
        position = 0;
    gMusicSource = source;
    if (gCurrentTrack >= 0) {
        int oldTrack = gCurrentTrack;
        gCurrentTrack = -1;
        PlayMusic(oldTrack);
    }
}

bool MusicPlaying() {
    return platform::MusicPlaying();
}

void SuspendMusic() {
    ++gMusicSuspensions;
}

void ResumeMusic() {
    --gMusicSuspensions;
}

bool MusicSuspended() {
    return gMusicSuspensions > 0;
}

// ---------------------------------------------------------------- samples

// Waveform similarity overlap-add for the original unsigned 8-bit mono intro.
// Keep the original sample rate and align overlapping chunks before blending.
static std::vector<u8> CompressMonoTempo(const u8* input, int length, int rate, int tempo) {
    const int overlap = rate * 15 / 1000;
    const int search = rate * 8 / 1000;
    const int outputLength = static_cast<int>(static_cast<i64>(length) * 3 / tempo);
    if (overlap < 2 || outputLength < overlap * 2)
        return {};
    std::vector<u8> output(input, input + outputLength);
    for (int out = overlap; out < outputLength; out += overlap) {
        const int blend = std::min(overlap, outputLength - out);
        const int chunk = std::min(overlap * 2, outputLength - out);
        const int expected = std::min(static_cast<int>(static_cast<i64>(out) * tempo / 3), length - chunk);
        int best = expected;
        double bestScore = -1.0;
        for (int in = std::max(0, expected - search); in <= std::min(length - chunk, expected + search); ++in) {
            int dot = 0, leftEnergy = 0, rightEnergy = 0;
            for (int j = 0; j < blend; j += 2) {
                const int left = output[out + j] - 128;
                const int right = input[in + j] - 128;
                dot += left * right;
                leftEnergy += left * left;
                rightEnergy += right * right;
            }
            if (leftEnergy == 0 || rightEnergy == 0)
                continue;
            const double similarity = dot / std::sqrt(static_cast<double>(leftEnergy) * rightEnergy);
            const double score = similarity - 0.01 * std::abs(in - expected) / std::max(1, search);
            if (score > bestScore) {
                best = in;
                bestScore = score;
            }
        }
        for (int j = 0; j < blend; ++j)
            output[out + j] = (output[out + j] * (blend - j) + input[best + j] * j) / blend;
        std::copy(input + best + blend, input + best + chunk, output.begin() + out + blend);
    }
    return output;
}

static void PlaySampleWithTempo(sample* resource, int tempo) {
    if (!gAudioReady || SamplesSuspended() || resource == NULL)
        return;
    auto found = gVoices.find(resource);
    if (found != gVoices.end()) {
        if (platform::VoicePlaying(found->second))
            return;
        platform::StopVoice(found->second);
        gVoices.erase(found);
    }
    const SamplePlaybackData& data = resource->m_playbackData;
    if (data.data == NULL || data.size <= 0)
        return;
    std::vector<u8> shortened;
    if (tempo > 3 && !data.stereo && !data.sampleFormat)
        shortened = CompressMonoTempo(reinterpret_cast<const u8*>(data.data), data.size, data.sampleRate, tempo);
    const void* frames = shortened.empty() ? static_cast<const void*>(data.data) : shortened.data();
    const size_t bytes = shortened.empty() ? static_cast<size_t>(data.size) : shortened.size();
    platform::Voice voice = platform::PlaySample(
        frames, bytes, data.sampleRate, data.stereo != 0 ? 2 : 1,
        data.sampleFormat != 0 ? platform::SAMPLE_S16 : platform::SAMPLE_U8,
        ScaleSampleVolume(data.volume), data.repeat != 0);
    if (voice != platform::NO_VOICE)
        gVoices[resource] = voice;
}

void PlaySample(sample* resource) {
    PlaySampleWithTempo(resource, 3);
}

sample* LoadPlaySample(char* name) {
    sample* resource = gResourceManager->GetSample(name);
    PlaySample(resource);
    return resource;
}

sample* LoadPlaySampleAtTempo(char* name, int tempo) {
    sample* resource = gResourceManager->GetSample(name);
    PlaySampleWithTempo(resource, tempo);
    return resource;
}

void StopSample(sample* resource) {
    if (SamplesSuspended())
        return;
    auto found = gVoices.find(resource);
    if (found == gVoices.end())
        return;
    platform::StopVoice(found->second);
    gVoices.erase(found);
}

void UpdateSampleVolume(sample* resource) {
    if (SamplesSuspended())
        return;
    auto found = gVoices.find(resource);
    if (found != gVoices.end())
        platform::SetVoiceVolume(found->second, ScaleSampleVolume(resource->m_playbackData.volume));
}

bool SamplePlaying(sample* resource) {
    const auto found = gVoices.find(resource);
    return found != gVoices.end() && platform::VoicePlaying(found->second);
}

void WaitSample(sample* resource) {
    if (SamplesSuspended())
        return;
    while (SamplePlaying(resource))
        DelayMilli(10);
    StopSample(resource);
}

void StopAllSamples() {
    if (SamplesSuspended())
        return;
    for (auto& entry : gVoices)
        platform::StopVoice(entry.second);
    gVoices.clear();
}

void UpdateAllSampleVolumes() {
    if (SamplesSuspended())
        return;
    for (auto& entry : gVoices)
        platform::SetVoiceVolume(entry.second,
                                 ScaleSampleVolume(entry.first->m_playbackData.volume));
}

void SuspendSamples() {
    ++gSampleSuspensions;
}

void ResumeSamples() {
    --gSampleSuspensions;
}

bool SamplesSuspended() {
    return gSampleSuspensions > 0;
}

// ---------------------------------------------------------------- movies

void InitSmackSound() {
    gMovieSoundReady = platform::OpenAudio();
}

void ShutdownSmackSound() {
    gMovieSoundReady = false;
}

i32 SmackSoundReady() {
    return gMovieSoundReady;
}

void UseSmackSound(i32 volume) {
    gMovieVolume = static_cast<float>(volume) / 127.0f;
}

float SmackSoundVolume() {
    return gMovieSoundReady ? gMovieVolume : 0.0f;
}
