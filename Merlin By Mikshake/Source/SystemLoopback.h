#pragma once
#include <JuceHeader.h>
#include <functional>
#include <atomic>

//==============================================================================
// SystemLoopbackCapture — capture de ce qui joue sur le PC (WASAPI "loopback"
// du périphérique de lecture par défaut de Windows), pour analyser sans carte
// son externe (ou sans son loopback matériel).
//
// Thread dédié : ouvre la sortie par défaut en mode loopback partagé, convertit
// au besoin en flottant stéréo et livre des blocs via onAudio. Quand rien ne
// joue, Windows ne livre rien : on envoie alors du silence au rythme réel pour
// que les mesures retombent. Si la sortie par défaut change ou disparaît, la
// capture se rouvre toute seule.
//==============================================================================
class SystemLoopbackCapture : private juce::Thread
{
public:
    SystemLoopbackCapture() : juce::Thread("Merlin system loopback") {}
    ~SystemLoopbackCapture() override { stop(); }

    void start() { if(!isThreadRunning()) startThread(juce::Thread::Priority::highest); }
    void stop()  { stopThread(3000); }
    bool isRunning() const { return isThreadRunning(); }

    juce::String getDeviceName() const { const juce::ScopedLock sl(nameLock); return deviceName; }
    double getSampleRate() const       { return sampleRate.load(); }

    // Appelés depuis le thread de capture
    std::function<void(double sampleRate, int maxBlock)>   onStart;
    std::function<void(const float* const* stereo, int n)> onAudio;
    std::function<void()>                                   onStop;

    static constexpr int kBlock = 1024;

private:
    void run() override;
    bool captureSession();   // une session sur la sortie par défaut ; false = échec

    juce::CriticalSection nameLock;
    juce::String deviceName;
    std::atomic<double> sampleRate { 0.0 };
    double preparedRate { 0.0 };
    bool   started { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SystemLoopbackCapture)
};

//==============================================================================
// Volume général de la sortie Windows par défaut (le curseur de la barre des
// tâches), réglable depuis Merlin. Appels depuis le thread de messages.
//==============================================================================
namespace WindowsVolume
{
    float get();          // 0..1, -1 si aucune sortie
    bool  set(float v);   // 0..1
}

//==============================================================================
// Journal de plantage : code de l'exception + pile d'appels reconstruite depuis
// le contexte de l'exception (fichier:ligne quand le PDB est là). Plus fiable
// que la pile capturée dans le gestionnaire, qui perd les cadres fautifs.
//==============================================================================
// Attend la prochaine composition de l'écran par Windows (DwmFlush) : sert
// d'horloge d'images aux modules GPU, sans passer par le thread de messages.
namespace WindowsDisplay
{
    bool waitForNextFrame();   // false : pas de composition (attendre ~16 ms soi-même)
    // Fenêtre qui ne s'active pas au clic (noActivate) : le clavier reste à
    // l'appli active (ex. Ableton joué au clavier du PC). Idempotent.
    void setNoActivate(void* nativeHandle, bool noActivate);
    void* foregroundWindow();          // fenêtre au premier plan (HWND)
    void  activate(void* nativeHandle); // lui rendre le premier plan
}

namespace CrashReport
{
    juce::String describe(void* exceptionPointers);   // EXCEPTION_POINTERS* (Windows)
}
