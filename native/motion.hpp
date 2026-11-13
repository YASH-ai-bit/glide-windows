#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace glide {
inline double clamp(double x,double lo,double hi) { return std::max(lo,std::min(x,hi)); }
struct Contact { uint32_t id=0; double x=0,y=0; };
struct Frame { std::array<Contact,10> contacts{}; int count=0,reported=0; double time=0; };
class FrameAssembler {
    std::array<uint32_t,10> seen{};
    int seenCount=0,expected=0;
    uint32_t scan=0;
    bool pending=false;
    Frame accumulated{};
public:
    int remaining() const { return pending ? std::max(0,expected-seenCount) : 0; }
    bool add(uint32_t timestamp,int total,const Frame& part,const uint32_t* ids,int idCount,Frame& out) {
        if(total>0) { expected=total; scan=timestamp; seenCount=0; pending=true; accumulated={}; }
        else if(pending && timestamp!=scan) pending=false;
        if(!pending) { out={}; out.time=part.time; return total==0; }
        for(int n=0;n<idCount;n++) {
            bool found=false; for(int i=0;i<seenCount;i++) found|=seen[i]==ids[n];
            if(!found && seenCount<10) seen[seenCount++]=ids[n];
        }
        for(int n=0;n<part.count;n++) {
            int i=0; while(i<accumulated.count && accumulated.contacts[i].id!=part.contacts[n].id) i++;
            if(i<10) { accumulated.contacts[i]=part.contacts[n]; if(i==accumulated.count) accumulated.count++; }
        }
        if(seenCount<expected) return false;
        out=accumulated; out.reported=expected; out.time=part.time; pending=false; return true;
    }
};
class Velocity {
    struct Sample { double t=0,p=0; };
    std::array<Sample,32> samples{};
    int count=0;
public:
    void clear() { count=0; }
    void add(double t,double p) {
        int remove=0; while(remove<count && t-samples[remove].t>.060) remove++;
        if(remove) { std::move(samples.begin()+remove,samples.begin()+count,samples.begin()); count-=remove; }
        if(count==32) { std::move(samples.begin()+1,samples.end(),samples.begin()); count--; }
        samples[count++]={t,p};
    }
    double value(double now) const {
        if(count<2 || now-samples[count-1].t>.060) return 0;
        double mt=0,mp=0,st=0,sp=0,weight=0;
        for(int i=0;i<count;i++) { double w=std::exp((samples[i].t-now)/.025); mt+=w*samples[i].t; mp+=w*samples[i].p; weight+=w; }
        mt/=weight; mp/=weight;
        for(int i=0;i<count;i++) { double w=std::exp((samples[i].t-now)/.025),t=samples[i].t-mt; st+=w*t*t; sp+=w*t*(samples[i].p-mp); }
        return st>1e-9 ? clamp(sp/st,-8,8) : 0;
    }
};
struct GestureEvent {
    bool touchBegin=false,touchEnd=false,begin=false,move=false,end=false,cancel=false;
    double progress=0,velocity=0;
};
class Gesture {
    bool armed=false,tracking=false,rejected=false;
    std::array<uint32_t,3> ids{};
    double startX=0,startY=0,progress=0,dragOffset=0,lastFrame=0,lastMotion=0,lastX=0;
    Velocity history;
public:
    double sensitivity=1.85;
    void reset() { armed=tracking=rejected=false; history.clear(); }
    void capture() { if(armed && !rejected) tracking=true; }
    GestureEvent feed(const Frame& f) {
        GestureEvent e; lastFrame=f.time;
        if(f.count!=3 || f.reported!=3) {
            e.touchEnd=armed; e.end=tracking; e.cancel=f.count>3 || f.reported>3;
            e.progress=progress; e.velocity=f.time-lastMotion>.075 ? 0 : history.value(f.time);
            if(tracking) rejected=true;
            armed=tracking=false;
            if(f.count==0) rejected=false;
            else if(f.count>3 || f.reported>3) rejected=true;
            return e;
        }
        if(rejected) return e;
        std::array<uint32_t,3> current{{f.contacts[0].id,f.contacts[1].id,f.contacts[2].id}};
        std::sort(current.begin(),current.end());
        double x=0,y=0; for(int i=0;i<3;i++) { x+=f.contacts[i].x/3; y+=f.contacts[i].y/3; }
        if(!armed) {
            armed=true; ids=current; startX=lastX=x; startY=y; progress=dragOffset=0; lastMotion=f.time;
            history.clear(); history.add(f.time,0); e.touchBegin=true; return e;
        }
        if(ids!=current) { e.end=tracking; e.touchEnd=true; e.cancel=true; tracking=armed=false; rejected=true; return e; }
        double dx=x-startX,dy=y-startY;
        progress=dx*sensitivity-dragOffset; history.add(f.time,dx*sensitivity);
        if(std::abs(x-lastX)>.00025) lastMotion=f.time; lastX=x;
        if(!tracking) {
            if(std::abs(dy)>.018 && std::abs(dy)>std::abs(dx)*1.25) { rejected=true; armed=false; e.touchEnd=true; e.cancel=true; return e; }
            if(std::abs(dx)<.010 || std::abs(dx)<std::abs(dy)*1.35) return e;
            // Remove the recognition dead zone from the drag, avoiding a jump at pickup.
            dragOffset=std::copysign(.010*sensitivity,dx); progress=dx*sensitivity-dragOffset;
            tracking=true; e.begin=true;
        }
        e.move=true; e.progress=progress; e.velocity=history.value(f.time); return e;
    }
    bool stale(double time) const { return tracking && time-lastFrame>1.5; }
};
inline int destination(double position,double velocity) {
    double projected=position+clamp(velocity*.085,-.35,.35);
    if(position<-.025 && projected<-.30) return 1;
    if(position>.025 && projected>.30) return -1;
    return 0;
}
// Evaluate intent throughout the drag; hysteresis prevents tiny velocity changes
// from flickering between commit/cancel. Reversing past the return threshold cancels.
class CommitIntent {
    int choice=0;
public:
    void reset() { choice=0; }
    int update(double position,double velocity) {
        double projected=position+clamp(velocity*.085,-.35,.35);
        if(choice==1 && (position>-.14 || projected>-.10)) choice=0;
        if(choice==-1 && (position<.14 || projected<.10)) choice=0;
        if(!choice) choice=destination(position,velocity);
        return choice;
    }
};
inline double smoothStep(double x) { x=clamp(x,0,1); return x*x*(3-2*x); }
inline double incomingFit(double reveal) { return smoothStep(reveal/.28); }
// Bounded elastic resistance, continuous with ordinary dragging at either end.
inline double dragPosition(double raw,double lower,double upper) {
    constexpr double extent=.12;
    if(raw<lower) { double d=lower-raw; return lower-extent*d/(extent+d); }
    if(raw>upper) { double d=raw-upper; return upper+extent*d/(extent+d); }
    return raw;
}
inline double rawPosition(double displayed,double lower,double upper) {
    constexpr double extent=.12;
    if(displayed<lower) { double d=std::min(lower-displayed,extent-.000001); return lower-extent*d/(extent-d); }
    if(displayed>upper) { double d=std::min(displayed-upper,extent-.000001); return upper+extent*d/(extent-d); }
    return displayed;
}
// Finite Hermite motion with an exact endpoint. Opposing momentum can make a tiny,
// bounded excursion before reversing; it never creates an endless spring tail.
class ReleaseMotion {
    double from=0,to=0,slope=0,start=0,length=0;
public:
    void begin(double position,double target,double velocity,double time,bool animated=true) {
        from=position; to=target; start=time;
        double distance=std::abs(to-from);
        // The remaining distance, rather than a fixed dwell, determines release time.
        // A nearly completed drag settles in a few frames, a fast flick in about 90 ms.
        length=!animated || distance<.00025 ? 0 : clamp(.040+.100*std::sqrt(distance)-.006*std::min(std::abs(velocity),6.0),.040,.135);
        slope=distance<1e-9 ? 0 : clamp(velocity*length/(to-from),-std::min(.8,.10/distance),2.8);
    }
    double duration() const { return length; }
    bool done(double time) const { return time-start>=length; }
    double value(double time) const {
        if(length==0 || done(time)) return to;
        double u=clamp((time-start)/length,0,1),u2=u*u,u3=u2*u;
        return from+(to-from)*((-2*u3+3*u2)+slope*(u3-2*u2+u));
    }
};
}
