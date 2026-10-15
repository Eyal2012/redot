#include "audio_meter.h"
#include <iostream>
#include <thread>
int main(){
    int failed=0;
    auto check=[&](bool value,const char* name){std::cout<<(value?"PASS ":"FAIL ")<<name<<'\n';failed+=!value;};
    AudioMeter meter;
    meter.publish(960*.0001*.0001,.0002f,480,1000);
    auto s=meter.snapshot(1000);
    check(std::abs(s.rms-.0001)<.000000001&&s.rms>0,"quiet PCM stays floating point below old 1/1000 threshold");
    check(std::abs(s.displayedPeak-.0002)<.000000001,"peak retains actual sample maximum");
    meter.publish(0,0,480,1100);s=meter.snapshot(1300);
    check(s.rms==0&&s.peak==0&&std::abs(s.displayedRms-.0001/std::exp(1.0))<.000000001,"silence remains zero; real-burst display releases over monotonic time");
    meter.publish(480,1,480,2000);s=meter.snapshot(2000);
    check(std::abs(s.rms-1/std::sqrt(2.0))<.0000001,"stereo RMS includes right-only signal without phase cancellation");
    meter.publish(0,0,0,3000);check(meter.snapshot(3000).rms==0,"empty packet is finite silence");
    std::atomic_bool done{};
    std::thread writer([&]{for(uint64_t n=4000;n<24000;++n)meter.publish(960*.01*.01,.02f,480,n);done=true;});
    bool coherent=true;while(!done){s=meter.snapshot(24000);if(s.frames&&std::abs(s.rms-std::sqrt(s.square/(2*s.frames)))>.0000001)coherent=false;}
    writer.join();check(coherent,"bounded concurrent snapshot stays coherent");return failed?1:0;
}
