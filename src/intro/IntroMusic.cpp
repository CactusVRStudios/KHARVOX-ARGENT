#include "IntroMusic.h"
#include "IntroAssets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace kharvox::intro {
bool ModDecoder::initialize(){
    auto module=asset(102);if(!module.data||module.size<1084||module.size>1024*1024)return false;
    auto begin=static_cast<const unsigned char*>(module.data);data.assign(begin,begin+module.size);
    return pocketmod_init(&context,data.data(),int(data.size()),48000)!=0;
}
bool ModDecoder::fill(float* output,int samples){
    if(!output||samples<=0||samples%2)return false;
    int remaining=samples*int(sizeof(float));auto cursor=reinterpret_cast<unsigned char*>(output);
    while(remaining){int written=pocketmod_render(&context,cursor,remaining);if(written<=0||written>remaining||written%8)return false;cursor+=written;remaining-=written;}

    for(int i=0;i<samples;i++){if(!std::isfinite(output[i]))return false;output[i]=std::clamp(output[i],-1.f,1.f);}
    return true;
}
HRESULT IntroMusic::initialize(const wchar_t* preferredDevice,float volume){
    if(!decoder.initialize())return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    HRESULT result=XAudio2Create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR);if(FAILED(result))return result;
    if(preferredDevice&&*preferredDevice){
        result=engine->CreateMasteringVoice(&master,2,48000,0,preferredDevice,nullptr,AudioCategory_GameMedia);
        usedHeadsetEndpoint=SUCCEEDED(result);
    }
    if(!master){result=engine->CreateMasteringVoice(&master,2,48000,0,nullptr,nullptr,AudioCategory_GameMedia);if(FAILED(result))return result;}
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=32;format.nBlockAlign=8;format.nAvgBytesPerSec=48000*8;
    result=engine->CreateSourceVoice(&voice,&format,0,1.f);if(FAILED(result))return result;
    return voice->SetVolume(std::clamp(volume,0.f,1.f));
}
HRESULT IntroMusic::submit(){
    auto& pcm=buffers[nextBuffer];if(!decoder.fill(pcm.data(),int(pcm.size())))return E_FAIL;
    detector.consume(pcm.data(),int(pcm.size()),[this](uint64_t frame){
        std::lock_guard<std::mutex> lock(beatMutex);
        beats.push_back(frame);if(beats.size()>128)beats.pop_front();
    });
    XAUDIO2_BUFFER buffer{};buffer.AudioBytes=UINT32(pcm.size()*sizeof(float));buffer.pAudioData=reinterpret_cast<const BYTE*>(pcm.data());
    auto result=voice->SubmitSourceBuffer(&buffer);if(SUCCEEDED(result))nextBuffer=(nextBuffer+1)%buffers.size();return result;
}
HRESULT IntroMusic::start(){
    if(playing)return S_OK;if(!voice)return E_UNEXPECTED;
    for(unsigned i=0;i<buffers.size();i++){auto result=submit();if(FAILED(result))return result;}
    auto result=voice->Start();if(FAILED(result))return result;playing=true;stopping=false;
    worker=std::thread([this]{
        while(!stopping.load()){
            XAUDIO2_VOICE_STATE state{};voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if(state.BuffersQueued<buffers.size()){
                auto result=submit();if(FAILED(result)){error=result;break;}
            }else Sleep(3);
        }
    });
    return S_OK;
}
void IntroMusic::stop(){

    stopping=true;if(voice)voice->Stop();
    if(worker.joinable())worker.join();
    if(voice)voice->FlushSourceBuffers();playing=false;
}
uint64_t IntroMusic::samplesPlayed() const {
    XAUDIO2_VOICE_STATE state{};if(voice)voice->GetState(&state);return state.SamplesPlayed;
}
float IntroMusic::beatAge() const {
    const auto played=samplesPlayed();
    std::lock_guard<std::mutex> lock(beatMutex);
    for(auto i=beats.rbegin();i!=beats.rend();++i)if(*i<=played)return float(played-*i)/48000.f;
    return -1.f;
}
unsigned IntroMusic::queuedBuffers() const {
    XAUDIO2_VOICE_STATE state{};if(voice)voice->GetState(&state);return state.BuffersQueued;
}
void IntroMusic::shutdown(){stop();if(voice){voice->DestroyVoice();voice=nullptr;}if(master){master->DestroyVoice();master=nullptr;}engine.Reset();}
IntroMusic::~IntroMusic(){shutdown();}
}
