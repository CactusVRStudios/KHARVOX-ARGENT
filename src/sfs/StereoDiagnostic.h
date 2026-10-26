#pragma once
#include "EyeProjection.h"
#include <string>
namespace argent::sfs {
// Reports GPU parameter data, not pixel equality or proof that every shader uses it.
inline std::string stereoDiagnostic(const EyeUniforms& u){
 float matrixDelta=0,translationDelta=0;bool finite=true;
 for(int i=0;i<16;++i){finite=finite&&std::isfinite(u.clipFromCenter[0][i])&&std::isfinite(u.clipFromCenter[1][i]);matrixDelta=std::max(matrixDelta,std::abs(u.clipFromCenter[0][i]-u.clipFromCenter[1][i]));}
 std::string values;
 for(int e=0;e<2;++e){values+=e?" rightTranslation=":" leftTranslation=";for(int i=0;i<4;++i){if(i)values+=",";values+=std::to_string(u.eyeTranslation[e][i]);finite=finite&&std::isfinite(u.eyeTranslation[e][i]);}}
 for(int i=0;i<4;++i)translationDelta=std::max(translationDelta,std::abs(u.eyeTranslation[0][i]-u.eyeTranslation[1][i]));
 return " finite="+std::to_string(finite)+" matrixDelta="+std::to_string(matrixDelta)+" translationDelta="+std::to_string(translationDelta)+" worldEyeSeparation="+std::to_string(finite&&translationDelta>1e-6f)+values+" pixelStereo=unverified";
}
}
