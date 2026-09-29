#pragma once
#ifdef __ANDROID__
#include <chrono>
#include <unistd.h>
#else
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
#include <openxr/openxr.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#ifndef __ANDROID__
#include "IntroAssets.h"
#endif

namespace kharvox::intro {
struct Dot { float x,y,z,size; unsigned color; unsigned line{}; };
struct Quad { float x,y,z,width,height; std::array<float,4> color; float mode; unsigned line{}; std::array<float,3> tangent{},bitangent{}; };
struct Rect { int x,y,width,height; };
inline constexpr unsigned copperBase=17,copperCount=32,footerColor=copperBase+copperCount,outlineColor=footerColor+1,starColor=outlineColor+1;
using Batches=std::array<std::vector<Rect>,starColor+1>;
inline XrVector3f rotate(XrQuaternionf q,XrVector3f v){
    XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
struct InputGate {
    bool presented{}, dismissed{};
    bool update(bool focused,bool down){
        if(!focused||!presented||dismissed)return false;

        if(down){dismissed=true;return true;}
        return false;
    }
};
struct Scene {
    static constexpr float logoDuration=6.5f;
    static constexpr float loopLogoDuration=4.f;
    static constexpr float cubeFadeStart=4.f,cubeFadeDuration=1.5f;
    static constexpr float logoDockScale=.42f,logoDockY=-1.81f,logoDockZ=-6.3f;
    static constexpr float logoSpinPeriod=3.5f,logoExitFade=1.2f;
    static constexpr float cubeGapSpeed=.0338f;
    static constexpr float halfSize=2.5f;
    static constexpr float starHalfWidth=4.8f, starHalfHeight=3.4f;
    static constexpr float frontZ=-6.5f, backZ=2.5f;
    static constexpr float textZ=frontZ+.015f;
    static constexpr float lineDelay=.30f,revealSeconds=.35f,holdSeconds=8.f;
    static constexpr float firstBlockDelay=.5f;
    static constexpr std::string_view scrollerText=
        "GREETINGS:    FLAT2VR DISCORD, GAMERTAG VR, LUNCHANDVR, BEARDO BENJO, VINCE CRUSTY, ERIC PROVENCHER, CABALISTIC, PRAYDOG, ELLIOTTTATE, PUREDARK, GET-RICH, RUSTY GERE, GINGASVR, CREMENTIF, ASHOK, VRIFIED GAMES, GANJJ_, DR. BEEF, BAGGYG, BUMMSER - TEAM BEEF, TALEMANN, FEWERWRONG, TINYBLACKDOG, THEFREEMIKE, NOTGODLIKE, MR-N1CE, MO FUN VR, CACTUS COWBOY DISCORD - YOU GUYS ROCK!       JOHN CARMACK, JOHN ROMERO, NATHIE, FLAT2VR STUDIOS, GALAGHAN, PARADISE DECAY, STOCKIVR, MICHEL, PURE, SHANE, SBSCE, HELLCAT, LGZAKX, NICI, TREIBER, DATA, MIKU, MANTAXMOBILE, MRSURVIV0R 8HITMAN2, BMFVR, EMVIERDEH, ID SOFTWARE, VOODOO VR";
    static constexpr int scrollCell=32;
    static constexpr float scrollPixel=.00875f,scrollStep=scrollCell*scrollPixel;
    static constexpr float scrollDuration=4.05f+(11.95f+float(scrollerText.size()-1)*scrollStep)/1.404f+3.f;
    static constexpr unsigned blockCount=7;
    static const std::array<std::vector<std::wstring>,blockCount>& blocks(){
        static const std::array<std::vector<std::wstring>,blockCount> value{{
            {L"THE KHARVOX PROJECT IS BACK",L"BRINGING YOU ANOTHER",L"INSANE VR MOD"},
            {L"CACTUS AND NABELO",L"PRESENT",L"",L"KHARVOX:ARGENT",L"",L"A DOOM ETERNAL VR CONVERSION"},
            {L"SYSTEM REQUIREMENTS",L"",L"PROCESSOR (CPU): 486DX4 AT 66 MHZ OR HIGHER",L"MEMORY (RAM): 8 MB",L"HARD DISK: 30-50 MB",L"SOUND BLASTER",L"",L"JUST KIDDING, YOU NEED REAL BEEF HERE!"},
            {L"VR IS DEAD?",L"",L"FUCK NO, WE'RE ON FIRE!"},
            {L"NOT YOUR AVERAGE AI SLOP MOD",L"",L"CODE: CACTUS",L"VR HANDS: DISHLAN",L"DEBUGGING: NABELO",L"MUSIC: DEZECRATOR"},
            {L"SPECIAL THANKS TO OUR TESTERS",L"",L"SHOUTOUTS AND GREETINGS",L"TO OUR FRIENDS AND",L"THE FLAT2VR MODDING SCENE"},
            {L"EXCEPT FOR THOSE WHO",L"HIDE THEIR MODS",L"BEHIND PAYWALLS",L"",L"GET FUCKED!"}
        }};return value;
    }
    static unsigned lineCount(unsigned block){unsigned count=0;for(const auto& line:blocks()[block])if(!line.empty())++count;return count;}
    static float revealEnd(unsigned block){return (block?1.1f:1.3f)+(block?0.f:firstBlockDelay);}
    static float blockDuration(unsigned block){
        unsigned letters=0;for(const auto& line:blocks()[block])letters+=unsigned(line.size());
        return std::max(9.5f,8.3f+float(letters)*.0425f);
    }
    static float blockStart(unsigned block){float start=0;for(unsigned i=0;i<block;i++)start+=blockDuration(i);return start;}
    struct Page { unsigned block; float age; unsigned cycle; };
    static Page page(float time){
        if(time<logoDuration)return {blockCount+1,std::max(0.f,time),0};
        const float textDuration=blockStart(blockCount),duration=textDuration+scrollDuration+loopLogoDuration;
        const float positive=std::max(0.f,time-logoDuration);
        float age=std::fmod(positive,duration);unsigned block=0;
        if(age>=textDuration+scrollDuration)return {blockCount+2,age-textDuration-scrollDuration,unsigned(positive/duration)};
        if(age>=textDuration)return {blockCount,age-textDuration,unsigned(positive/duration)};
        while(block+1<blockCount&&age>=blockDuration(block)){age-=blockDuration(block);++block;}
        return {block,age,unsigned(positive/duration)};
    }
    static unsigned blockIndex(float time){return page(time).block;}
    static float lineProgress(unsigned block,unsigned line,float age){
        unsigned order=0;for(unsigned i=0;i<line;i++)if(!blocks()[block][i].empty())++order;
        return std::clamp((age-order*lineDelay)/revealSeconds,0.f,1.f);
    }
#ifdef __ANDROID__
    uint32_t effectSeed=uint32_t(std::chrono::steady_clock::now().time_since_epoch().count())^uint32_t(getpid());
#else
    uint32_t effectSeed=uint32_t(GetTickCount64())^GetCurrentProcessId();
#endif
    unsigned revealEffect(Page current)const{
        uint32_t hash=effectSeed+(current.cycle*blockCount+current.block)*0x9e3779b9u;
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
        return hash%3;
    }
    static unsigned copperBand(float x,float time){
        const float wave=x*1.25f-time*1.2f;
        return copperBase+unsigned((wave-std::floor(wave))*copperCount)%copperCount;
    }
    XrVector3f anchor{};float yaw{};bool anchored{};
    float eyeLocalY=-.85f;
    std::array<std::vector<Dot>,blockCount> textBlocks;
    std::vector<Dot> footer;
    std::array<std::vector<Quad>,blockCount> textQuads;
    std::vector<Quad> footerQuads;
    std::vector<Quad> scrollQuads;
    std::vector<size_t> scrollLetterOffsets;
    std::vector<Quad> logoFaces;
    bool makeLogo(const unsigned char* rgba,unsigned width,unsigned height){
        if(!rgba||width!=344||height!=309)return false;
        constexpr unsigned step=2;
        const unsigned cols=(width+step-1)/step,rows=(height+step-1)/step;
        std::vector<unsigned char> mask(cols*rows);
        for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<cols;x++){
            const unsigned px=std::min(width-1,x*step+1),py=std::min(height-1,y*step+1);
            const auto* p=rgba+(py*width+px)*4;
            mask[y*cols+x]=p[0]>140&&p[0]>p[1]*2&&p[0]>p[2]*2&&p[3]>80;
        }
        auto filled=[&](int x,int y){return x>=0&&y>=0&&x<int(cols)&&y<int(rows)&&mask[y*cols+x];};
        logoFaces.clear();logoFaces.reserve(cols*rows);
        const float scale=1.75f/float(width),cell=scale*step,depth=.10f;
        const std::array<float,4> front={248.f/255.f,57.f/255.f,0.f,1.f},back={.39f,.09f,0.f,1.f};
        const std::array<float,4> side={.66f,.15f,0.f,1.f},top={.80f,.18f,0.f,1.f};
        auto face=[&](float x,float y,float z,float w,float h,std::array<float,4> color,
                      std::array<float,3> tangent,std::array<float,3> bitangent){
            logoFaces.push_back({x,y,z,w,h,color,3.f,0,tangent,bitangent});
        };
        for(unsigned y=0;y<rows;y++){
            const float py=(float(height)*.5f-float(y*step)-1.f)*scale;
            for(unsigned x=0;x<cols;){
                if(!filled(int(x),int(y))){++x;continue;}
                const unsigned start=x;while(x<cols&&filled(int(x),int(y)))++x;
                const float px=(float(start+x)*float(step)*.5f-float(width)*.5f)*scale;
                const float span=float(x-start)*cell;
                face(px,py,depth*.5f,span,cell,front,{1,0,0},{0,1,0});
                face(px,py,-depth*.5f,span,cell,back,{1,0,0},{0,1,0});
            }
        }
        for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<cols;x++)if(filled(int(x),int(y))){
            const float px=(float(x*step)+1.f-float(width)*.5f)*scale;
            const float py=(float(height)*.5f-float(y*step)-1.f)*scale;
            if(!filled(int(x)-1,int(y)))face(px-cell*.5f,py,0.f,depth,cell,side,{0,0,1},{0,1,0});
            if(!filled(int(x)+1,int(y)))face(px+cell*.5f,py,0.f,depth,cell,side,{0,0,1},{0,1,0});
            if(!filled(int(x),int(y)-1))face(px,py+cell*.5f,0.f,cell,depth,top,{1,0,0},{0,0,1});
            if(!filled(int(x),int(y)+1))face(px,py-cell*.5f,0.f,cell,depth,side,{1,0,0},{0,0,1});
        }
        return logoFaces.size()>1000;
    }
    std::array<std::vector<float>,blockCount> textLineY;
    struct LetterInfo { float x{},y{},side{}; };
    std::array<std::vector<LetterInfo>,blockCount> letters;
    void setAnchor(const std::array<XrView,2>& eyes,float floorY=NAN){
        anchor={(eyes[0].pose.position.x+eyes[1].pose.position.x)*.5f,(eyes[0].pose.position.y+eyes[1].pose.position.y)*.5f,(eyes[0].pose.position.z+eyes[1].pose.position.z)*.5f};
        const float headY=anchor.y;
        anchor.y=(std::isfinite(floorY)?floorY:headY-1.65f)+halfSize;
        const float nextEyeY=headY-anchor.y;
        for(auto& dot:footer)dot.y+=nextEyeY-eyeLocalY;
        eyeLocalY=nextEyeY;
        auto f=rotate(eyes[0].pose.orientation,{0,0,-1});yaw=atan2f(-f.x,-f.z);anchored=true;
    }
#ifdef __ANDROID__
    bool makeText();
#else
    bool makeText(){
        for(auto& dots:textBlocks)dots.clear();footer.clear();
        for(auto& quads:textQuads)quads.clear();footerQuads.clear();scrollQuads.clear();scrollLetterOffsets.clear();
        for(auto& lines:textLineY)lines.clear();
        for(auto& blockLetters:letters)blockLetters.clear();
        auto png=asset(105);if(!png.data)return false;
        Microsoft::WRL::ComPtr<IWICImagingFactory> imaging;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging))))return false;
        Microsoft::WRL::ComPtr<IWICStream> stream;if(FAILED(imaging->CreateStream(&stream)))return false;
        if(FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(png.data)),DWORD(png.size))))return false;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;if(FAILED(imaging->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return false;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return false;
        UINT width{},height{};if(FAILED(frame->GetSize(&width,&height))||width!=320||height!=192)return false;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;if(FAILED(imaging->CreateFormatConverter(&converter)))return false;
        if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        std::vector<unsigned char> rgba(width*height*4);
        if(FAILED(converter->CopyPixels(nullptr,width*4,UINT(rgba.size()),rgba.data())))return false;

        auto scrollPng=asset(106);if(!scrollPng.data)return false;
        Microsoft::WRL::ComPtr<IWICStream> scrollStream;if(FAILED(imaging->CreateStream(&scrollStream)))return false;
        if(FAILED(scrollStream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(scrollPng.data)),DWORD(scrollPng.size))))return false;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> scrollDecoder;if(FAILED(imaging->CreateDecoderFromStream(scrollStream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&scrollDecoder)))return false;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> scrollFrame;if(FAILED(scrollDecoder->GetFrame(0,&scrollFrame)))return false;
        UINT scrollWidth{},scrollHeight{};if(FAILED(scrollFrame->GetSize(&scrollWidth,&scrollHeight))||scrollWidth!=320||scrollHeight!=192)return false;
        Microsoft::WRL::ComPtr<IWICFormatConverter> scrollConverter;if(FAILED(imaging->CreateFormatConverter(&scrollConverter)))return false;
        if(FAILED(scrollConverter->Initialize(scrollFrame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        std::vector<unsigned char> scrollRgba(scrollWidth*scrollHeight*4);
        if(FAILED(scrollConverter->CopyPixels(nullptr,scrollWidth*4,UINT(scrollRgba.size()),scrollRgba.data())))return false;
        auto logoPng=asset(107);if(!logoPng.data)return false;
        Microsoft::WRL::ComPtr<IWICStream> logoStream;if(FAILED(imaging->CreateStream(&logoStream)))return false;
        if(FAILED(logoStream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<void*>(logoPng.data)),DWORD(logoPng.size))))return false;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> logoDecoder;if(FAILED(imaging->CreateDecoderFromStream(logoStream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&logoDecoder)))return false;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> logoFrame;if(FAILED(logoDecoder->GetFrame(0,&logoFrame)))return false;
        UINT logoWidth{},logoHeight{};if(FAILED(logoFrame->GetSize(&logoWidth,&logoHeight))||logoWidth!=344||logoHeight!=309)return false;
        Microsoft::WRL::ComPtr<IWICFormatConverter> logoConverter;if(FAILED(imaging->CreateFormatConverter(&logoConverter)))return false;
        if(FAILED(logoConverter->Initialize(logoFrame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
        std::vector<unsigned char> logoRgba(logoWidth*logoHeight*4);
        if(FAILED(logoConverter->CopyPixels(nullptr,logoWidth*4,UINT(logoRgba.size()),logoRgba.data()))||!makeLogo(logoRgba.data(),logoWidth,logoHeight))return false;

        constexpr float pixel=.00525f;
        constexpr int cell=32;
        auto glyphIndex=[](char ch)->int{
            const int index=int(static_cast<unsigned char>(ch))-32;
            return index>=0&&index<60?index:-1;
        };
        auto narrow=[](const std::wstring& wide){
            std::string out;for(wchar_t ch:wide)out.push_back(ch>=32&&ch<127?char(ch):' ');return out;
        };
        for(unsigned block=0;block<blockCount;block++){
            auto& quads=textQuads[block];quads.reserve(1024);
            std::vector<std::string> lines;for(const auto& line:blocks()[block])lines.push_back(narrow(line));
            size_t longest=0;for(const auto& line:lines)longest=std::max(longest,line.size());
            const float pixel=std::min(.00525f,4.65f/(float(std::max<size_t>(1,longest))*cell));
            const float lineStep=std::min(.245f,2.5f/float(std::max<size_t>(1,lines.size()-1)));
            const float firstY=(float(lines.size())-1.f)*lineStep*.5f;
            for(size_t lineIndex=0;lineIndex<lines.size();lineIndex++){
                const auto& text=lines[lineIndex];if(text.empty())continue;
                const float textWidth=float(text.size()*cell)*pixel;
                const float lineY=firstY-float(lineIndex)*lineStep;
                for(size_t letter=0;letter<text.size();letter++){
                    const int glyph=glyphIndex(text[letter]);if(glyph<0)continue;
                    const unsigned letterId=unsigned(letters[block].size());
                    const float originX=-textWidth*.5f+float(letter*cell)*pixel;
                    const float centerX=originX+cell*.5f*pixel;
                    const float side=(letterId%2)?1.f:-1.f;
                    letters[block].push_back({centerX,lineY,side});
                    const int gx=(glyph%10)*cell,gy=(glyph/10)*cell;
                    for(int y=0;y<cell;y++)for(int x=0;x<cell;){
                        const int offset=((gy+y)*int(width)+gx+x)*4;
                        const bool ink=rgba[offset+3]>16&&(rgba[offset]||rgba[offset+1]||rgba[offset+2]);
                        if(!ink){++x;continue;}
                        const int start=x;const std::array<float,4> color{1.f,1.f,1.f,1.f};
                        while(x<cell){
                            const int next=((gy+y)*int(width)+gx+x)*4;
                            const bool same=rgba[next+3]>16&&(rgba[next]||rgba[next+1]||rgba[next+2]);
                            if(!same)break;++x;
                        }
                        quads.push_back({originX+(start+(x-start)*.5f)*pixel,lineY+(cell*.5f-y-.5f)*pixel,textZ,
                            (x-start)*pixel,pixel,color,1.f,letterId});
                    }
                }
            }
            textLineY[block].resize(lines.size());
            for(size_t i=0;i<lines.size();i++)textLineY[block][i]=firstY-float(i)*lineStep;
        }
        constexpr std::string_view footerText="PRESS ANY BUTTON TO LAUNCH DOOM IN VR";
        constexpr float footerPixel=.0036f;
        const float footerStart=-float(footerText.size()*cell)*footerPixel*.5f;
        for(size_t letter=0;letter<footerText.size();++letter){
            const int glyph=glyphIndex(footerText[letter]);if(glyph<0)continue;
            const int gx=(glyph%10)*cell,gy=(glyph/10)*cell;
            for(int y=0;y<cell;y++)for(int x=0;x<cell;){
                const int offset=((gy+y)*int(width)+gx+x)*4;
                const bool ink=rgba[offset+3]>16&&(rgba[offset]||rgba[offset+1]||rgba[offset+2]);
                if(!ink){++x;continue;}
                const int start=x;
                while(x<cell){const int next=((gy+y)*int(width)+gx+x)*4;
                    if(!(rgba[next+3]>16&&(rgba[next]||rgba[next+1]||rgba[next+2])))break;++x;}
                footerQuads.push_back({footerStart+(float(letter*cell)+float(start+x)*.5f)*footerPixel,
                    -2.31f+(cell*.5f-y-.5f)*footerPixel,textZ,(x-start)*footerPixel,footerPixel,
                    {1.f,1.f,1.f,1.f},4.f,unsigned(letter)});
            }
        }
        const std::string_view scrollText=scrollerText;
        constexpr int scrollColumns=10;
        scrollQuads.reserve(scrollText.size()*48);
        scrollLetterOffsets.resize(scrollText.size()+1);
        for(size_t letter=0;letter<scrollText.size();letter++){
            scrollLetterOffsets[letter]=scrollQuads.size();
            const bool underscore=scrollText[letter]=='_';
            const int glyph=int(static_cast<unsigned char>(underscore?'-':scrollText[letter]))-32;
            if(glyph<0||glyph>=60)continue;
            const int gx=(glyph%scrollColumns)*scrollCell,gy=(glyph/scrollColumns)*scrollCell;
            const float originX=float(letter*scrollCell)*scrollPixel;
            for(int y=0;y<scrollCell;y++)for(int x=0;x<scrollCell;){
                const int offset=((gy+y)*int(scrollWidth)+gx+x)*4;
                const bool ink=scrollRgba[offset+3]>16&&(scrollRgba[offset]||scrollRgba[offset+1]||scrollRgba[offset+2]);
                if(!ink){++x;continue;}
                const int start=x;std::array<float,4> color{};
                for(int k=0;k<3;k++)color[k]=scrollRgba[offset+k]/255.f;color[3]=1.f;
                while(x<scrollCell){
                    const int next=((gy+y)*int(scrollWidth)+gx+x)*4;
                    const bool same=scrollRgba[next+3]>16&&(scrollRgba[next]||scrollRgba[next+1]||scrollRgba[next+2])
                        &&std::abs(int(scrollRgba[next])-int(color[0]*255.f))<8
                        &&std::abs(int(scrollRgba[next+1])-int(color[1]*255.f))<8
                        &&std::abs(int(scrollRgba[next+2])-int(color[2]*255.f))<8;
                    if(!same)break;++x;
                }
                scrollQuads.push_back({originX+(start+(x-start)*.5f)*scrollPixel,(scrollCell*.5f-y-.5f)*scrollPixel-(underscore?.08f:0.f),-2.7f,
                    (x-start)*scrollPixel,scrollPixel,color,0.f,unsigned(letter)});
            }
        }
        scrollLetterOffsets.back()=scrollQuads.size();
        return std::all_of(textQuads.begin(),textQuads.end(),[](const auto& block){return !block.empty();})
            &&!footerQuads.empty()&&!scrollQuads.empty();
    }
#endif
    std::vector<Dot> points(float t,float beatAge=-1.f)const{
        (void)beatAge;
        const auto current=page(t);
        auto cubeEdges=[&](float burst,float gaps){
            std::vector<Dot> edges;edges.reserve(12*128);
            constexpr int steps=128;
            for(int axis=0;axis<3;axis++)for(float a:{-halfSize,halfSize})for(float b:{-halfSize,halfSize})for(int i=0;i<steps;i++){
                const float u=-halfSize+2.f*halfSize*float(i)/(steps-1);
                float x{},y{},z{};
                if(axis==0){x=u;y=a;z=b<0?frontZ:backZ;}
                if(axis==1){x=a;y=u;z=b<0?frontZ:backZ;}
                if(axis==2){x=a;y=b;z=frontZ+(backZ-frontZ)*float(i)/(steps-1);}
                const float edgeId=float(axis*4)+(a>0?1.f:0.f)+(b>0?2.f:0.f);
                const float lineFade=std::clamp((t-firstBlockDelay)/2.0f,0.f,1.f);
                float glow=.78f+.22f*lineFade+.18f*burst;
                const float path=(edgeId+float(i)/(steps-1))/12.f;
                const float phase=std::fmod(t*cubeGapSpeed,1.f);
                const float distance=std::fabs(std::fmod(path-phase+1.f, .25f)-.125f)*12.f;
                const float gap=1.f-std::clamp((distance-.34f)/.12f,0.f,1.f);
                glow+=.32f*std::exp(-std::pow((distance-.53f)/.11f,2.f));
                if(burst>0.f){
                    const float seed=std::sin(float(i+axis*53+(a>0?7:17)+(b>0?29:41))*12.9898f)*43758.547f;
                    const float rx=(seed-std::floor(seed))*2.f-1.f;
                    const float ry=std::sin(seed)*.75f;
                    const float rz=std::cos(seed*1.37f)*1.1f;
                    const float fly=burst*burst*(3.f-2.f*burst);
                    x+=rx*fly*3.6f;y+=ry*fly*2.2f;z+=rz*fly*4.0f;
                }
                const float fragmentScale=1.f-burst*.88f;
                const float visible=1.f-gap*gaps;
                const float size=visible<=0.f?0.f:(.0015f+(.026f+.024f*glow)*fragmentScale*visible);
                edges.push_back({x,y,z,size,starColor});
            }
            return edges;
        };
        if(current.block==blockCount+2)return cubeEdges(0.f,0.f);
        if(current.block==blockCount+1){
            if(current.age<=cubeFadeStart)return {};
            return cubeEdges(0.f,0.f);
        }
        if(current.block<blockCount)return cubeEdges(0.f,current.block==0?std::clamp(current.age/.8f,0.f,1.f):1.f);
        constexpr int starCount=2300;
        std::vector<Dot> dots;dots.reserve(starCount);
        constexpr float depth=backZ-frontZ;
        const float travel=std::fmod(std::max(0.f,t)*1.65f,depth);
        const float driftX=sinf(t*.21f)*.24f,driftY=cosf(t*.17f)*.16f;
        auto random01=[](uint32_t seed){
            seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;
            return float(seed&0x00ffffffu)/float(0x01000000u);
        };
        const float scatter=std::clamp((current.age-.55f)/2.5f,0.f,1.f);
        const float scatterEase=scatter*scatter*(3.f-2.f*scatter);
        const float reassemble=std::clamp((3.0f-(scrollDuration-current.age))/3.0f,0.f,1.f);
        const float assembleEase=reassemble*reassemble*(3.f-2.f*reassemble);
        const float starVisibility=scatterEase*(1.f-assembleEase);
        for(uint32_t i=0;i<starCount;i++){
            const float rx=random01(i*3u+11u)*2.f-1.f;
            const float ry=random01(i*3u+29u)*2.f-1.f;
            const float rz=random01(i*3u+47u);
            float z=frontZ+std::fmod(rz*depth+travel,depth);
            const float progress=(z-frontZ)/depth;
            const float spreadX=1.65f+progress*5.2f,spreadY=1.25f+progress*3.9f;
            float x=rx*spreadX+driftX*progress;
            float y=ry*spreadY+driftY*progress;
            if(std::fabs(x)>starHalfWidth||std::fabs(y)>starHalfHeight)continue;
            const float twinkle=.76f+.24f*sinf(t*(2.1f+random01(i+91u)*3.4f)+random01(i+173u)*6.2831853f);
            const float size=(.0035f+progress*.012f)*twinkle*starVisibility;
            dots.push_back({x,y,z,size,starColor});
        }
        if(scatter<1.f||reassemble>0.f){
            const float burst=scatterEase*(1.f-assembleEase);
            auto edges=cubeEdges(burst,1.f-scatterEase);
            const float edgeVisibility=1.f-scatterEase+assembleEase;
            for(auto& edge:edges)edge.size*=edgeVisibility;
            dots.insert(dots.end(),edges.begin(),edges.end());
        }
        return dots;
    }
    static float lineBounce(unsigned line,float beatAge){
        const float age=beatAge-float(line)*.012f;
        if(age<=0||age>=.3f)return 0;
        return .065f*std::sin(age/.3f*3.1415927f)*std::exp(-age*3.f);
    }
    Batches project(const XrView& eye,int w,int h,float t,float beatAge=-1.f)const{
        Batches batches;
        auto q=eye.pose.orientation;q.x=-q.x;q.y=-q.y;q.z=-q.z;
        const float l=tanf(eye.fov.angleLeft),r=tanf(eye.fov.angleRight),up=tanf(eye.fov.angleUp),down=tanf(eye.fov.angleDown);
        auto add=[&](Dot d){
            XrVector3f world{anchor.x+cosf(yaw)*d.x+sinf(yaw)*d.z,anchor.y+d.y,anchor.z-sinf(yaw)*d.x+cosf(yaw)*d.z};
            auto p=rotate(q,{world.x-eye.pose.position.x,world.y-eye.pose.position.y,world.z-eye.pose.position.z});if(p.z>=-.05f)return;
            float depth=-p.z,px=(p.x/depth-l)/(r-l)*w,py=(up-p.y/depth)/(up-down)*h;
            if(d.size<=0.f)return;
            int size=std::clamp(int(d.size/depth*w/(r-l)),1,96);
            if(!std::isfinite(px)||!std::isfinite(py)||px < -size||px>w+size||py < -size||py>h+size)return;
            int x0=std::clamp(int(px)-size/2,0,w),y0=std::clamp(int(py)-size/2,0,h),x1=std::clamp(int(px)+size/2+1,0,w),y1=std::clamp(int(py)+size/2+1,0,h);
            if(x1>x0&&y1>y0)batches[d.color].push_back({x0,y0,x1-x0,y1-y0});
        };
        for(auto d:points(t,beatAge))add(d);return batches;
    }
    std::vector<Quad> geometry(float time,float beatAge=-1.f)const{
        const auto current=page(time);
        std::vector<Quad> quads;quads.reserve(2600+(current.block<blockCount?textQuads[current.block].size():scrollQuads.size()));
        auto appendLogo=[&](float opacity){
            if(opacity<=0.f)return;
            const bool introLogo=current.block==blockCount+1;
            const float dock= introLogo?std::clamp((current.age-cubeFadeStart)/cubeFadeDuration,0.f,1.f):1.f;
            const float easedDock=dock*dock*(3.f-2.f*dock);
            const float scale=1.f+(logoDockScale-1.f)*easedDock;
            const float turn=introLogo&&current.age<=cubeFadeStart
                ?current.age/cubeFadeStart*6.2831853f
                :introLogo&&dock<1.f?6.2831853f
                :(time-cubeFadeStart-cubeFadeDuration)/logoSpinPeriod*6.2831853f;
            const float sn=std::sin(turn),cs=std::cos(turn);
            const float centerY=logoDockY*easedDock;
            const float centerZ=-3.2f+(logoDockZ+3.2f)*easedDock;
            const size_t first=quads.size();
            for(auto face:logoFaces){
                const float x=face.x*cs+face.z*sn;
                const float z=-face.x*sn+face.z*cs;
                const auto rotateBasis=[&](std::array<float,3>& basis){
                    const float bx=basis[0],bz=basis[2];basis[0]=bx*cs+bz*sn;basis[2]=-bx*sn+bz*cs;
                };
                rotateBasis(face.tangent);rotateBasis(face.bitangent);
                face.x=x*scale;face.y=centerY+face.y*scale;face.z=centerZ+z*scale;
                face.width*=scale;face.height*=scale;
                for(int channel=0;channel<3;channel++)face.color[channel]*=opacity;
                quads.push_back(face);
            }
            std::sort(quads.begin()+first,quads.end(),[](const Quad& a,const Quad& b){return a.z<b.z;});
        };
        auto appendFooter=[&](float opacity){
            if(opacity<=0.f)return;
            for(auto footer:footerQuads){
                const float beatOffset=std::fmod(float(footer.line)*.037f,.14f);
                const float letterBeat=beatAge-beatOffset;
                if(letterBeat>0.f&&letterBeat<.24f){
                    const float pulse=std::sin(letterBeat/.24f*3.1415927f)*std::exp(-letterBeat*3.f);
                    footer.y+=.14f*pulse;
                    const float grow=1.f+.12f*pulse;
                    footer.width*=grow;footer.height*=grow;
                }
                for(int channel=0;channel<3;channel++)footer.color[channel]*=opacity;
                quads.push_back(footer);
            }
        };
        if(current.block==blockCount+1||current.block==blockCount+2){
            const bool firstLogo=current.block==blockCount+1;
            const float cubeProgress=std::clamp((current.age-cubeFadeStart)/cubeFadeDuration,0.f,1.f);
            const float cubeOpacity=firstLogo?cubeProgress*cubeProgress*(3.f-2.f*cubeProgress):1.f;
            const float loopIn=std::clamp(current.age/.6f,0.f,1.f);
            const float logoOpacity=firstLogo?1.f:loopIn*loopIn*(3.f-2.f*loopIn);
            quads.reserve(logoFaces.size()+1536);
            appendLogo(logoOpacity);
            for(auto d:points(time,beatAge))if(d.size>0.f){
                auto tint=color(d.color);for(int channel=0;channel<3;channel++)tint[channel]*=cubeOpacity;
                quads.push_back({d.x,d.y,d.z,d.size,d.size,tint,2.f});
            }
            appendFooter(cubeOpacity);
            return quads;
        }
        for(auto d:points(time,beatAge))if(d.size>0.f)quads.push_back({d.x,d.y,d.z,d.size,d.size,color(d.color),2});
        if(current.block==blockCount){
            const float scrollAge=std::max(0.f,current.age-4.05f);
            const float speed=1.404f;
            const float distance=scrollAge*speed;
            const size_t firstLetter=size_t(std::clamp(std::floor((distance-11.95f)/scrollStep),0.f,float(scrollerText.size())));
            const size_t lastLetter=size_t(std::clamp(std::ceil(distance/scrollStep)+1.f,0.f,float(scrollerText.size())));
            for(size_t index=scrollLetterOffsets[firstLetter];index<scrollLetterOffsets[lastLetter];++index){
                auto quad=scrollQuads[index];
                const float letterBase=float(quad.line)*scrollCell*scrollPixel;
                const float letterX=starHalfWidth+.45f-scrollAge*speed+letterBase;
                const float grow=std::clamp((starHalfWidth+.45f-letterX)/.85f,0.f,1.f);
                if(grow<=0.f)continue;
                float scale=.08f+.92f*grow*grow*(3.f-2.f*grow);
                const float localX=quad.x-letterBase-scrollCell*.5f*scrollPixel;
                quad.x=letterX+localX*scale;
                quad.y=-.96f+(quad.y*.78f)*scale;
                quad.z=-3.2f;
                quad.width*=scale;quad.height*=scale;
                const float fall=std::clamp((-starHalfWidth-.55f-letterX)/1.35f,0.f,1.f);
                if(fall>=1.f)continue;
                if(fall>0.f){
                    const float eased=fall*fall*(3.f-2.f*fall);
                    const float spinSeed=std::sin(float(quad.line)*9.731f)*18137.17f;
                    const float spin=(spinSeed-std::floor(spinSeed))*2.f-1.f;
                    const float angle=spin*4.8f*eased;
                    const float shrink=1.f-eased;
                    const float center=letterX;
                    const float dx=quad.x-center,dy=quad.y+.96f;
                    const float cs=std::cos(angle)*shrink,sn=std::sin(angle)*shrink;
                    quad.x=center+dx*cs-dy*sn;
                    quad.y=-.96f+dx*sn+dy*cs-eased*1.6f;
                    quad.width*=shrink;quad.height*=shrink;
                    for(int k=0;k<3;k++)quad.color[k]*=shrink;
                }
                const float beatOffset=std::fmod(float(quad.line)*.037f,.14f);
                const float letterBeat=beatAge-beatOffset;
                if(letterBeat>0.f&&letterBeat<.24f){
                    const float pulse=std::sin(letterBeat/.24f*3.1415927f)*std::exp(-letterBeat*3.f);
                    quad.y+=.14f*pulse;
                    const float grow=1.f+.12f*pulse;
                    quad.width*=grow;quad.height*=grow;
                }
                quads.push_back(quad);
            }
            return quads;
        }
        const float fadeLeft=current.block==blockCount-1
            ?std::clamp((blockDuration(current.block)-current.age)/logoExitFade,0.f,1.f):1.f;
        appendLogo(fadeLeft*fadeLeft*(3.f-2.f*fadeLeft));
        appendFooter(1.f);
        const float duration=1.3f;
        for(auto quad:textQuads[current.block]){
            const auto& info=letters[current.block][quad.line];
            const float baseX=quad.x,baseY=quad.y,baseZ=quad.z;
            const float blockDelay=current.block==0?firstBlockDelay:0.f;
            float p=std::clamp((current.age-blockDelay-float(quad.line)*.0275f)/duration,0.f,1.f);
            p=p*p*(3.f-2.f*p);
            const float side=info.side;
            const float startX=side*(starHalfWidth+.85f);
            const float startY=std::sin(float(quad.line)*1.7f)*.95f;
            const float startZ=.85f+.18f*std::cos(float(quad.line)*1.1f);
            quad.x=startX+(baseX-startX)*p;
            quad.y=startY+(baseY-startY)*p+std::sin(p*3.1415927f)*.32f*side;
            quad.z=startZ+(baseZ-startZ)*p;

            const float fallStart=7.f+float(quad.line)*.0425f;
            float fall=std::clamp((current.age-fallStart)/.775f,0.f,1.f);
            if(fall>=1.f)continue;
            fall=fall*fall*(3.f-2.f*fall);
            if(fall>0.f){
                const float spinSeed=std::sin(float(quad.line)*12.9898f)*43758.547f;
                const float spin=(spinSeed-std::floor(spinSeed))*2.f-1.f;
                const float angle=spin*5.8f*fall;
                const float scale=std::max(.04f,1.f-fall);
                const float dx=quad.x-info.x,dy=quad.y-info.y;
                const float cs=std::cos(angle)*scale,sn=std::sin(angle)*scale;
                quad.x=info.x+dx*cs-dy*sn;
                quad.y=info.y+dx*sn+dy*cs-fall*fall*2.6f;
                quad.width*=scale;
                quad.height*=scale;
                const float fade=1.f-fall;
                for(int k=0;k<3;k++)quad.color[k]*=fade;
            }
            quads.push_back(quad);
        }
        (void)beatAge;
        return quads;
    }
    static std::array<float,4> color(unsigned c){
        if(c==footerColor)return {.85f,.85f,.92f,1};
        if(c==outlineColor)return {.32f,.34f,.48f,1};
        if(c==starColor)return {.92f,.94f,1.f,1};
        if(c>=copperBase){
            constexpr float neon[8][3]={{1,0,.55f},{.65f,0,1},{.08f,.2f,1},{0,1,1},
                {0,1,.35f},{.7f,1,0},{1,.85f,0},{1,.08f,.3f}};
            const float phase=float(c-copperBase)*8/copperCount;
            const int index=int(phase);const float mix=phase-index;
            std::array<float,4> result{0,0,0,1};
            for(int k=0;k<3;k++)result[k]=std::round((neon[index][k]*(1-mix)+neon[(index+1)%8][k]*mix)*31)/31;
            return result;
        }
        if(c==16)return {.55f,.55f,.55f,1};
        std::array<float,4> out{0,0,0,1};for(int k=0;k<3;k++)out[k]=.03f+.97f*std::max(0.f,cosf(c*6.2831853f/16-k*2.094395f));return out;
    }
};
}

