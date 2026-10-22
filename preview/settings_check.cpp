#include "settings.h"
#include "spatial.h"
#include <cassert>
#include <fstream>
#include <unistd.h>

int main() {
    PreviewSettings settings;
    assert(settings.valid() && settings.value(0)=="1920 x 1080" && settings.value(2)=="20 Mbps");
    settings.adjust(0,1); assert(settings.resolution==0);
    settings.adjust(0,-1); assert(settings.resolution==1);
    settings.adjust(1,1); assert(settings.fps==0);
    for(int i=0;i<4;++i) settings.adjust(2,-1);
    assert(settings.bitrate==1);
    settings.adjust(3,1); assert(settings.reduced_motion);
    assert(settings.value(3)=="Reduced");
    settings.adjust(3,1);assert(settings.value(3)=="Full");
    settings.adjust(3,1);
    char directory[]="/tmp/moonlight-profile-XXXXXX";
    assert(mkdtemp(directory));
    auto path=std::filesystem::path(directory)/"profile.cfg";
    assert(load_settings(path).resolution==1);
    std::string error;
    assert(save_settings(settings,path,error));
    auto loaded=load_settings(path);
    assert(loaded.resolution==settings.resolution && loaded.fps==settings.fps && loaded.bitrate==settings.bitrate && loaded.reduced_motion);
    settings.bitrate=4;
    assert(!save_settings(settings,path,error));
    assert(load_settings(path).bitrate==1);
    std::ofstream(path)<<"resolution = 9; fps = 0; bitrate = 0; reduced_motion = false;";
    bool rejected=false;
    try { (void)load_settings(path); } catch(const std::exception &) { rejected=true; }
    assert(rejected);
    std::filesystem::remove_all(directory);

    auto origin=spatial::project({0,0,0});
    assert(origin.x==0 && origin.y==0);
    for(int i=0;i<100;++i) {
        auto p=spatial::rotate({1,1,1},i*0.1f);
        auto projected=spatial::project(p);
        assert(std::isfinite(projected.x) && std::isfinite(projected.y));
        assert(std::abs(p.x*p.x+p.y*p.y+p.z*p.z-3)<0.02f);
    }
}
