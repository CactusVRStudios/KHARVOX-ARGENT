#pragma once
#include "EternalCameraMath.h"
namespace argent::player {
inline bool meathookViewAngles(const camera::Basis& basis,float* angles){
 if(!angles||!camera::validBasis(basis))return false;
 constexpr float degrees=57.2957795131f;
 angles[0]=std::atan2(-basis[2],std::hypot(basis[0],basis[1]))*degrees;
 angles[1]=std::atan2(basis[1],basis[0])*degrees;
 angles[2]=std::atan2(basis[5],basis[8])*degrees;
 return true;
}
}
