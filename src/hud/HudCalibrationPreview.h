#pragma once
#include "OffhandLayout.h"
namespace argent::hud {
inline bool calibrationPreviewAllowed(bool enabled,int selected,int role,bool gameplay,bool animation,bool tracked){
 return enabled&&role==selected&&handRole(role)&&gameplay&&!animation&&tracked;
}
template<class T> class PreviewValueScope {
 T& slot;T saved;
public:
 PreviewValueScope(T& target,T value):slot(target),saved(target){slot=value;}
 ~PreviewValueScope(){slot=saved;}
 PreviewValueScope(const PreviewValueScope&)=delete;
 PreviewValueScope& operator=(const PreviewValueScope&)=delete;
};
}
