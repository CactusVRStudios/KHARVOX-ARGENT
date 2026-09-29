#include "../src/sfs/EyeProjection.h"
#include "../src/sfs/EternalProjection.h"
#include <iostream>
#include <stdexcept>
using namespace argent::sfs;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static std::array<float,4> point(const Matrix& m,std::array<float,4> p){std::array<float,4> r{};for(int row=0;row<4;++row)for(int c=0;c<4;++c)r[row]+=m[c*4+row]*p[c];return r;}
int main(){try{
    float common[27]{};common[2]=-1.000008f;common[3]=-.06f;common[5]=1.777778f;common[8]=1114;common[9]=626;common[21]=2.001997f;common[26]=-1.125f;
    Matrix measured;check(eternalProjection(common,sizeof(common),measured),"Captured Eternal projection rejected");check(std::abs(measured[5]+1.777778f)<1e-5f,"Eternal vertical scale incorrect");
    auto unchanged=measured;
    // Real frame 7200: yaw/pitch changed, projection must remain the same.
    common[19]=1.414095f;common[20]=.2984871f;common[21]=.9760747f;common[22]=-1.747933f;common[24]=-.2422538f;common[25]=-.1352786f;common[26]=-1.090247f;
    check(eternalProjection(common,sizeof(common),measured),"Rotated Eternal camera rejected");
    check(std::abs(measured[0]-unchanged[0])<1e-5f&&std::abs(measured[5]-unchanged[5])<1e-5f,"Camera rotation changed FOV");
    unchanged=measured;common[26]=0;check(!eternalProjection(common,sizeof(common),measured)&&measured==unchanged,"Invalid Eternal constants replaced projection");
    common[26]=-1.090247f;common[8]=626;check(!eternalProjection(common,sizeof(common),measured),"Inconsistent viewport accepted");
    check(!eternalProjection(common,80,measured),"Truncated UBO accepted");
    // Captured at native XR output size: Eternal keeps a square projection
    // although the output image is portrait. This must not disable stereo.
    float portrait[27]{};portrait[2]=-1.00000131f;portrait[3]=-.0600000769f;
    portrait[5]=3.07920194f;portrait[8]=2496;portrait[9]=2688;
    portrait[22]=-.649518907f;portrait[26]=-.649518967f;
    check(eternalProjection(portrait,sizeof(portrait),measured),"Portrait XR camera rejected");
    check(std::abs(measured[0]+measured[5])<1e-5f,"Portrait camera must use measured square projection");
    portrait[8]=2233;portrait[9]=2405;
    check(eternalProjection(portrait,sizeof(portrait),measured),"Scaled portrait XR camera rejected");
    Matrix projection{};projection[0]=1;projection[5]=-1;projection[10]=0;projection[11]=-1;projection[14]=.1f; // infinite reversed-Z
    XrPosef camera{};camera.orientation.w=1;
    std::array<XrView,2> eyes{};for(auto& e:eyes){e.pose=camera;e.fov={-.785398163f,.785398163f,.785398163f,-.785398163f};}
    eyes[0].pose.position.x=-.032f;eyes[1].pose.position.x=.032f;
    EyeUniforms u;check(eyeProjection(projection,camera,eyes,1,u),"Valid stereo rejected");
    auto source=point(projection,{0,0,-2,1});auto left=point(u.clipFromCenter[0],source),right=point(u.clipFromCenter[1],source);
    check(std::abs(left[0]/left[3]-.016f)<1e-5f&&std::abs(right[0]/right[3]+.016f)<1e-5f,"IPD/parallax sign or magnitude incorrect");
    check(std::abs(left[2]/left[3]-.05f)<1e-5f,"Reversed depth changed");
    for(auto& e:eyes){e.pose.position={0,0,0};e.pose.orientation={0,std::sin(.15f),0,std::cos(.15f)};}
    check(eyeProjection(projection,camera,eyes,1,u),"Head rotation rejected");left=point(u.clipFromCenter[0],source);check(std::abs(left[0]/left[3]-std::tan(.3f))<1e-5f,"Head yaw canceled or inverted");
    for(auto& e:eyes){e.pose=camera;e.fov={-.6f,.9f,.8f,-.7f};}check(eyeProjection(projection,camera,eyes,1,u),"Asymmetric FOV rejected");
    auto ray=point(projection,{2*std::tan(-.6f),0,-2,1});left=point(u.clipFromCenter[0],ray);check(std::abs(left[0]/left[3]+1)<1e-5f,"Asymmetric left edge incorrect");
    auto before=u;eyes[0].pose.orientation.w=0;check(!eyeProjection(projection,camera,eyes,1,u),"Invalid quaternion accepted");check(u.clipFromCenter==before.clipFromCenter,"Invalid input overwrote last result");
    // Same menu point must describe the same ray despite asymmetric eye FOVs.
    eyes[0].pose=camera;eyes[1].pose=camera;
    eyes[0].fov={-.9f,.6f,.8f,-.7f};eyes[1].fov={-.6f,.9f,.8f,-.7f};
    check(screenProjection(projection,camera,eyes,u),"Screen UI projection rejected");
    float rays[2]{};
    for(int e=0;e<2;++e){auto q=point(u.screenClip[e],{.3f,.2f,0,1});auto f=eyes[e].fov;rays[e]=((q[0]/q[3]+1)*.5f)*(std::tan(f.angleRight)-std::tan(f.angleLeft))+std::tan(f.angleLeft);}
    check(std::abs(rays[0]-.3f)<1e-5f&&std::abs(rays[0]-rays[1])<1e-5f,"Menu point produces binocular double vision");
    eyes[0].pose=camera;check(!eyeProjection(projection,camera,eyes,0,u),"Guessed/zero world scale accepted");Matrix zero{};check(!eyeProjection(zero,camera,eyes,1,u),"Singular projection accepted");
    eyes[0].pose=camera;eyes[1].pose=camera;eyes[0].pose.position.x=-.032f;eyes[1].pose.position.x=.032f;
    check(parallelEyeProjection(projection,eyes,1,u),"Parallel eye projection rejected");
    for(int e=0;e<2;++e)for(float w:{.2f,10.f,1000.f}){auto v=point(u.clipFromCenter[e],{.1f,.2f,.07f,w});for(int i=0;i<4;++i)v[i]+=u.eyeTranslation[e][i];check(v[2]==.07f&&v[3]==w,"Parallel projection changed engine depth");}
    check(u.eyeTranslation[0][0]>0&&u.eyeTranslation[1][0]<0,"Parallel IPD reversed");
    std::cout<<"IPD, reversed depth, tracked yaw, asymmetric FOV and invalid contracts verified\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
