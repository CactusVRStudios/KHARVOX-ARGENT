#pragma once
namespace argent {
#ifdef ARGENT_CLEAN_RELEASE
inline constexpr bool cleanRelease=true;
#else
inline constexpr bool cleanRelease=false;
#endif
}
