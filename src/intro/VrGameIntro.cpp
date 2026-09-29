#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string>

#include <stdexcept>

#include "CubeD3D11.h"
#include "DesktopPreview.h"
#include "IntroMusic.h"
using Microsoft::WRL::ComPtr;
static void check(XrResult r,const char* message){if(XR_FAILED(r))throw std::runtime_error(message);}
static void hr(HRESULT r,const char* message){if(FAILED(r))throw std::runtime_error(message);}
struct IntroHeadsetUnavailable : std::runtime_error {
    static constexpr int exitCode=0x4b480002;
    IntroHeadsetUnavailable():std::runtime_error("No usable OpenXR headset is available.\n\nConnect your headset and start its OpenXR runtime, then try again. If you use Virtual Desktop, connect to your PC first."){}
};
static void checkHeadsetSystem(XrResult result){
    if(result==XR_ERROR_FORM_FACTOR_UNAVAILABLE)throw IntroHeadsetUnavailable();
    check(result,"Query OpenXR headset");
}
#define XR_FUNCTIONS(X) \
 X(xrDestroyInstance) X(xrGetSystem) X(xrCreateSession) X(xrDestroySession) X(xrPollEvent) X(xrBeginSession) X(xrEndSession) X(xrRequestExitSession) \
 X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace) X(xrEnumerateViewConfigurationViews) X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) \
 X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) X(xrLocateViews) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) \
 X(xrStringToPath) X(xrCreateActionSet) X(xrDestroyActionSet) X(xrCreateAction) X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions) X(xrGetActionStateBoolean) X(xrGetActionStateFloat) X(xrGetD3D11GraphicsRequirementsKHR)
struct IntroApp {
    PFN_xrGetInstanceProcAddr getProc{};
#define DECLARE(name) PFN_##name name{};
    XR_FUNCTIONS(DECLARE)
#undef DECLARE
    XrInstance instance{};XrSystemId system{};XrSession session{};XrSpace space{},stage{};XrActionSet actions{};XrAction buttons{},analogs{};
    bool running{},focused{},exitRequested{},stopping{},firstShown{};

    kharvox::intro::CubeRenderer renderer;
    kharvox::intro::DesktopPreview preview;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    struct Eye {XrSwapchain chain{};int width{},height{};std::vector<XrSwapchainImageD3D11KHR> images;std::vector<ComPtr<ID3D11RenderTargetView>> targets;};
    std::array<Eye,2> eyes;
    kharvox::intro::Scene scene;kharvox::intro::InputGate input;
    kharvox::intro::IntroMusic music;
    ULONGLONG start{},blackAt{},exitAt{};
    bool shutdownDone{},shutdownOk=true;
    bool shutdown(){
        if(shutdownDone)return shutdownOk;shutdownDone=true;
        music.shutdown();

        const auto gpu=kharvox::intro::CubeRenderer::finishGpu(device.Get(),context.Get());
        if(FAILED(gpu))shutdownOk=false;
        preview.shutdown();renderer.shutdown();
        auto destroy=[&](auto function,auto handle){
            if(!handle||!function)return;
            const auto result=function(handle);

            if(XR_FAILED(result))shutdownOk=false;
        };
        for(auto& eye:eyes){eye.targets.clear();eye.images.clear();destroy(xrDestroySwapchain,eye.chain);eye.chain=XR_NULL_HANDLE;}
        destroy(xrDestroySpace,stage);stage=XR_NULL_HANDLE;
        destroy(xrDestroySpace,space);space=XR_NULL_HANDLE;
        destroy(xrDestroySession,session);session=XR_NULL_HANDLE;
        destroy(xrDestroyActionSet,actions);actions=XR_NULL_HANDLE;
        destroy(xrDestroyInstance,instance);instance=XR_NULL_HANDLE;
        context.Reset();device.Reset();


        return shutdownOk;
    }
    ~IntroApp(){
        shutdown();

    }
    XrPath path(const std::string& name){XrPath p{};check(xrStringToPath(instance,name.c_str(),&p),"XR input path");return p;}
    void initialize(){
        PFN_xrCreateInstance create{};check(getProc(XR_NULL_HANDLE,"xrCreateInstance",reinterpret_cast<PFN_xrVoidFunction*>(&create)),"Load xrCreateInstance");
        PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions{};
        check(getProc(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",reinterpret_cast<PFN_xrVoidFunction*>(&enumerateExtensions)),"Load extension enumeration");
        uint32_t extensionCount{};check(enumerateExtensions(nullptr,0,&extensionCount,nullptr),"Count XR extensions");
        std::vector<XrExtensionProperties> available(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
        check(enumerateExtensions(nullptr,extensionCount,&extensionCount,available.data()),"Read XR extensions");
        bool audioExtension=false;for(auto& ext:available)if(strcmp(ext.extensionName,XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME)==0)audioExtension=true;
        std::vector<const char*> extensions{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};if(audioExtension)extensions.push_back(XR_OCULUS_AUDIO_DEVICE_GUID_EXTENSION_NAME);
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};strcpy_s(ci.applicationInfo.applicationName,"KHARVOX Intro");ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=uint32_t(extensions.size());ci.enabledExtensionNames=extensions.data();
        check(create(&ci,&instance),"Create intro OpenXR instance");
#define LOAD(name) check(getProc(instance,#name,reinterpret_cast<PFN_xrVoidFunction*>(&name)),"Load " #name);
        XR_FUNCTIONS(LOAD)
#undef LOAD

        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;const auto headsetResult=xrGetSystem(instance,&si,&system);checkHeadsetSystem(headsetResult);
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};check(xrGetD3D11GraphicsRequirementsKHR(instance,system,&req),"D3D11 graphics requirements");
        ComPtr<IDXGIFactory1> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;;i++){ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);if(memcmp(&desc.AdapterLuid,&req.adapterLuid,sizeof(LUID))==0){adapter=candidate;break;}}
        if(!adapter)throw std::runtime_error("Headset graphics adapter unavailable");
        D3D_FEATURE_LEVEL level{};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        hr(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,&level,&context),"Create intro D3D11 device");
        if(level<req.minFeatureLevel)throw std::runtime_error("Insufficient graphics feature level");
        renderer.initialize(device.Get());
        preview.initialize(device.Get());
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;check(xrCreateSession(instance,&sessionInfo,&session),"Create intro session");
        XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};ref.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;ref.poseInReferenceSpace.orientation.w=1;check(xrCreateReferenceSpace(session,&ref,&space),"Create intro space");
        ref.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
        if(XR_FAILED(xrCreateReferenceSpace(session,&ref,&stage)))stage=XR_NULL_HANDLE;
        setupInput();
        uint32_t count{};check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"Enumerate views");if(count!=2)throw std::runtime_error("Stereo headset required");
        std::array<XrViewConfigurationView,2> configs{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};check(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,configs.data()),"Read views");
        check(xrEnumerateSwapchainFormats(session,0,&count,nullptr),"Enumerate formats");std::vector<int64_t> formats(count);check(xrEnumerateSwapchainFormats(session,count,&count,formats.data()),"Read formats");
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;for(auto candidate:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB})if(std::find(formats.begin(),formats.end(),candidate)!=formats.end()){format=candidate;break;}
        if(format==DXGI_FORMAT_UNKNOWN)throw std::runtime_error("No supported intro swapchain format");
        for(int e=0;e<2;e++){
            auto& eye=eyes[e];eye.width=configs[e].recommendedImageRectWidth;eye.height=configs[e].recommendedImageRectHeight;

            XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};sc.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;sc.format=format;sc.sampleCount=1;sc.width=eye.width;sc.height=eye.height;sc.faceCount=1;sc.arraySize=1;sc.mipCount=1;check(xrCreateSwapchain(session,&sc,&eye.chain),"Create intro eye");
            check(xrEnumerateSwapchainImages(eye.chain,0,&count,nullptr),"Count eye images");eye.images.resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});check(xrEnumerateSwapchainImages(eye.chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"Read eye images");
            for(auto image:eye.images){
                if(!image.texture)throw std::runtime_error("OpenXR returned a null eye texture");

                ComPtr<ID3D11RenderTargetView> target;
                hr(kharvox::intro::createEyeTarget(device.Get(),image.texture,format,&target),"Create eye target");
                eye.targets.push_back(target);
            }
        }
        if(!scene.makeText())throw std::runtime_error("Embedded 04b font could not be rendered");
        wchar_t audioDevice[XR_MAX_AUDIO_DEVICE_STR_SIZE_OCULUS]{};
        if(audioExtension){
            PFN_xrGetAudioOutputDeviceGuidOculus audioOutput{};
            if(XR_SUCCEEDED(getProc(instance,"xrGetAudioOutputDeviceGuidOculus",reinterpret_cast<PFN_xrVoidFunction*>(&audioOutput)))&&audioOutput)
                if(XR_FAILED(audioOutput(instance,audioDevice)))audioDevice[0]=0;
        }
        hr(music.initialize(audioDevice),"Initialize ninja.mod audio output");

        start=GetTickCount64();
    }
    void setupInput(){
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(set.actionSetName,"intro");strcpy_s(set.localizedActionSetName,"Game Intro");check(xrCreateActionSet(instance,&set,&actions),"Create intro action set");
        XrActionCreateInfo action{XR_TYPE_ACTION_CREATE_INFO};action.actionType=XR_ACTION_TYPE_BOOLEAN_INPUT;strcpy_s(action.actionName,"dismiss_button");strcpy_s(action.localizedActionName,"Dismiss button");check(xrCreateAction(actions,&action,&buttons),"Create button action");
        action.actionType=XR_ACTION_TYPE_FLOAT_INPUT;strcpy_s(action.actionName,"dismiss_analog");strcpy_s(action.localizedActionName,"Dismiss trigger or grip");check(xrCreateAction(actions,&action,&analogs),"Create analog action");
        auto suggest=[&](const char* profile,const std::vector<std::string>& clicks,const std::vector<std::string>& values){
            std::vector<XrActionSuggestedBinding> bindings;for(const auto& p:clicks)bindings.push_back({buttons,path(p)});for(const auto& p:values)bindings.push_back({analogs,path(p)});
            XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};info.interactionProfile=path(profile);info.countSuggestedBindings=uint32_t(bindings.size());info.suggestedBindings=bindings.data();xrSuggestInteractionProfileBindings(instance,&info);
        };
        std::vector<std::string> touch,index,vive,microsoft,hp,analogTouch,analogIndex,analogVive;
        for(std::string hand:{"left","right"}){
            auto base="/user/hand/"+hand+"/input/";
            touch.push_back(base+(hand=="left"?"x/click":"a/click"));touch.push_back(base+(hand=="left"?"y/click":"b/click"));touch.push_back(base+"thumbstick/click");
            index.push_back(base+"a/click");index.push_back(base+"b/click");index.push_back(base+"thumbstick/click");
            vive.push_back(base+"menu/click");vive.push_back(base+"trackpad/click");vive.push_back(base+"squeeze/click");
            microsoft.push_back(base+"menu/click");microsoft.push_back(base+"thumbstick/click");microsoft.push_back(base+"trackpad/click");microsoft.push_back(base+"squeeze/click");
            analogTouch.push_back(base+"trigger/value");analogTouch.push_back(base+"squeeze/value");analogIndex.push_back(base+"trigger/value");analogIndex.push_back(base+"squeeze/force");analogVive.push_back(base+"trigger/value");
        }
        hp=touch;touch.push_back("/user/hand/left/input/menu/click");hp.push_back("/user/hand/left/input/menu/click");hp.push_back("/user/hand/right/input/menu/click");
        suggest("/interaction_profiles/oculus/touch_controller",touch,analogTouch);suggest("/interaction_profiles/valve/index_controller",index,analogIndex);suggest("/interaction_profiles/htc/vive_controller",vive,analogVive);suggest("/interaction_profiles/microsoft/motion_controller",microsoft,analogVive);
        suggest("/interaction_profiles/khr/simple_controller",{"/user/hand/left/input/select/click","/user/hand/right/input/select/click","/user/hand/left/input/menu/click","/user/hand/right/input/menu/click"},{});
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions;check(xrAttachSessionActionSets(session,&attach),"Attach intro actions");
    }
    bool buttonDown(){
        bool down=false;for(int key=1;key<256;key++)down|=(GetAsyncKeyState(key)&0x8000)!=0;
        XrActiveActionSet set{actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&set;
        if(XR_SUCCEEDED(xrSyncActions(session,&sync))){
            XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.action=buttons;XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};if(XR_SUCCEEDED(xrGetActionStateBoolean(session,&info,&b)))down|=b.isActive&&b.currentState;
            info.action=analogs;XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};if(XR_SUCCEEDED(xrGetActionStateFloat(session,&info,&f)))down|=f.isActive&&f.currentState>.55f;
        }return down;
    }
    void render(){

        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};check(xrWaitFrame(session,&wait,&frame),"Wait intro frame");XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};check(xrBeginFrame(session,&begin),"Begin intro frame");
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=frame.predictedDisplayTime;li.space=space;XrViewState state{XR_TYPE_VIEW_STATE};uint32_t count{};
        check(xrLocateViews(session,&li,&state,2,&count,views.data()),"Locate intro views");
        bool valid=count==2&&(state.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT)&&(state.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT);
        if(valid&&!scene.anchored){
            float floorY=NAN;
            if(stage){XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};if(XR_SUCCEEDED(xrLocateSpace(stage,space,frame.predictedDisplayTime,&floor))&&(floor.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT))floorY=floor.pose.position.y;}
            scene.setAnchor(views,floorY);start=GetTickCount64();

        }
        if(input.update(focused,buttonDown())){music.stop();blackAt=GetTickCount64();}
        std::array<XrCompositionLayerProjectionView,2> projectionViews{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        bool drew=frame.shouldRender&&valid;
        const float sceneTime=float(GetTickCount64()-start)*.001f,beatAge=music.beatAge();
        if(drew&&!input.dismissed)renderer.prepare(context.Get(),scene,sceneTime,beatAge);
        if(drew)for(int e=0;e<2;e++){
            auto& eye=eyes[e];uint32_t i{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(xrAcquireSwapchainImage(eye.chain,&acquire,&i),"Acquire intro image");XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;check(xrWaitSwapchainImage(eye.chain,&wi),"Wait intro image");
            renderer.drawEye(context.Get(),eye.targets[i].Get(),scene,views[e],
                eye.width,eye.height,sceneTime,input.dismissed);
            context->Flush();XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(xrReleaseSwapchainImage(eye.chain,&ri),"Release intro image");
            auto& pv=projectionViews[e];pv.pose=views[e].pose;pv.fov=views[e].fov;pv.subImage.swapchain=eye.chain;pv.subImage.imageRect.extent={eye.width,eye.height};
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=projectionViews.data();const auto* header=reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=drew?1:0;end.layers=drew?&header:nullptr;check(xrEndFrame(session,&end),"Submit intro frame");
        if(drew)preview.draw(context.Get(),renderer,scene,views,sceneTime,input.dismissed);
        if(drew){if(!input.dismissed){
            input.presented=true;
            if(!firstShown){hr(music.start(),"Start intro chiptune");}
            firstShown=true;
        }}
        hr(music.status(),"Intro audio stream");
    }
    int run(){
        while(!stopping){
            if(kharvox::intro::DesktopPreview::pump()){input.dismissed=true;stopping=true;}
            auto now=GetTickCount64();
            bool leave=input.dismissed;
            if(blackAt&&now-blackAt>120000)leave=true;
            if(!firstShown&&now-start>60000)throw std::runtime_error("Headset did not display intro within 60 seconds");
            if(leave&&!exitRequested){if(running){check(xrRequestExitSession(session),"Request intro exit");exitRequested=true;exitAt=now;}else break;}
            if(exitRequested&&now-exitAt>5000)break;
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            while(xrPollEvent(instance,&event)==XR_SUCCESS){
                if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;focused=state==XR_SESSION_STATE_FOCUSED;
                    if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;check(xrBeginSession(session,&bi),"Begin intro session");running=true;}
                    if(state==XR_SESSION_STATE_STOPPING){check(xrEndSession(session),"End intro session");running=false;stopping=true;}
                    if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING){running=false;stopping=true;}
                }else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)stopping=true;
                event={XR_TYPE_EVENT_DATA_BUFFER};
            }
            if(stopping)break;if(running)render();else Sleep(10);
        }
        return input.dismissed?0:2;
    }
};
int runArgentVrIntro(const wchar_t* loaderPath){
    SetProcessDPIAware();
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    int result=1;
    HMODULE loader=LoadLibraryExW(loaderPath,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    try{
        if(!loader)throw std::runtime_error("Cannot load the packaged OpenXR loader.");
        IntroApp app;app.getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
        if(!app.getProc)throw std::runtime_error("OpenXR loader entry point missing.");
        app.initialize();
        result=app.run();if(!app.shutdown())result=1;
    }catch(const std::exception& error){

        const bool missing=dynamic_cast<const IntroHeadsetUnavailable*>(&error)!=nullptr;
        if(missing)result=IntroHeadsetUnavailable::exitCode;
        MessageBoxA(nullptr,error.what(),
            "KHARVOX Intro",MB_OK|(missing?MB_ICONINFORMATION:MB_ICONERROR));
    }
    if(loader)FreeLibrary(loader);
    if(SUCCEEDED(com))CoUninitialize();
    return result;
}

