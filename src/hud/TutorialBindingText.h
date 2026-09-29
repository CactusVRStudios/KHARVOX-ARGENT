#pragma once
#include <string>
#include <string_view>

namespace argent::hud {
struct TutorialBindingLayout {
 bool left{},swapSticks{},indexLeft{},indexRight{};
};
inline std::string tutorialBindingLabel(std::string_view action,TutorialBindingLayout layout) {
 const bool full=layout.left&&layout.swapSticks;
 const std::string weapon=layout.left?"L ":"R ",support=layout.left?"R ":"L ";
 const std::string turn=full?"L ":"R ",move=full?"R ":"L ";
 auto face=[&](bool left,bool upper)->std::string {
  const bool index=left?layout.indexLeft:layout.indexRight;
  return std::string(left?"L ":"R ")+(left&&!index?(upper?"Y":"X"):(upper?"B":"A"));
 };
 // These are localized tutorial tokens, not the similarly named native
 // user-command channels. In Eternal _bfg means Flame Belch and _quickuse
 // means equipment launch (verified in the loaded localization table).
 if(action=="_attack1")return weapon+"Trigger";
 if(action=="_attack2"||action=="_melee"||action=="_use")return weapon+"Stick click";
 if(action=="_zoom"||action=="_altfire")return weapon+"Grip";
 if(action=="_quick3")return weapon+"Grip behind shoulder";
 if(action=="_quickuse"||action=="_quickUse"||action=="_quick2")return support+"Trigger";
 if(action=="_quick0"||action=="_nextQuickItem")return support+"Grip";
 if(action=="_bfg")return face(!full,false);
 if(action=="_reload")return face(!full,true);
 if(action=="_jump")return face(full,true);
 if(action=="_dash")return face(full,false);
 if(action=="_crucible")return turn+"Stick up";
 if(action=="_changeWeapon")return turn+"Stick down";
 if(action=="_inventory")return support+"Stick click (hold)";
 if(action=="_objectives")return support+"Stick click (tap)";
 if(action=="_moveforward")return move+"Stick forward";
 if(action=="_moveback")return move+"Stick back";
 if(action=="_moveleft"||action=="_moveLeft")return move+"Stick left";
 if(action=="_moveright"||action=="_moveRight")return move+"Stick right";
 return {};
}
inline std::string replaceTutorialBindings(std::string_view text,TutorialBindingLayout layout) {
 auto identifier=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';};
 std::string result;result.reserve(text.size());
 for(size_t i=0;i<text.size();) {
  // ^a_action is a color escape immediately followed by an action.
  if(text[i]=='_'&&(i==0||!identifier(text[i-1])||(i>=2&&text[i-2]=='^'))) {
   size_t end=i+1;while(end<text.size()&&identifier(text[end]))++end;
   auto label=tutorialBindingLabel(text.substr(i,end-i),layout);
   if(!label.empty()){result+='[';result+=label;result+=']';i=end;continue;}
   result.append(text.substr(i,end-i));i=end;continue;
  }
  result+=text[i++];
 }
 return result;
}
}
