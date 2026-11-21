#include "ce_commander_transition.h"
#include <cstdio>
#include <cstring>
#include <limits>

int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"presentation check failed at line %d\n",__LINE__); return 1; } } while (0)
static CeCommanderPresentedPose Pose(double angle, int axis = 2)
{
    CeCommanderPresentedPose p{}; p.zoom = 1.0f;
    const double r = angle*3.14159265358979323846/180.0;
    ce_commander_transition_detail::Quaternion q{0.0,0.0,0.0,std::cos(r*0.5)};
    if (axis == 0) q.x = std::sin(r*0.5);
    if (axis == 1) q.y = std::sin(r*0.5);
    if (axis == 2) q.z = std::sin(r*0.5);
    ce_commander_transition_detail::Matrix(q,p.rows);
    return p;
}
static bool Orthonormal(const CeCommanderPresentedPose& p)
{
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        double dot=0.0; for (int k=0;k<3;++k) dot+=double(p.rows[i*3+k])*p.rows[j*3+k];
        if (std::fabs(dot-(i==j?1.0:0.0))>0.00001) return false;
    }
    return true;
}
int main()
{
    auto a=Pose(0.0), b=Pose(180.0), result=Pose(45.0);
    a.pos[0]=-20.0f; b.pos[0]=80.0f; a.zoom=0.5f; b.zoom=2.5f;
    auto originalA=a,originalB=b;
    CHECK(CeCommanderBlendPresentedPose(a,b,0.0,&result));
    CHECK(std::memcmp(&a,&result,sizeof(a))==0);
    CHECK(CeCommanderBlendPresentedPose(a,b,1.0,&result));
    CHECK(std::memcmp(&b,&result,sizeof(b))==0);
    CHECK(CeCommanderBlendPresentedPose(a,b,0.5,&result));
    CHECK(std::fabs(result.pos[0]-30.0f)<0.0001f && std::fabs(result.zoom-1.5f)<0.0001f);
    CHECK(std::fabs(result.rows[0])<0.0001f && std::fabs(result.rows[4])<0.0001f);
    CHECK(Orthonormal(result));
    CHECK(std::memcmp(&a,&originalA,sizeof(a))==0 && std::memcmp(&b,&originalB,sizeof(b))==0);
    for (int axis=0;axis<3;++axis) for (int angle=-180;angle<=180;angle+=15) {
        a=Pose(angle,axis); b=Pose(180-angle,axis);
        for (int step=0;step<=20;++step) {
            CHECK(CeCommanderBlendPresentedPose(a,b,double(step)/20.0,&result));
            CHECK(Orthonormal(result));
        }
    }
    a=Pose(170.0);b=Pose(-170.0);
    CHECK(CeCommanderBlendPresentedPose(a,b,0.5,&result));
    CHECK(result.rows[0]<-0.9999f && result.rows[4]<-0.9999f);
    a=Pose(-45.0);b=Pose(170.0);
    CHECK(CeCommanderBlendPresentedPose(a,b,0.5,&result));
    CHECK(std::fabs(result.rows[0]-std::cos(-117.5*3.14159265358979323846/180.0))<0.0001);
    a=Pose(0.0);b=Pose(0.001);
    CHECK(CeCommanderBlendPresentedPose(a,b,0.5,&result) && Orthonormal(result));
    a.pos[0]=1e30f;b.pos[0]=-1e30f;
    CHECK(CeCommanderBlendPresentedPose(a,b,0.5,&result) && std::isfinite(result.pos[0]));
    const auto saved=result;
    CHECK(!CeCommanderBlendPresentedPose(a,b,std::numeric_limits<double>::quiet_NaN(),&result));
    CHECK(std::memcmp(&result,&saved,sizeof(result))==0);
    CHECK(!CeCommanderBlendPresentedPose(a,b,0.5,nullptr));
    b.rows[4]=0.0f;
    CHECK(!CeCommanderBlendPresentedPose(a,b,0.5,&result));
    b=Pose(0.0);b.rows[0]=-1.0f;
    CHECK(!CeCommanderBlendPresentedPose(a,b,0.5,&result));
    b=Pose(0.0);b.zoom=0.0f;
    CHECK(!CeCommanderBlendPresentedPose(a,b,0.5,&result));
    b=Pose(0.0);b.pos[1]=std::numeric_limits<float>::infinity();
    CHECK(!CeCommanderBlendPresentedPose(a,b,0.5,&result));
    CHECK(CeCommanderCameraEase(0.0,0.35)==0.0);
    CHECK(CeCommanderCameraEase(0.35,0.35)==1.0);
    CHECK(CeCommanderCameraEase(-1.0,0.35)==0.0);
    CHECK(CeCommanderCameraEase(2.0,0.35)==1.0);
    double last=0.0;
    for(int i=0;i<=100;++i){double now=CeCommanderCameraEase(double(i)*0.0035,0.35);CHECK(now>=last && now<=1.0);last=now;}
    CHECK(CeCommanderCameraEase(0.01,0.35)<0.01/0.35);
    CHECK(CeCommanderCameraEase(0.34,0.35)>0.34/0.35);
    std::printf("presentation checks=%d failures=0 skips=0\n",checks);
    return 0;
}
