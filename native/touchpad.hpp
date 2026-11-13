#pragma once
#include "platform.hpp"
namespace glide {
class RawTouchpad {
    struct Device {
        std::vector<BYTE> data;
        std::vector<HIDP_VALUE_CAPS> xs;
        std::unordered_map<USHORT,HIDP_VALUE_CAPS> ys;
        HIDP_VALUE_CAPS count{},scan{};
        bool hasScan=false,valid=false;
        FrameAssembler assembler;
        PHIDP_PREPARSED_DATA ppd() { return reinterpret_cast<PHIDP_PREPARSED_DATA>(data.data()); }
        static bool contains(const HIDP_VALUE_CAPS& c,USHORT page,USHORT usage) {
            return c.UsagePage==page && (c.IsRange ? c.Range.UsageMin<=usage && usage<=c.Range.UsageMax : c.NotRange.Usage==usage);
        }
        explicit Device(HANDLE handle) {
            UINT size=0; if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,nullptr,&size)==UINT(-1) || !size) return;
            data.resize(size); if(GetRawInputDeviceInfoW(handle,RIDI_PREPARSEDDATA,data.data(),&size)==UINT(-1)) return;
            HIDP_CAPS caps{}; if(HidP_GetCaps(ppd(),&caps)!=HIDP_STATUS_SUCCESS || caps.UsagePage!=0x0d || caps.Usage!=5) return;
            USHORT n=caps.NumberInputValueCaps; std::vector<HIDP_VALUE_CAPS> values(n);
            if(HidP_GetValueCaps(HidP_Input,values.data(),&n,ppd())!=HIDP_STATUS_SUCCESS) return;
            bool hasCount=false;
            for(const auto& c:values) {
                if(contains(c,0x0d,0x54)) { count=c; hasCount=true; }
                if(contains(c,0x0d,0x56)) { scan=c; hasScan=true; }
                if(c.LinkCollection && contains(c,1,0x30)) xs.push_back(c);
                if(c.LinkCollection && contains(c,1,0x31)) ys[c.LinkCollection]=c;
            }
            valid=hasCount && !xs.empty();
        }
        bool value(BYTE* report,ULONG length,USHORT page,USHORT link,USHORT usage,ULONG& result) {
            return HidP_GetUsageValue(HidP_Input,page,link,usage,&result,ppd(),reinterpret_cast<PCHAR>(report),length)==HIDP_STATUS_SUCCESS;
        }
        bool parse(BYTE* report,ULONG length,double time,Frame& result) {
            if(!length || report[0]!=count.ReportID) return false;
            ULONG total=0,timestamp=0;
            if(!value(report,length,0x0d,count.LinkCollection,0x54,total) || total>10) return false;
            if(hasScan) value(report,length,0x0d,scan.LinkCollection,0x56,timestamp);
            int slots=total ? int(total) : assembler.remaining();
            Frame part{}; part.time=time; uint32_t ids[10]{}; int idsCount=0;
            for(int i=0;i<std::min(slots,int(xs.size()));i++) {
                const auto& xcap=xs[i]; USHORT link=xcap.LinkCollection;
                ULONG id=0,x=0,y=0;
                if(xcap.ReportID!=report[0] || !value(report,length,0x0d,link,0x51,id)) continue;
                USAGE usages[32]{}; ULONG buttons=32;
                if(HidP_GetUsages(HidP_Input,0x0d,link,usages,&buttons,ppd(),reinterpret_cast<PCHAR>(report),length)!=HIDP_STATUS_SUCCESS) continue;
                bool tip=false,confidence=false;
                for(ULONG u=0;u<buttons;u++) { tip|=usages[u]==0x42; confidence|=usages[u]==0x47; }
                if(idsCount<10) ids[idsCount++]=id;
                auto yi=ys.find(link);
                if(!tip || !confidence || yi==ys.end() || !value(report,length,1,link,0x30,x) || !value(report,length,1,link,0x31,y)) continue;
                const auto& yc=yi->second;
                if(xcap.LogicalMax<=xcap.LogicalMin || yc.LogicalMax<=yc.LogicalMin || part.count>=10) continue;
                part.contacts[part.count++]={id,(double(x)-xcap.LogicalMin)/(xcap.LogicalMax-xcap.LogicalMin),(double(y)-yc.LogicalMin)/(yc.LogicalMax-yc.LogicalMin)};
            }
            return assembler.add(timestamp,int(total),part,ids,idsCount,result);
        }
    };
    std::unordered_map<HANDLE,std::unique_ptr<Device>> devices;
    std::vector<BYTE> buffer;
    HWND receiver;
    Device* device(HANDLE h) {
        auto it=devices.find(h); if(it!=devices.end()) return it->second.get();
        auto p=std::make_unique<Device>(h); if(!p->valid) p.reset();
        Device* result=p.get(); devices.emplace(h,std::move(p)); return result;
    }
public:
    uint64_t reports=0,frames=0;
    int fingers=0;
    explicit RawTouchpad(HWND w):receiver(w) {
        RAWINPUTDEVICE rid{0x0d,5,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,w};
        if(!RegisterRawInputDevices(&rid,1,sizeof(rid))) throw std::runtime_error("Raw Input registration failed");
        UINT n=0; GetRawInputDeviceList(nullptr,&n,sizeof(RAWINPUTDEVICELIST));
        std::vector<RAWINPUTDEVICELIST> list(n);
        UINT result=GetRawInputDeviceList(list.data(),&n,sizeof(RAWINPUTDEVICELIST));
        if(result!=UINT(-1)) for(UINT i=0;i<result;i++) if(list[i].dwType==RIM_TYPEHID) device(list[i].hDevice);
    }
    ~RawTouchpad() { RAWINPUTDEVICE rid{0x0d,5,RIDEV_REMOVE,nullptr}; RegisterRawInputDevices(&rid,1,sizeof(rid)); }
    int connected() const { int count=0; for(const auto& d:devices) if(d.second) count++; return count; }
    void changed(HANDLE h,bool arrived) { devices.erase(h); if(arrived) device(h); }
    void read(HRAWINPUT input,const std::function<void(const Frame&)>& callback) {
        UINT size=0;
        if(GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==UINT(-1) || size<sizeof(RAWINPUTHEADER)+8 || size>1024*1024) return;
        buffer.resize(size);
        if(GetRawInputData(input,RID_INPUT,buffer.data(),&size,sizeof(RAWINPUTHEADER))==UINT(-1)) return;
        auto raw=reinterpret_cast<RAWINPUT*>(buffer.data()); if(raw->header.dwType!=RIM_TYPEHID) return;
        auto d=device(raw->header.hDevice); if(!d) return;
        auto& hid=raw->data.hid;
        size_t offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
        if(!hid.dwSizeHid || !hid.dwCount || uint64_t(hid.dwSizeHid)*hid.dwCount+offset>size) return;
        for(DWORD i=0;i<hid.dwCount;i++) {
            Frame frame{}; reports++;
            if(d->parse(hid.bRawData+i*hid.dwSizeHid,hid.dwSizeHid,now(),frame)) { frames++; fingers=frame.count; callback(frame); }
        }
    }
};
}
