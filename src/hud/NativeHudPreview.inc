// Exact native SWF contracts for the hash-verified Eternal executable. The
// preview owns a separate movie: no native HUD events or inventory mutations.
bool previewApiReady{};
thread_local void* drawingPreview{};
struct PreviewMovie {void* movie{};void* asset{};int role=-1;};
PreviewMovie previewMovie;
void* failedPreviewAsset{};int failedPreviewRole=-1;ULONGLONG previewRetry{};
std::recursive_mutex previewMutex;
template<class Function> Function previewFunction(uintptr_t rva){return reinterpret_cast<Function>(image+build::rva(rva));}
bool drawCalibrationPreview(void* swf,void* canvas,uint64_t time){
 if(!swf||swf!=drawingPreview)return false;
 // The native synchronous fallback, so the isolated movie is never queued
 // to a worker that could outlive selection changes or its owning scope.
 previewFunction<void(__fastcall*)(void*,void*,int,int,uint64_t)>(0x1831450)(swf,canvas,-1,0,time);
 return true;
}
void destroyPreview(){
 if(previewMovie.movie){
  previewFunction<void(__fastcall*)(void*)>(0x181d610)(previewMovie.movie);
  previewFunction<void(__fastcall*)(void*,size_t)>(0x26ec104)(previewMovie.movie,0x21f8);
 }
 previewMovie={};
}
void* previewChild(void* sprite,const char* name){
 if(!sprite)return nullptr;
 alignas(8) unsigned char value[16]{};
 previewFunction<void*(__fastcall*)(void*,void*,const char*)>(0x185c150)(sprite,value,name);
 auto result=previewFunction<void*(__fastcall*)(void*)>(0x184e470)(value);
 const auto type=*reinterpret_cast<const int*>(value);
 if(type==2||type==8)previewFunction<void(__fastcall*)(void*)>(0x184e3b0)(value);
 return result;
}
void previewShow(void* sprite){
 if(!sprite)return;
 previewFunction<void(__fastcall*)(void*,bool,bool)>(0x1864430)(sprite,true,true);
 const auto frame=previewFunction<int(__fastcall*)(void*,const char*,bool)>(0x1859e20)(sprite,"idle",false);
 if(frame>0)previewFunction<void(__fastcall*)(void*,int)>(0x1865280)(sprite,frame);
}
bool previewEligible(int role){
 if(!previewApiReady||!handCalibrationActive.load()||role!=selectedHandRole.load()||!handRole(role))return false;
 float grip[3]{},hand[9]{};bool left{};
 return previewApiReady&&calibrationPreviewAllowed(handCalibrationActive.load(),selectedHandRole.load(),role,
  presentation::gameplayInput.load(),presentation::nativeAnimation.load()||presentation::syncAttack.load(),
  camera::hudOffhand(grip,hand,left));
}
void* preparePreview(void* original,int role,void* time){
 if(!original||!time)return nullptr;
 const auto asset=*reinterpret_cast<void**>(static_cast<unsigned char*>(original)+8);
 if(!asset)return nullptr;
 if(previewMovie.movie&&previewMovie.asset==asset&&previewMovie.role==role)return previewMovie.movie;
 if(failedPreviewAsset==asset&&failedPreviewRole==role&&GetTickCount64()<previewRetry)return nullptr;
 failedPreviewAsset=asset;failedPreviewRole=role;previewRetry=GetTickCount64()+5000;
 destroyPreview();
 auto memory=previewFunction<void*(__fastcall*)(size_t,int,void*)>(0x357240)(0x21f8,0x4f,original);
 if(!memory)return nullptr;
 auto movie=previewFunction<void*(__fastcall*)(void*,void*)>(0x181cb10)(memory,asset);
 if(!movie){previewFunction<void(__fastcall*)(void*,size_t)>(0x26ec104)(memory,0x21f8);return nullptr;}
 previewMovie={movie,asset,role};
 previewFunction<void(__fastcall*)(void*,uint64_t)>(0x181dd80)(movie,*static_cast<uint64_t*>(time));
 auto root=*reinterpret_cast<void**>(static_cast<unsigned char*>(movie)+0x18);
 previewShow(root);
 auto element=previewChild(root,"hudElement");previewShow(element);
 bool ready=element!=nullptr;
 if(role==8){
  auto cards=previewChild(element,"keycards");
  if(cards){
   previewShow(cards);
   // Native keycard layout selects frame 1 for one ordinary key slot.
   previewFunction<void(__fastcall*)(void*,int)>(0x1865280)(cards,1);
  }
  auto card=previewChild(cards,"key_0");previewShow(card);
  auto icon=previewChild(card,"icon");previewShow(icon);
  if(icon)previewFunction<void(__fastcall*)(void*,const char*)>(0x1863a50)(icon,"art/ui/icons/keycard/slayer_key_glyph");
  ready=cards&&card&&icon;
 }
 log("ETERNAL_HUD_PREVIEW role="+std::string(roles[role].name)+" isolatedMovie=1 ready="+std::to_string(ready));
 if(!ready){destroyPreview();return nullptr;}
 return movie;
}
using OwnerRender=void(__fastcall*)(void*,void*,void*,bool,bool);
OwnerRender originalOwnerRender{};
void __fastcall renderOwner(void* owner,void* view,void* time,bool advance,bool draw){
 const int role=owner?roleIndex(build::semanticRva(*static_cast<uintptr_t*>(owner)-image)):-1;
 if(previewEligible(role)){
  PreviewValueScope<unsigned char> visible(*(static_cast<unsigned char*>(owner)+0xb8),1);
  originalOwnerRender(owner,view,time,advance,draw);
 }else originalOwnerRender(owner,view,time,advance,draw);
}
bool validatePreviewApi(unsigned char* base){
 struct Signature {uintptr_t rva;std::initializer_list<unsigned char> bytes;};
 for(const auto& s:{Signature{0x15ef6c0,{0x48,0x89,0x6c,0x24,0x20,0x56,0x41,0x56,0x41,0x57}},
  Signature{0x181cb10,{0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57}},
  Signature{0x181dd80,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30}},
  Signature{0x185c150,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x49,0x28}},
  Signature{0x1864430,{0x44,0x0f,0xb6,0xd2,0x38,0x51,0x51}},
  Signature{0x1865280,{0x48,0x89,0x5c,0x24,0x08,0x57}},
  Signature{0x1863a50,{0x48,0x89,0x5c,0x24,0x08,0x57}}})
  if(std::memcmp(base+build::rva(s.rva),s.bytes.begin(),s.bytes.size()))return false;
 return true;
}
