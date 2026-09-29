#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <chrono>
#include <cstring>
#include "../src/intro/CubeD3D11.h"
#include "../src/intro/DesktopPreview.h"
using Microsoft::WRL::ComPtr;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
    SetProcessDPIAware();require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM initialization failed");
    require(argc==2,"output directory required");std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    kharvox::intro::InputGate input;
    require(!input.update(true,true),"unpresented scene dismisses");input.presented=true;
    require(!input.update(false,true),"unfocused held input dismisses");
    require(input.update(true,true)&&input.dismissed,"held input must dismiss after presentation and focus");
    require(!input.update(true,true),"duplicate dismiss");
    kharvox::intro::InputGate fresh;fresh.presented=true;
    require(!fresh.update(true,false),"idle input dismisses");
    require(fresh.update(true,true),"fresh press does not dismiss");

    kharvox::intro::Scene scene;require(scene.makeText(),"asset bootstrap failed");
    std::ifstream fontFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"assets/intro/038_32.png",std::ios::binary);
    const std::string fontBytes(std::istreambuf_iterator<char>{fontFile},{});
    const auto embeddedFont=kharvox::intro::asset(106);
    require(!fontBytes.empty()&&embeddedFont.size==fontBytes.size()&&
        std::memcmp(embeddedFont.data,fontBytes.data(),fontBytes.size())==0,
        "scroller does not use the supplied 038 bitmap font");
    require(scene.scrollPixel>.0078f&&scene.scrollLetterOffsets.size()==scene.scrollerText.size()+1&&
        scene.scrollLetterOffsets.back()==scene.scrollQuads.size(),"upscaled scroller or letter index missing");
    std::array<XrView,2> eyes{};for(int e=0;e<2;e++){eyes[e].pose.orientation.w=1;eyes[e].pose.position.x=e?.032f:-.032f;eyes[e].fov={-.8f,.7f,.75f,-.7f};}scene.setAnchor(eyes);
    require(fabsf(scene.anchor.x)<.001f&&fabsf(scene.anchor.z)<.001f,"viewer not centred horizontally");
    require(fabsf(eyes[0].pose.position.y-scene.anchor.y)<2.5f,"viewer outside starfield volume vertically");
    require(scene.frontZ==-6.5f&&scene.backZ==2.5f,"starfield depth changed unexpectedly");
    require(std::abs(scene.cubeGapSpeed-.026f*1.3f)<1e-6f,"cube running lights are not 30 percent faster");
    require(scene.copperBand(0.f,0.f)!=scene.copperBand(0.f,.5f),"copper colors are not animated");

    auto logoFront=scene.geometry(.1f),logoSide=scene.geometry(.85f);
    require(scene.logoFaces.size()>1000&&logoFront.size()>1000&&logoSide.size()>1000,"extruded logo missing");
    bool hasSide=false;for(const auto& q:scene.logoFaces)if(fabsf(q.tangent[2])>.5f||fabsf(q.bitangent[2])>.5f)hasSide=true;
    require(hasSide,"logo side faces missing");
    float frontWidth=0,sideWidth=0;
    for(const auto& q:logoFront)frontWidth=std::max(frontWidth,fabsf(q.x));
    for(const auto& q:logoSide)sideWidth=std::max(sideWidth,fabsf(q.x));
    require(sideWidth<frontWidth*.8f,"logo does not rotate with depth");
    require(frontWidth<1.f,"logo was not reduced to half size");
    require(scene.points(3.99f).empty(),"cube appears before logo finishes rotating");
    auto logoAndCube=scene.geometry(4.75f),cubeComplete=scene.geometry(5.5f),dockedLogo=scene.geometry(6.f);
    auto redForMode=[](const std::vector<kharvox::intro::Quad>& faces,float mode){
        float maximum=-1.f;for(const auto& face:faces)if(face.mode==mode)maximum=std::max(maximum,face.color[0]);
        return maximum;
    };
    require(redForMode(logoAndCube,3.f)==redForMode(logoFront,3.f),"logo faded before cube was complete");
    require(redForMode(logoAndCube,2.f)>0.f&&redForMode(logoAndCube,2.f)<redForMode(cubeComplete,2.f),"cube does not fade in");
    require(redForMode(dockedLogo,3.f)==redForMode(cubeComplete,3.f),"docked logo faded during text intro");
    auto logoBounds=[](const std::vector<kharvox::intro::Quad>& quads){
        float lowX=100.f,highX=-100.f,lowY=100.f,highY=-100.f;
        for(const auto& q:quads)if(q.mode==3.f){
            lowX=std::min(lowX,q.x);highX=std::max(highX,q.x);
            lowY=std::min(lowY,q.y);highY=std::max(highY,q.y);
        }
        return std::array<float,4>{lowX,highX,lowY,highY};
    };
    const auto large=logoBounds(logoFront),docked=logoBounds(cubeComplete);
    require(docked[1]-docked[0]<(large[1]-large[0])*.5f&&docked[2]<-1.6f&&docked[3]<-1.f,
        "logo did not shrink and move above the cube footer");
    const auto turned=logoBounds(scene.geometry(5.5f+kharvox::intro::Scene::logoSpinPeriod*.25f));
    require(turned[1]-turned[0]<(docked[1]-docked[0])*.8f,"docked 3D logo does not rotate");
    auto corners=scene.points(kharvox::intro::Scene::logoDuration);
    require(corners.size()>1400&&corners.size()<1700,"intro should start with continuous lit cube edges only");
    for(auto d:corners)require(fabsf(fabsf(d.x)-kharvox::intro::Scene::halfSize)<.101f||fabsf(fabsf(d.y)-kharvox::intro::Scene::halfSize)<.101f||fabsf(d.z-kharvox::intro::Scene::frontZ)<.101f||fabsf(d.z-kharvox::intro::Scene::backZ)<.101f,"edge light not near cube edge");
    auto still=scene.points(6.f),shaking=scene.points(6.f,.08f);
    require(still.size()==shaking.size(),"bass reaction changed edge count");
    for(size_t i=0;i<still.size();++i)
        require(still[i].x==shaking[i].x&&still[i].y==shaking[i].y&&still[i].z==shaking[i].z,
            "bass hit still shakes cube edges");
    const auto quiet=scene.geometry(6.f,-1.f),pulsing=scene.geometry(6.f,.08f);
    bool footerJumped=false,footerGrew=false;
    for(size_t i=0;i<quiet.size();++i)if(quiet[i].mode==4.f){
        require(quiet[i].y>-scene.halfSize&&quiet[i].y< -2.2f,
            "footer is not above the cube bottom edge");
        require(quiet[i].x==pulsing[i].x&&pulsing[i].y>=quiet[i].y,
            "footer letters still jitter instead of jumping");
        if(pulsing[i].y-quiet[i].y>.01f)footerJumped=true;
        if(pulsing[i].width>quiet[i].width)footerGrew=true;
    }
    require(footerJumped&&footerGrew,"footer letters do not jump and grow on the beat");
    const float scrollStart=kharvox::intro::Scene::logoDuration+kharvox::intro::Scene::blockStart(kharvox::intro::Scene::blockCount);
    const float finalBlockStart=kharvox::intro::Scene::logoDuration+kharvox::intro::Scene::blockStart(kharvox::intro::Scene::blockCount-1);
    require(redForMode(scene.geometry(finalBlockStart+1.f),3.f)>
        redForMode(scene.geometry(scrollStart-.25f),3.f),"logo did not fade before cube explosion");
    const float loopLogoStart=scrollStart+kharvox::intro::Scene::scrollDuration;
    require(scene.page(loopLogoStart+.8f).block==kharvox::intro::Scene::blockCount+2,"logo does not return after cube reassembly");
    require(scene.points(loopLogoStart+.8f).size()==12*128,"cube missing behind returning logo");
    require(scene.page(loopLogoStart+kharvox::intro::Scene::loopLogoDuration+.1f).block==0,"text blocks do not resume after returning logo");
    bool returnedLogo=false;for(const auto& q:scene.geometry(loopLogoStart+.8f))if(q.mode==3.f)returnedLogo=true;
    require(returnedLogo,"returning logo is not visible");
    for(float t:{scrollStart+3.4f,scrollStart+4.f,scrollStart+8.f,scrollStart+12.f,scrollStart+18.f}){
        auto stars=scene.points(t);require(stars.size()>700&&stars.size()<=2600,"unexpected star density");
        float minSize=10,maxSize=0,minZ=10,maxZ=-10;unsigned bright=0;
        for(auto d:stars){
            require(std::isfinite(d.x)&&std::isfinite(d.y)&&std::isfinite(d.z)&&std::isfinite(d.size),"nonfinite star");
            require(fabsf(d.x)<=kharvox::intro::Scene::starHalfWidth+.001f&&fabsf(d.y)<=kharvox::intro::Scene::starHalfHeight+.001f&&d.z>=-6.5001f&&d.z<=2.5001f,"star escaped volume");
            require(d.color==kharvox::intro::starColor,"star is not white");
            minSize=std::min(minSize,d.size);maxSize=std::max(maxSize,d.size);minZ=std::min(minZ,d.z);maxZ=std::max(maxZ,d.z);
            if(d.size>.010f)++bright;
        }
        require(maxZ-minZ>7.5f,"starfield lost depth");
        require(maxSize>minSize*2.5f&&maxSize<.02f&&bright>10,"stars are not small pinpoints");
    }
    auto starsA=scene.points(scrollStart+3.4f),starsB=scene.points(scrollStart+3.9f);
    require(starsA.size()>700&&starsB.size()>700,"starfield density collapsed while travelling");
    require(starsA.size()!=starsB.size()||starsA.front().z!=starsB.front().z,"starfield is static");
    auto idleScroll=scene.geometry(scrollStart+8.f,-1.f),beatScroll=scene.geometry(scrollStart+8.f,.08f);
    require(idleScroll.size()==beatScroll.size(),"beat reaction changed scroller glyph count");
    size_t visibleScroller=0;for(const auto& q:idleScroll)if(q.mode==0.f)++visibleScroller;
    require(visibleScroller>100&&visibleScroller<scene.scrollQuads.size()/4,
        "scroller is not limited to visible letters");
    unsigned firstMovingLetter=~0u;bool anotherMovingLetter=false;
    for(size_t i=0;i<idleScroll.size();i++)if(idleScroll[i].mode==0.f&&fabsf(idleScroll[i].y-beatScroll[i].y)>.001f){
        if(firstMovingLetter==~0u)firstMovingLetter=idleScroll[i].line;
        else if(idleScroll[i].line!=firstMovingLetter)anotherMovingLetter=true;
    }
    require(anotherMovingLetter,"scroller letters do not react individually to a beat");
    bool lastLetterVisible=false;for(const auto& q:scene.geometry(scrollStart+kharvox::intro::Scene::scrollDuration-3.5f))if(q.mode==0.f)lastLetterVisible=true;
    bool letterDuringReassembly=false;for(const auto& q:scene.geometry(scrollStart+kharvox::intro::Scene::scrollDuration-2.5f))if(q.mode==0.f)letterDuringReassembly=true;
    require(lastLetterVisible&&!letterDuringReassembly,"scroller and cube reassembly overlap incorrectly");
    require(kharvox::intro::Scene::blocks().size()==7,"intro must contain seven text blocks");
    require(kharvox::intro::Scene::blocks()[0].back()==L"INSANE VR MOD","first block copy regressed");
    require(kharvox::intro::Scene::blocks()[2][2]==L"PROCESSOR (CPU): 486DX4 AT 66 MHZ OR HIGHER","requirements copy regressed");
    require(kharvox::intro::Scene::blocks()[6].back()==L"GET FUCKED!","final block copy regressed");
    require(kharvox::intro::Scene::scrollerText.substr(0,14)=="GREETINGS:    ","scroller prefix regressed");
    require(kharvox::intro::Scene::scrollerText.find("MR-N1CE, MO FUN VR")!=std::string_view::npos&&
        kharvox::intro::Scene::scrollerText.find("YOU GUYS ROCK!       JOHN CARMACK")!=std::string_view::npos&&
        kharvox::intro::Scene::scrollerText.substr(kharvox::intro::Scene::scrollerText.size()-
            std::string_view("ID SOFTWARE, VOODOO VR").size())==
            "ID SOFTWARE, VOODOO VR","scroller copy regressed");
    for(const auto& block:kharvox::intro::Scene::blocks())for(const auto& line:block)for(wchar_t ch:line)
        require(ch<128&&(ch<L'a'||ch>L'z'),"block font received lowercase or unsupported character");
    for(char ch:kharvox::intro::Scene::scrollerText)
        require(ch>=' '&&(ch<='Z'||ch=='_'),"scroller font received lowercase or unsupported character");
    bool footerVisible=false;for(const auto& q:scene.geometry(kharvox::intro::Scene::logoDuration+2.f))
        if(q.mode==4.f){footerVisible=true;require(q.color[0]==1.f&&q.color[1]==1.f&&q.color[2]==1.f,"cube footer is not white");}
    require(footerVisible,"cube footer missing");

    auto geometry=scene.geometry(kharvox::intro::Scene::logoDuration+2.f,.1f);
    size_t textQuads=0;for(const auto& q:geometry){require(q.width>0&&q.height>0,"invalid quad");if(q.mode==1)++textQuads;}
    require(textQuads>100,"Copper bitmap text geometry missing");
    auto projected=scene.project(eyes[0],1000,1000,scrollStart+1.25f,.1f);
    size_t projectedStars=projected[kharvox::intro::starColor].size();
    require(projectedStars>100,"starfield not visible in projection");

    auto floorScene=scene;floorScene.setAnchor(eyes,-1.7f);
    require(fabsf(floorScene.anchor.y-2.5f+1.7f)<.001f,"volume bottom does not match tracked floor");
    auto seatedScene=scene;seatedScene.setAnchor(eyes,-1.2f);
    const auto standingHud=scene.geometry(kharvox::intro::Scene::logoDuration+2.f);
    const auto seatedHud=seatedScene.geometry(kharvox::intro::Scene::logoDuration+2.f);
    require(standingHud.size()==seatedHud.size(),"floor height changed cube HUD geometry count");
    for(size_t i=0;i<standingHud.size();++i)if(standingHud[i].mode==3.f||standingHud[i].mode==4.f)
        require(standingHud[i].y==seatedHud[i].y,"cube HUD drifts below the edge with headset height");
    auto anchor=scene.anchor;eyes[0].pose.position.x+=.4f;auto moved=scene.project(eyes[0],1000,1000,scrollStart+1.25f);require(scene.anchor.x==anchor.x,"scene follows headset");
    for(auto& batch:moved)for(auto r:batch)require(r.x>=0&&r.y>=0&&r.x+r.width<=1000&&r.y+r.height<=1000,"projection outside eye");

    const auto benchStart=std::chrono::steady_clock::now();size_t rectCount=0;
    for(int frame=0;frame<120;frame++){auto frameGeometry=scene.geometry(frame/90.f,.1f);rectCount+=frameGeometry.size();}
    std::cout<<"Geometry ms/frame: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-benchStart).count()/120
        <<"; rectangles/eye: "<<rectCount/120<<'\n';

    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> base;ComPtr<ID3D11DeviceContext1> context;
    D3D_FEATURE_LEVEL level{};require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&base)),"D3D11 WARP unavailable");require(SUCCEEDED(base.As(&context)),"D3D11.1 unavailable");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=1000;desc.Height=1000;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)),"texture creation failed");ComPtr<ID3D11RenderTargetView> target;
    require(FAILED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)),"typeless regression fixture must reject inferred view");
    require(SUCCEEDED(kharvox::intro::createEyeTarget(device.Get(),texture.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,&target)),"explicit typeless eye target creation failed");
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM}){
        ComPtr<ID3D11RenderTargetView> typed;
        require(SUCCEEDED(kharvox::intro::createEyeTarget(device.Get(),texture.Get(),format,&typed)),"negotiated eye format rejected");
        D3D11_RENDER_TARGET_VIEW_DESC actual{};typed->GetDesc(&actual);
        require(actual.Format==format,"negotiated view format changed");
    }
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"staging creation failed");
    kharvox::intro::CubeRenderer renderer;renderer.initialize(device.Get());
    kharvox::intro::DesktopPreview preview;preview.initialize(device.Get(),false);
    RECT client{};GetClientRect(preview.handle(),&client);
    require(client.right==1280&&client.bottom==720,"desktop client is not 720p");
    const auto desktop=kharvox::intro::desktopView(eyes);
    require(fabsf(tanf(desktop.fov.angleRight)/tanf(desktop.fov.angleUp)-1280.f/720.f)<.001f,"desktop aspect distorted");
    renderer.prepare(context.Get(),scene,0.f,.1f);
    preview.draw(context.Get(),renderer,scene,eyes,0.f,false);
    kharvox::intro::DesktopPreview::pump();

    std::array<std::vector<unsigned char>,2> stereo,motion;
    for(int shot=0;shot<11;shot++){
        const int eye=shot==1?1:0;const bool black=shot==4;
        const float t=shot==10?scrollStart+8.f:shot==3?scrollStart+1.5f:(shot==2?.75f:(shot==5?4.75f:(shot==6?6.f:
            (shot==7?loopLogoStart+.8f:(shot==8?kharvox::intro::Scene::logoDuration+3.f:
            (shot==9?kharvox::intro::Scene::logoDuration+kharvox::intro::Scene::blockDuration(0)+3.f:0.f))))));
        renderer.prepare(context.Get(),scene,t,.1f);
        renderer.drawEye(context.Get(),target.Get(),scene,eyes[eye],1000,1000,t,black);
        context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU readback failed");
        std::vector<unsigned char> pixels;pixels.reserve(3000000);int colored=0,white=0,orange=0;
        for(unsigned y=0;y<1000;y++)for(unsigned x=0;x<1000;x++){
            auto p=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
            pixels.insert(pixels.end(),p,p+3);if(p[0]>80||p[1]>80||p[2]>80)++colored;
            if(p[0]>120&&p[1]>120&&p[2]>120)++white;
            if(p[0]>100&&p[1]<100&&p[2]<80)++orange;
            if(black)require(p[0]==0&&p[1]==0&&p[2]==0,"handoff image is not black");
        }
        context->Unmap(staging.Get(),0);if(!black)require(colored>(shot==3?700:20),"scene not rendered by D3D11");
        if(shot==7)require(white>1000&&orange>60,"returning logo and cube are not both rendered");
        std::ofstream image(output/("frame-"+std::to_string(shot)+".ppm"),std::ios::binary);image<<"P6\n1000 1000\n255\n";image.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
        if(shot<2)stereo[shot]=pixels;
        if(shot==0||shot==3)motion[shot==0?0:1]=pixels;
    }
    require(stereo[0]!=stereo[1],"stereo parallax missing");
    require(motion[0]!=motion[1],"GPU starfield is static");
    std::cout<<"PASS: forward starfield, stereo D3D11 rendering, black handoff, input gate\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

