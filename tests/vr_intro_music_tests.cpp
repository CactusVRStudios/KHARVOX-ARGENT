#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <cstdint>
#include "../src/intro/IntroMusic.h"
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
    require(argc>=2,"output directory required");std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    kharvox::intro::ModDecoder decoder;require(decoder.initialize(),"embedded intro-radiation.mod failed to initialize");
    std::ofstream wav(output/"intro-radiation-preview.wav",std::ios::binary);
    auto u16=[&](uint16_t n){wav.write(reinterpret_cast<char*>(&n),2);};auto u32=[&](uint32_t n){wav.write(reinterpret_cast<char*>(&n),4);};
    constexpr int previewFrames=48000*10;wav.write("RIFF",4);u32(36+previewFrames*4);wav.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(48000*4);u16(4);u16(16);wav.write("data",4);u32(previewFrames*4);
    float buffer[2048]{};double power=0;float peak=0;int stereo=0,totalFrames=0,loops=0;
    kharvox::intro::MusicBeat detector;int beatCount=0;uint64_t lastBeat=0;
    while(totalFrames<48000*300){
        require(decoder.fill(buffer,2048),"MOD decoder stalled");
        detector.consume(buffer,2048,[&](uint64_t frame){require(lastBeat==0||frame-lastBeat>=8640,"beats too close");lastBeat=frame;++beatCount;});
        for(int i=0;i<2048;i++){
            require(std::isfinite(buffer[i]),"nonfinite PCM");power+=buffer[i]*buffer[i];peak=std::max(peak,fabsf(buffer[i]));
            if(totalFrames+i/2<previewFrames){int16_t sample=int16_t(buffer[i]*.65f*32767);wav.write(reinterpret_cast<char*>(&sample),2);}
        }
        for(int i=0;i<2048;i+=2)if(fabsf(buffer[i]-buffer[i+1])>.001f)++stereo;
        totalFrames+=1024;loops=decoder.loops();if(loops>=2)break;
    }
    require(beatCount>40&&beatCount<1000,"music attack detector is inactive or overactive");
    std::cout<<"Detected music attacks: "<<beatCount<<"\n";
    require(totalFrames>=previewFrames,"preview unexpectedly short");require(peak>.05f&&power>10&&stereo>100,"silent or invalid module audio");require(loops>=2,"original tune does not loop");
    if(argc>2){
        HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        {
            kharvox::intro::IntroMusic player;
            require(SUCCEEDED(player.initialize(nullptr,0.f)),"XAudio2 output could not initialize");
            require(SUCCEEDED(player.start())&&player.isPlaying(),"XAudio2 playback did not start");
            Sleep(250);require(player.samplesPlayed()>0&&SUCCEEDED(player.status()),"XAudio2 did not consume music buffers");
            player.stop();auto stoppedAt=player.samplesPlayed();Sleep(60);
            require(!player.isPlaying()&&player.samplesPlayed()==stoppedAt&&player.queuedBuffers()==0,"music survived stop/black handoff");
        }
        if(SUCCEEDED(com))CoUninitialize();
        std::cout<<"PASS: XAudio2 output consumed PCM and stopped/flushed before handoff (muted test)\n";
    }
    std::cout<<"PASS: embedded intro-radiation.mod, stereo PCM, peak="<<peak<<", loops="<<loops<<", duration="<<totalFrames/48000.f<<"s\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
