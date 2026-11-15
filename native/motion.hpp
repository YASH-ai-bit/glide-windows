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
// Integrate a velocity profile: short smooth ramp, steady travel, short brake.
// Committed travel does not spend the whole remaining distance easing to a stop.
class ReleaseMotion {
    double from=0,to=0,direction=1,start=0,length=0;
    double initial=0,peak=0,ramp=0,cruise=0,brake=0;
    static double integratedEase(double u) { return u*u*u*(1-.5*u); }
public:
    void begin(double position,double target,double velocity,double time,bool animated=true) {
        from=position; to=target; start=time;
        double distance=std::abs(to-from);
        length=ramp=cruise=brake=0;
        if(!animated || distance<.00025) return;
        direction=to>from ? 1. : -1.;
        double scale=std::sqrt(std::min(1.,distance/.16));
        ramp=.032*scale; brake=.028*scale;
        // Bound reverse travel to less than 1.25% of a page. Ordinary release
        // velocity is preserved; fast forward swipes are never forced to slow first.
        initial=std::max(velocity*direction,-.025/ramp);
        peak=std::max(4.8,initial);
        double rampsDistance=.5*(initial+peak)*ramp+.5*peak*brake;
        if(distance>=rampsDistance) cruise=(distance-rampsDistance)/peak;
        else {
            double triangularPeak=(2*distance-initial*ramp)/(ramp+brake);
            if(triangularPeak>=std::max(0.,initial)) peak=triangularPeak;
            else {
                // Already moving too quickly for a ramp: brake immediately over
                // exactly the remaining distance, retaining the initial velocity.
                peak=initial; ramp=0; brake=2*distance/initial;
            }
        }
        length=ramp+cruise+brake;
    }
    double duration() const { return length; }
    bool done(double time) const { return time-start>=length; }
    double value(double time) const {
        if(length==0 || done(time)) return to;
        double elapsed=std::max(0.,time-start),travel=0;
        if(ramp>0 && elapsed<ramp) {
            double u=elapsed/ramp;
            travel=initial*elapsed+(peak-initial)*ramp*integratedEase(u);
        } else {
            travel=.5*(initial+peak)*ramp;
            elapsed-=ramp;
            travel+=peak*std::min(elapsed,cruise);
            if(elapsed>cruise) {
                double u=clamp((elapsed-cruise)/brake,0,1);
                travel+=peak*brake*(u-integratedEase(u));
            }
        }
        return from+direction*travel;
    }
};
}
