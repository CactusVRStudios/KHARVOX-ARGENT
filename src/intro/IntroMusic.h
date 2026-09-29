#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <deque>
#include "MusicBeat.h"
#include "../../third-party/pocketmod/pocketmod.h"
namespace kharvox::intro {
class ModDecoder {
    std::vector<unsigned char> data;
    pocketmod_context context{};
public:
    bool initialize();
    bool fill(float* output,int samples);
    int loops() { return pocketmod_loop_count(&context); }
};
class IntroMusic {
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master{};
    IXAudio2SourceVoice* voice{};
    ModDecoder decoder;
    MusicBeat detector;
    mutable std::mutex beatMutex;
    std::deque<uint64_t> beats;
    std::array<std::array<float,4096>,3> buffers{};
    std::atomic<bool> stopping{false};
    std::atomic<HRESULT> error{S_OK};
    std::thread worker;
    unsigned nextBuffer{};
    bool playing{};
    HRESULT submit();
public:
    IntroMusic()=default;
    ~IntroMusic();
    HRESULT initialize(const wchar_t* preferredDevice,float volume=.65f);
    HRESULT start();
    void stop();
    void shutdown();
    bool isPlaying() const { return playing; }
    HRESULT status() const { return error.load(); }
    uint64_t samplesPlayed() const;
    unsigned queuedBuffers() const;
    float beatAge() const;
    bool usedHeadsetEndpoint{};
};
}
