#include "../src/sfs/StereoDiagnostic.h"
#include <iostream>
#include <limits>
int main(){
 using namespace argent::sfs;
 EyeUniforms u{{identity(),identity()},{}};
 if(stereoDiagnostic(u).find("worldEyeSeparation=0")==std::string::npos)return 1;
 u.clipFromCenter[1][12]=.1f; // FOV asymmetry alone is not metric stereo.
 if(stereoDiagnostic(u).find("worldEyeSeparation=0")==std::string::npos)return 2;
 u.eyeTranslation[0][0]=.03f;u.eyeTranslation[1][0]=-.03f;
 if(stereoDiagnostic(u).find("worldEyeSeparation=1")==std::string::npos)return 3;
 u.eyeTranslation[0][0]=std::numeric_limits<float>::quiet_NaN();
 if(stereoDiagnostic(u).find("finite=0")==std::string::npos)return 4;
 std::cout<<"PASS: mono, asymmetric FOV, stereo separation and nonfinite diagnostics\n";
}
