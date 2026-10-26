#include "SystemLoopback.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <endpointvolume.h>
 #include <mmreg.h>
 #include <ksmedia.h>
 #include <initguid.h>                       // définit PKEY_Device_FriendlyName ici
 #include <functiondiscoverykeys_devpkey.h>
 #include <dbghelp.h>
 #include <dwmapi.h>
 #pragma comment(lib, "ole32.lib")
 #pragma comment(lib, "dbghelp.lib")
 #pragma comment(lib, "dwmapi.lib")
#endif

#if JUCE_WINDOWS
namespace
{
    // pointeur COM minimal (Release automatique)
    template <class T> struct ComRef
    {
        T* p = nullptr;
        ~ComRef()                 { reset(); }
        void reset()              { if(p){ p->Release(); p=nullptr; } }
        T** put()                 { reset(); return &p; }
        T* operator->() const     { return p; }
        explicit operator bool() const { return p!=nullptr; }
    };

    juce::String endpointId(IMMDevice* dev)
    {
        LPWSTR id=nullptr;
        if(dev==nullptr || FAILED(dev->GetId(&id)) || id==nullptr) return {};
        juce::String s(id);
        CoTaskMemFree(id);
        return s;
    }
}

void SystemLoopbackCapture::run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while(!threadShouldExit())
        wait(captureSession() ? 50 : 1000);   // échec : nouvel essai dans 1 s
    if(started && onStop) onStop();
    started=false; preparedRate=0.0;
    CoUninitialize();
}

bool SystemLoopbackCapture::captureSession()
{
    ComRef<IMMDeviceEnumerator> en;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               __uuidof(IMMDeviceEnumerator), (void**)en.put()))) return false;
    ComRef<IMMDevice> dev;
    if(FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put()))) return false;
    const juce::String devId=endpointId(dev.p);

    {   // nom lisible du périphérique (Options > Audio)
        ComRef<IPropertyStore> ps;
        if(SUCCEEDED(dev->OpenPropertyStore(STGM_READ, ps.put()))){
            PROPVARIANT v; PropVariantInit(&v);
            if(SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt==VT_LPWSTR){
                const juce::ScopedLock sl(nameLock);
                deviceName=juce::String(v.pwszVal);
            }
            PropVariantClear(&v);
        }
    }

    ComRef<IAudioClient> client;
    if(FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)client.put()))) return false;
    WAVEFORMATEX* wfx=nullptr;
    if(FAILED(client->GetMixFormat(&wfx)) || wfx==nullptr) return false;
    struct FreeFmt { WAVEFORMATEX* w; ~FreeFmt(){ CoTaskMemFree(w); } } freeFmt { wfx };

    if(FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 200000 /* 20 ms */, 0, wfx, nullptr))) return false;
    ComRef<IAudioCaptureClient> cap;
    if(FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)cap.put()))) return false;

    // format du mélangeur Windows (le plus souvent flottant 32 bits)
    const int    nch=juce::jmax(1, (int)wfx->nChannels);
    const int    bits=wfx->wBitsPerSample;
    const double sr=(double)wfx->nSamplesPerSec;
    bool isFloat=wfx->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
    if(wfx->wFormatTag==WAVE_FORMAT_EXTENSIBLE)
        isFloat=IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)!=0;
    const int bytesPerSample=bits/8;
    if((isFloat && bits!=32) || (!isFloat && bits!=16 && bits!=24 && bits!=32)) return false;

    if(FAILED(client->Start())) return false;
    sampleRate=sr;
    if(!started || sr!=preparedRate){
        if(onStart) onStart(sr, kBlock);
        started=true; preparedRate=sr;
    }

    std::vector<float> bufL((size_t)kBlock), bufR((size_t)kBlock);
    const float* chans[2] = { bufL.data(), bufR.data() };
    auto deliver=[&](int n){ if(onAudio) onAudio(chans, n); };
    auto sampleAt=[&](const BYTE* frame, int ch)->float{
        const BYTE* s=frame+ch*bytesPerSample;
        if(isFloat)  return *reinterpret_cast<const float*>(s);
        if(bits==16) return (float)*reinterpret_cast<const juce::int16*>(s)/32768.f;
        if(bits==24){ const int v=(int)((juce::uint32)s[0]<<8 | (juce::uint32)s[1]<<16 | (juce::uint32)s[2]<<24)>>8; return (float)v/8388608.f; }
        return (float)((double)*reinterpret_cast<const juce::int32*>(s)/2147483648.0);
    };

    double lastData=juce::Time::getMillisecondCounterHiRes(), lastDevCheck=lastData;
    bool lost=false;
    while(!threadShouldExit() && !lost){
        UINT32 packet=0;
        if(FAILED(cap->GetNextPacketSize(&packet))){ lost=true; break; }
        if(packet==0){
            const double now=juce::Time::getMillisecondCounterHiRes();
            // rien ne joue : silence au rythme réel, les mesures retombent
            if(now-lastData>30.0){
                int n=(int)juce::jmin((now-lastData)*sr*0.001, sr*0.1);
                std::fill(bufL.begin(), bufL.end(), 0.f);
                std::fill(bufR.begin(), bufR.end(), 0.f);
                while(n>0){ const int k=juce::jmin(n, kBlock); deliver(k); n-=k; }
                lastData=now;
            }
            // sortie par défaut changée dans Windows : on rouvre
            if(now-lastDevCheck>1000.0){
                lastDevCheck=now;
                ComRef<IMMDevice> cur;
                if(FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, cur.put())) || endpointId(cur.p)!=devId) break;
            }
            juce::Thread::sleep(5);
            continue;
        }
        while(packet>0){
            BYTE* data=nullptr; UINT32 frames=0; DWORD flags=0;
            if(FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))){ lost=true; break; }
            const bool silent=(flags & AUDCLNT_BUFFERFLAGS_SILENT)!=0 || data==nullptr;
            const int frameBytes=nch*bytesPerSample;
            for(UINT32 pos=0; pos<frames;){
                const int k=(int)juce::jmin<UINT32>(frames-pos, (UINT32)kBlock);
                for(int i=0;i<k;++i){
                    if(silent){ bufL[(size_t)i]=bufR[(size_t)i]=0.f; continue; }
                    const BYTE* f=data+(size_t)(pos+(UINT32)i)*(size_t)frameBytes;
                    bufL[(size_t)i]=sampleAt(f, 0);
                    bufR[(size_t)i]=sampleAt(f, nch>1 ? 1 : 0);   // mono -> L et R
                }
                deliver(k);
                pos+=(UINT32)k;
            }
            cap->ReleaseBuffer(frames);
            if(FAILED(cap->GetNextPacketSize(&packet))){ lost=true; break; }
        }
        lastData=juce::Time::getMillisecondCounterHiRes();
    }
    client->Stop();
    return true;   // nouvelle session aussitôt (nouvelle sortie par défaut / périphérique perdu)
}

//==============================================================================
namespace
{
    // contrôle de volume de la sortie par défaut (COM initialisé au besoin)
    template <typename Fn> bool withEndpointVolume(Fn&& fn)
    {
        const HRESULT co=CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok=false;
        {
            ComRef<IMMDeviceEnumerator> en;
            ComRef<IMMDevice> dev;
            ComRef<IAudioEndpointVolume> vol;
            if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          __uuidof(IMMDeviceEnumerator), (void**)en.put()))
               && SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put()))
               && SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)vol.put())))
                ok=fn(vol.p);
        }
        if(SUCCEEDED(co)) CoUninitialize();
        return ok;
    }
}

float WindowsVolume::get()
{
    float v=-1.f;
    withEndpointVolume([&](IAudioEndpointVolume* vol){ return SUCCEEDED(vol->GetMasterVolumeLevelScalar(&v)); });
    return v;
}

bool WindowsVolume::set(float v)
{
    return withEndpointVolume([&](IAudioEndpointVolume* vol){
        return SUCCEEDED(vol->SetMasterVolumeLevelScalar(juce::jlimit(0.f, 1.f, v), nullptr));
    });
}

//==============================================================================
bool WindowsDisplay::waitForNextFrame()
{
    return SUCCEEDED(DwmFlush());
}

void WindowsDisplay::setNoActivate(void* nativeHandle, bool noActivate)
{
    auto hwnd=static_cast<HWND>(nativeHandle);
    if(hwnd==nullptr) return;
    const LONG_PTR ex=GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // APPWINDOW : garder le bouton de la barre des tâches
    const LONG_PTR want=(noActivate ? (ex | WS_EX_NOACTIVATE) : (ex & ~(LONG_PTR)WS_EX_NOACTIVATE)) | WS_EX_APPWINDOW;
    if(want!=ex) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, want);
}

void* WindowsDisplay::foregroundWindow() { return GetForegroundWindow(); }

void WindowsDisplay::activate(void* nativeHandle)
{
    auto hwnd=static_cast<HWND>(nativeHandle);
    if(hwnd!=nullptr && IsWindow(hwnd)) SetForegroundWindow(hwnd);
}

//==============================================================================
juce::String CrashReport::describe(void* info)
{
    auto* ep=static_cast<EXCEPTION_POINTERS*>(info);
    if(ep==nullptr || ep->ExceptionRecord==nullptr || ep->ContextRecord==nullptr) return {};

    juce::String s;
    s << "Exception 0x" << juce::String::toHexString((int)ep->ExceptionRecord->ExceptionCode) << "\n";

    const HANDLE proc=GetCurrentProcess(), thread=GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);   // peut déjà l'être : sans conséquence

    CONTEXT ctx=*ep->ContextRecord;
    STACKFRAME64 f {};
    f.AddrPC.Offset=ctx.Rip;    f.AddrPC.Mode=AddrModeFlat;
    f.AddrFrame.Offset=ctx.Rbp; f.AddrFrame.Mode=AddrModeFlat;
    f.AddrStack.Offset=ctx.Rsp; f.AddrStack.Mode=AddrModeFlat;
    for(int i=0;i<48;++i){
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &f, &ctx, nullptr,
                        SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || f.AddrPC.Offset==0) break;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO)+256] {};
        auto* sym=reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct=sizeof(SYMBOL_INFO); sym->MaxNameLen=255;
        DWORD64 disp=0;
        s << "  " << i << ": ";
        if(SymFromAddr(proc, f.AddrPC.Offset, &disp, sym)) s << sym->Name << " + 0x" << juce::String::toHexString((juce::int64)disp);
        else                                               s << "0x" << juce::String::toHexString((juce::int64)f.AddrPC.Offset);
        IMAGEHLP_LINE64 line {}; line.SizeOfStruct=sizeof(line);
        DWORD lineDisp=0;
        if(SymGetLineFromAddr64(proc, f.AddrPC.Offset, &lineDisp, &line) && line.FileName!=nullptr)
            s << "  (" << juce::File(line.FileName).getFileName() << ":" << (int)line.LineNumber << ")";
        s << "\n";
    }
    return s;
}
#endif

//==============================================================================
// Mac : volume général de la sortie par défaut (curseur de la barre des menus)
#if JUCE_MAC
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioServices.h>

namespace
{
    AudioObjectID defaultOutputDevice()
    {
        AudioObjectID id = kAudioObjectUnknown;
        UInt32 size = sizeof (id);
        const AudioObjectPropertyAddress a { kAudioHardwarePropertyDefaultOutputDevice,
                                             kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &a, 0, nullptr, &size, &id) != noErr)
            return kAudioObjectUnknown;
        return id;
    }

    const AudioObjectPropertyAddress mainVolume { kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
                                                  kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain };
}

float WindowsVolume::get()
{
    const auto id = defaultOutputDevice();
    if (id == kAudioObjectUnknown || ! AudioObjectHasProperty (id, &mainVolume))
        return -1.f;
    Float32 v = 0;
    UInt32 size = sizeof (v);
    if (AudioObjectGetPropertyData (id, &mainVolume, 0, nullptr, &size, &v) != noErr)
        return -1.f;
    return (float) v;
}

bool WindowsVolume::set (float v)
{
    const auto id = defaultOutputDevice();
    if (id == kAudioObjectUnknown)
        return false;
    Float32 f = juce::jlimit (0.f, 1.f, v);
    return AudioObjectSetPropertyData (id, &mainVolume, 0, nullptr, sizeof (f), &f) == noErr;
}
#endif

#if ! JUCE_WINDOWS
void SystemLoopbackCapture::run() {}
bool SystemLoopbackCapture::captureSession() { return false; }
#if ! JUCE_MAC
float WindowsVolume::get() { return -1.f; }
bool  WindowsVolume::set(float) { return false; }
#endif
juce::String CrashReport::describe(void*) { return {}; }
bool WindowsDisplay::waitForNextFrame() { return false; }
void WindowsDisplay::setNoActivate(void*, bool) {}
void* WindowsDisplay::foregroundWindow() { return nullptr; }
void WindowsDisplay::activate(void*) {}
#endif
