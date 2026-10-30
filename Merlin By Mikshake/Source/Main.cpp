/*
    Merlin Alpha (affiché « Merlin α ») — application autonome (pas un plugin)
    ---------------------------------------------------------
    Héberge le moteur d'analyse (LeakAudioProcessor) et l'éditeur de
    Merlin MTR 2 tels quels, alimentés directement par une carte son via
    AudioDeviceManager + AudioProcessorPlayer.

    Usage visé : mesurer la sortie directe de la carte (ex. "Loopback 1 L/R"
    de la SSL 1, qui reprend la sortie stéréo principale, DAW en ASIO comprise).
    La carte est ouverte en ENTRÉE SEULEMENT (0 sortie) : l'appli ne renvoie
    jamais de son, ce qui évite tout slarsen avec le loopback.

    WASAPI PARTAGÉ UNIQUEMENT : le pilote ASIO de la SSL 1 n'accepte qu'un
    seul logiciel — l'ouvrir ici coupait celui d'Ableton. Le loopback est aussi
    exposé à Windows comme périphérique d'enregistrement, qu'on lit en mode
    partagé à côté de l'ASIO de la DAW. Pas d'ASIO ni de WASAPI exclusif.

    Menu "Audio" : choix du périphérique d'entrée analysé. Menu "Affichage" :
    plein écran (F11, Échap pour sortir) mis à l'échelle selon l'écran,
    "Toujours au premier plan".
    L'état du processeur (modules, couleurs, taille…) et la config audio sont
    sauvegardés dans %APPDATA%\Side Key System\Merlin Alpha.settings.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SystemLoopback.h"

#if JUCE_WINDOWS
// PC à double carte graphique (portables NVIDIA Optimus / AMD) : les rendus
// OpenGL passent par la carte dédiée plutôt que par la puce intégrée.
extern "C"
{
    __declspec (dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec (dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace
{
    constexpr int numInputs  = 2;   // paire stéréo analysée
    constexpr int numOutputs = 0;   // jamais de sortie audio


    // Ne propose que "Windows Audio" (WASAPI partagé) : jamais de prise
    // exclusive de la carte, la DAW garde son ASIO. Sur Mac, CoreAudio est
    // partagé par nature : la DAW et Merlin lisent la carte en même temps.
    class SharedAudioDeviceManager final : public juce::AudioDeviceManager
    {
        void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
        {
           #if JUCE_WINDOWS
            if (auto* t = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared))
                types.add (t);
           #elif JUCE_MAC
            if (auto* t = juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio())
                types.add (t);
           #endif
        }
    };

   #if JUCE_WINDOWS
    constexpr const char* sharedDeviceType = "Windows Audio";
   #else
    constexpr const char* sharedDeviceType = "CoreAudio";
   #endif

    // Remplace AudioProcessorPlayer : sans sortie, le player copie l'entrée
    // dans un tampon interne dimensionné au buffer annoncé par la carte, et ne
    // le vérifie qu'en jassert (rien en Release). Ici on découpe toujours en
    // blocs <= taille préparée, quoi que livre le pilote.
    class AnalyserCallback final : public juce::AudioIODeviceCallback
    {
    public:
        explicit AnalyserCallback (juce::AudioProcessor& p) : processor (p) {}

        std::atomic<uint32_t> callbackCount { 0 };

        void audioDeviceAboutToStart (juce::AudioIODevice* device) override
        {
            prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        }

        void audioDeviceStopped() override   { stop(); }

        void audioDeviceIOCallbackWithContext (const float* const* ins, int numIns,
                                               float* const* outs, int numOuts, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override
        {
            for (int i = 0; i < numOuts; ++i)
                if (outs[i] != nullptr)
                    juce::FloatVectorOperations::clear (outs[i], numSamples);

            callbackCount.fetch_add (1, std::memory_order_relaxed);
            process (ins, numIns, numSamples);
        }

        // Aussi utilisés par la capture de la sortie Windows (sans carte son)
        void prepare (double sr, int blockSize)
        {
            const auto bs = juce::jmax (64, blockSize);
            const juce::ScopedLock sl (processor.getCallbackLock());
            if (prepared)
                processor.releaseResources();

            buffer.setSize (2, bs);
            processor.setRateAndBufferSizeDetails (sr, bs);
            processor.prepareToPlay (sr, bs);
            prepared = true;
        }

        void stop()
        {
            const juce::ScopedLock sl (processor.getCallbackLock());
            if (prepared)
                processor.releaseResources();
            prepared = false;
        }

        void process (const float* const* ins, int numIns, int numSamples)
        {
            const juce::ScopedLock sl (processor.getCallbackLock());
            if (! prepared || numIns <= 0 || ins == nullptr)
                return;

            const int maxBlock = buffer.getNumSamples();
            for (int pos = 0; pos < numSamples; pos += maxBlock)
            {
                const int n = juce::jmin (maxBlock, numSamples - pos);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* src = ins[juce::jmin (ch, numIns - 1)];   // mono -> L et R
                    if (src != nullptr)
                        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), src + pos, n);
                    else
                        juce::FloatVectorOperations::clear (buffer.getWritePointer (ch), n);
                }

                juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, n);
                processor.processBlock (block, midi);
            }
        }

    private:
        juce::AudioProcessor& processor;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        bool prepared = false;
    };

    // Fenêtre plein écran : fenêtre de bureau sans bordure, distincte de la
    // fenêtre principale (masquée pendant ce temps). On n'utilise PAS le mode
    // kiosque de JUCE : sous Windows il se désactive dès que l'appli perd le
    // focus (clic sur un autre écran). Sur Mac il ne sert qu'à masquer la barre
    // de menus et le Dock, qui passeraient sinon devant l'en-tête.
    // L'éditeur y garde une taille "de conception" proche du 1080p et est
    // agrandi par transformation — même mise en page sur un écran 1080p ou 4K
    // (rendu net, JUCE dessine à la résolution physique). En dessous de 1080
    // lignes, il prend simplement toute la surface.
    class FullScreenWindow final : public juce::Component
    {
    public:
        static constexpr float referenceHeight = 1080.f;
        static constexpr float maxDesignWidth  = 2400.f;   // = largeur max de l'éditeur

        std::function<void()> onExitRequested;    // F11 / Échap
        std::function<void()> onCloseRequested;   // Alt+F4
        float fixedScale = 0.f;   // Options › Taille de l'interface fixe (0 : Auto, d'après la taille de l'écran)

        FullScreenWindow()
        {
            setOpaque (true);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (false);   // un clic ne prend pas le clavier à Ableton
        }

        void open (juce::Rectangle<int> screenArea, bool alwaysOnTop)
        {
            setBounds (screenArea);
            addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
            if (auto* p = getPeer())
                WindowsDisplay::setNoActivate (p->getNativeHandle(), ! keyboard);
            setMouseClickGrabsKeyboardFocus (keyboard);
            setAlwaysOnTop (alwaysOnTop);
            setVisible (true);
            toFront (keyboard);
            if (keyboard)
                grabKeyboardFocus();
           #if JUCE_MAC
            juce::Desktop::getInstance().setKioskModeComponent (this, false);
           #endif
        }

        bool keyboard = false;   // bouton clavier de l'en-tête allumé : la fenêtre prend le clavier

        void close()
        {
           #if JUCE_MAC
            juce::Desktop::getInstance().setKioskModeComponent (nullptr, false);
           #endif
            setEditor (nullptr);
            setVisible (false);
            removeFromDesktop();
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::F11Key || key == juce::KeyPress::escapeKey)
            {
                if (onExitRequested)
                    onExitRequested();
                return true;
            }
            return false;
        }

        void userTriedToCloseWindow() override
        {
            if (onCloseRequested)
                onCloseRequested();
        }

        void setEditor (juce::Component* newEditor)
        {
            if (editor != nullptr)
            {
                editor->setTransform ({});
                removeChildComponent (editor);
            }

            editor = newEditor;

            if (editor != nullptr)
            {
                addAndMakeVisible (editor);
                resized();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setGradientFill (Neu::plateFace (getLocalBounds().toFloat()));   // la plaque continue sous les marges
            g.fillAll();
        }

        void resized() override
        {
            if (editor == nullptr || getWidth() <= 0 || getHeight() <= 0)
                return;

            const float scale = fixedScale > 0.f ? fixedScale
                                                 : juce::jmax (1.f, (float) getHeight() / referenceHeight,
                                                                    (float) getWidth()  / maxDesignWidth);
            // marges de sécurité : sans barre de fenêtre, l'en-tête toucherait le
            // haut de l'écran (le bas a déjà la marge de la bande de contrôles)
            const int top = juce::roundToInt ((float) getHeight() * 0.011f), bottom = juce::roundToInt ((float) getHeight() * 0.005f);
            editor->setTransform ({});
            editor->setBounds (0, 0, juce::roundToInt ((float) getWidth()  / scale),
                                     juce::roundToInt ((float) (getHeight() - top - bottom) / scale));
            editor->setTransform (juce::AffineTransform::scale (scale).translated (0.f, (float) top));
        }

    private:
        juce::Component* editor = nullptr;
    };

    // Contenu de la fenêtre normale (v1.72) : comme en plein écran, l'éditeur y
    // est agrandi d'un bloc (Options › Taille de l'interface) — même mise en
    // page à toutes les tailles, rendu net à la résolution physique.
    class ScaledHolder final : public juce::Component
    {
    public:
        float scale = 1.f;

        ScaledHolder() { setOpaque (true); }

        void setEditor (juce::Component* newEditor)
        {
            if (editor != nullptr)
            {
                editor->setTransform ({});
                removeChildComponent (editor);
            }
            editor = newEditor;
            if (editor != nullptr)
            {
                addAndMakeVisible (editor);
                resized();
            }
        }

        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

        void resized() override
        {
            if (editor == nullptr || getWidth() <= 0 || getHeight() <= 0)
                return;
            editor->setTransform ({});
            editor->setBounds (0, 0, juce::roundToInt ((float) getWidth()  / scale),
                                     juce::roundToInt ((float) getHeight() / scale));
            editor->setTransform (juce::AffineTransform::scale (scale));
        }

    private:
        juce::Component* editor = nullptr;
    };

    // Journal de plantage : pile d'appels symbolisée (PDB à côté de l'exe)
    // ajoutée à %APPDATA%\Side Key System\Merlin Alpha crash.log
    juce::File crashLogFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Side Key System")
                   .getChildFile ("Merlin Alpha crash.log");
    }

    void writeCrashLog (void* exceptionPointers)
    {
        juce::String report;
        report << "==== " << juce::Time::getCurrentTime().toString (true, true, true, true)
               << "  v" << ProjectInfo::versionString << "\n";

        if (auto* t = juce::Thread::getCurrentThread())
            report << "Thread : " << t->getThreadName() << "\n";
        else
            report << "Thread : (non-JUCE / message thread)\n";

        report << CrashReport::describe (exceptionPointers);   // pile au point fautif (fichier:ligne)
        report << "Pile du gestionnaire :\n" << juce::SystemStats::getStackBacktrace() << "\n";
        crashLogFile().appendText (report);
    }
}

//==============================================================================
// Nom affiché (barre de titre, barre des tâches). Le nom technique (exe,
// réglages) est ProjectInfo::projectName = "Merlin Alpha".
static juce::String merlinDisplayName() { return juce::String (juce::CharPointer_UTF8 ("Merlin \xce\xb1")); }

class MainWindow final : public juce::DocumentWindow,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MainWindow (juce::PropertiesFile& settingsToUse)
        : DocumentWindow (merlinDisplayName(),
                          juce::Colours::black,
                          DocumentWindow::allButtons),
          settings (settingsToUse)
    {
        // 1) état du processeur AVANT l'éditeur (taille, modules, couleurs…)
        juce::MemoryBlock state;
        if (state.fromBase64Encoding (settings.getValue ("processorState")) && state.getSize() > 0)
            processor.setStateInformation (state.getData(), (int) state.getSize());

        // v1.1 : le VU à aiguille remplace Panorama dans le slot C par défaut —
        // appliqué une seule fois aux dispositions sauvegardées par la v1.0.
        if (settings.getIntValue ("layoutRevision", 0) < 1)
        {
            if (processor.slotModule[2] == (int) LeakAudioProcessor::ModuleID::Multipanorama)
                processor.slotModule[2] = (int) LeakAudioProcessor::ModuleID::NeedleVU;
            settings.setValue ("layoutRevision", 1);
        }

        // 2) carte son, ou sortie de Windows s'il n'y a pas de carte (loopback)
        initialiseAudio();
        deviceManager.addAudioCallback (&analyser);
        deviceManager.addChangeListener (this);
        systemCapture.onStart = [this] (double sr, int bs)                 { analyser.prepare (sr, bs); };
        systemCapture.onAudio = [this] (const float* const* st, int n)     { analyser.process (st, 2, n); };
        systemCapture.onStop  = [this]                                     { analyser.stop(); };
        audioSource = juce::jlimit (0, 2, settings.getIntValue ("audioSource", sourceAuto));
        applyAudioSource();
        startTimer (1000);   // chien de garde du flux audio + détection de la carte

        // 3) fenêtre
        barLook->install (*this);
        editor.reset (processor.createEditorAndMakeActive());
        // l'éditeur vit dans windowHolder, à l'échelle (Options › Taille de
        // l'interface) ; la fenêtre se redimensionne par ses bords
        editor->setResizable (false, false);
        const juce::Point<int> designSize { editor->getWidth(), editor->getHeight() };
        windowHolder.setEditor (editor.get());
        windowHolder.setSize (designSize.x, designSize.y);
        setContentNonOwned (&windowHolder, true);
        // cadre Windows redimensionnable : bouton Agrandir, et la fenêtre
        // glissée en haut de l'écran s'agrandit (Aero Snap), comme un navigateur
        setResizable (true, false);
        applyMinSize();
        // par défaut, un clic sur Merlin ne lui donne pas le clavier : Ableton,
        // joué au clavier du PC, continue de recevoir les touches (la fenêtre
        // passe quand même devant). Bouton clavier de l'en-tête pour l'inverser.
        keyboardInMerlin = settings.getBoolValue ("keyboardInMerlin", false);
        fullScreenWindow.keyboard = keyboardInMerlin;
        addMouseListener (&menuFocus, true);
        fullScreenWindow.addMouseListener (&menuFocus, true);
        PopupFocus::beforePopup() = [this] { menuFocus.begin(); };
        wireEditor();

        alwaysOnTop = settings.getBoolValue ("alwaysOnTop", false);
        setAlwaysOnTop (alwaysOnTop);
        if (auto* leak = dynamic_cast<LeakEditor*> (editor.get()))
            leak->setAlwaysOnTopState (alwaysOnTop);

        const auto pos = settings.getValue ("windowPos");
        if (pos.isNotEmpty())
            restoreWindowStateFromString (pos);
        // échelle de l'écran où la fenêtre se trouve : l'éditeur garde sa taille de
        // conception mémorisée, la fenêtre en est l'agrandissement
        applyUiScale (designSize);
        if (pos.isEmpty())
            centreWithSize (getWidth(), getHeight());

        fullScreenWindow.onExitRequested  = [this] { setFullScreenMode (false); };
        fullScreenWindow.onCloseRequested = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };

        fullScreenWindow.setName (merlinDisplayName());   // titre dans la barre des tâches

        // la fenêtre est déjà sur le bureau (masquée) : son écran est connu
        if (settings.getBoolValue ("fullScreen", false))
            setFullScreenMode (true);
        else
            setVisible (true);

        applyMinSize();   // cadre Windows connu maintenant
        applyNoActivate();
    }

    // Taille mini du CONTENU : 1024×576 (en dessous, en gros texte, les en-têtes et la
    // barre du bas se chevauchent). Avec la barre de titre Windows, JUCE applique la
    // limite au cadre entier : on y ajoute donc le cadre, connu une fois la fenêtre affichée.
    void applyMinSize()
    {
        auto w = juce::roundToInt (1024.f * windowHolder.scale), h = juce::roundToInt (576.f * windowHolder.scale);
        if (auto* peer = getPeer())
            if (const auto frame = peer->getFrameSizeIfPresent())
            {
                w += frame->getLeftAndRight();
                h += frame->getTopAndBottom();
            }
        // barre Merlin et bord : ils font partie de la fenêtre, donc de la limite
        const auto border = getContentComponentBorder();
        w += border.getLeftAndRight();
        h += border.getTopAndBottom();
        // fenêtre masquée (plein écran) : limites enregistrées sans repositionner
        // la fenêtre — setResizeLimits la recalait, et une fenêtre mémorisée
        // agrandie était alors restaurée et RÉAFFICHÉE, vide et noire, derrière
        // le plein écran (v1.59 → v1.69.1)
        if (! isVisible())
        {
            if (auto* c = getConstrainer())
            {
                c->setSizeLimits (w, h, 16384, 16384);
                return;
            }
        }
        setResizeLimits (w, h, 16384, 16384);
    }

    // Taille de l'interface (au lancement, Options, écran changé) : l'éditeur garde
    // sa taille de conception, la fenêtre en est l'agrandissement — l'échelle est
    // bornée pour que la taille mini (1024×576 × échelle) tienne dans l'écran
    void applyUiScale (juce::Point<int> designSize)
    {
        const juce::ScopedValueSetter<bool> busy (applyingUiScale, true);
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (fullScreen)
        {
            fullScreenWindow.fixedScale = processor.uiScale > 0 ? (float) processor.uiScale / 100.f : 0.f;
            fullScreenWindow.resized();
            return;
        }
        const auto* d = displays.getDisplayForRect (getScreenBounds());
        float s = Ui::uiScaleFor (processor.uiScale, d);
        if (d != nullptr)
            s = juce::jmax (0.5f, juce::jmin (s, d->userBounds.getWidth() / 1040.f, d->userBounds.getHeight() / 620.f));
        scaleDisplayArea = d != nullptr ? d->logicalBounds.getSmallestIntegerContainer() : juce::Rectangle<int>();
        windowHolder.scale = s;
        applyMinSize();
        if (! isFullScreen())   // fenêtre agrandie (bouton Agrandir) : elle garde sa taille
        {
            auto w = juce::roundToInt ((float) designSize.x * s), h = juce::roundToInt ((float) designSize.y * s);
            if (d != nullptr)
            {
                w = juce::jmin (w, juce::roundToInt (d->userBounds.getWidth()));
                h = juce::jmin (h, juce::roundToInt (d->userBounds.getHeight()) - 40);   // barre de titre
            }
            setContentComponentSize (w, h);
        }
        windowHolder.resized();
    }

    // fenêtre passée sur un autre écran : l'échelle Auto le suit
    void moved() override
    {
        DocumentWindow::moved();
        if (applyingUiScale || fullScreen || editor == nullptr || processor.uiScale != 0)
            return;
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            if (d->logicalBounds.getSmallestIntegerContainer() != scaleDisplayArea)
                applyUiScale ({ editor->getWidth(), editor->getHeight() });
    }

    // Merlin relancé alors qu'il tourne déjà : on ramène la fenêtre existante
    void bringToFront()
    {
        if (fullScreen)
        {
            fullScreenWindow.toFront (true);
            return;
        }
        if (isMinimised())
            setMinimised (false);
        if (auto* p = getPeer())
            WindowsDisplay::activate (p->getNativeHandle());
        toFront (true);
    }

    ~MainWindow() override
    {
        stopTimer();
        PopupFocus::beforePopup() = nullptr;
        removeMouseListener (&menuFocus);
        fullScreenWindow.removeMouseListener (&menuFocus);
        systemCapture.stop();
        saveState();

        if (fullScreen)
            fullScreenWindow.close();

        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (&analyser);
        clearContentComponent();
        windowHolder.setEditor (nullptr);
        editor = nullptr;
        setLookAndFeel (nullptr);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    // F11 : passe en plein écran (la sortie est gérée par FullScreenWindow).
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::F11Key)
        {
            setFullScreenMode (true);
            return true;
        }

        return false;
    }

    void saveState()
    {
        // en plein écran, on mémorise la taille et la position FENÊTRÉES
        if (fullScreen)
        {
            processor.editorWidth  = windowedEditorSize.x;
            processor.editorHeight = windowedEditorSize.y;
        }

        juce::MemoryBlock state;
        processor.getStateInformation (state);
        settings.setValue ("processorState", state.toBase64Encoding());

        if (auto xml = deviceManager.createStateXml())
            settings.setValue ("audioDeviceState", xml.get());

        settings.setValue ("windowPos", fullScreen ? windowedState : getWindowStateAsString());
        settings.setValue ("alwaysOnTop", alwaysOnTop);
        settings.setValue ("fullScreen", fullScreen);
        settings.setValue ("audioSource", audioSource);
        settings.saveIfNeeded();
    }

private:
    // Branche l'éditeur sur la fenêtre : plein écran et fonctions reprises par
    // son menu Options. La bascule plein écran (qui déplace l'éditeur) est
    // différée, jamais pendant son propre clic.
    void wireEditor()
    {
        auto* leak = dynamic_cast<LeakEditor*> (editor.get());
        if (leak == nullptr)
            return;

        juce::Component::SafePointer<MainWindow> sp (this);
        leak->onToggleFullScreen = [sp]
        {
            juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->setFullScreenMode (! sp->fullScreen); });
        };

        auto& h = leak->host;
        h.getAudioSource    = [this] { return audioSource; };
        h.setAudioSource    = [this] (int s) { selectAudioSource (s); };
        h.getSourceName     = [this] { return currentSourceName(); };
        h.holdKeyboard      = [this] (bool on) { holdKeyboardForLicence (on); };
        // écran de licence déjà ouvert au démarrage (avant ce branchement)
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<LeakEditor> (leak), this]
        {
            if (sp != nullptr && sp->isLicenceOpen())
                holdKeyboardForLicence (true);
        });
        h.openAudioSettings = [this] { if (! usingSystem) showAudioSettings(); };
        h.isFullScreen      = [this] { return fullScreen; };
        h.isAlwaysOnTop     = [this] { return alwaysOnTop; };
        h.setAlwaysOnTop    = [this] (bool on) { if (on != alwaysOnTop) toggleAlwaysOnTop(); };
        h.setKeyboardInMerlin = [this] (bool on) { setKeyboardInMerlin (on); };
        h.uiScaleChanged    = [this] { if (editor != nullptr) applyUiScale (fullScreen ? windowedEditorSize
                                                                                      : juce::Point<int> { editor->getWidth(), editor->getHeight() }); };

        leak->setFullScreenState (fullScreen);
        leak->setKeyboardState (keyboardInMerlin);
        leak->setAlwaysOnTopState (alwaysOnTop);
    }

    void selectAudioSource (int s)
    {
        audioSource = juce::jlimit ((int) sourceAuto, (int) sourceSystem, s);
        settings.setValue ("audioSource", audioSource);
        applyAudioSource();
    }

    void toggleAlwaysOnTop()
    {
        alwaysOnTop = ! alwaysOnTop;
        setAlwaysOnTop (alwaysOnTop);
        if (auto* leak = dynamic_cast<LeakEditor*> (editor.get()))
            leak->setAlwaysOnTopState (alwaysOnTop);   // bouton de l'en-tête (Options peut aussi le changer)
        if (fullScreen)
            fullScreenWindow.setAlwaysOnTop (alwaysOnTop);
        settings.setValue ("alwaysOnTop", alwaysOnTop);
    }

    // Plein écran sur l'écran où se trouve la fenêtre principale : l'éditeur
    // passe dans FullScreenWindow et la fenêtre principale est masquée.
    void setFullScreenMode (bool shouldBeFullScreen)
    {
        if (shouldBeFullScreen == fullScreen || editor == nullptr)
            return;

        fullScreen = shouldBeFullScreen;

        // Les modules GPU lâchent leur contexte AVANT le transfert de l'éditeur
        // (sinon JUCE le détruit et le recrée en plein vol, pendant que le fil de
        // cadence déclenche des images : modules noirs / vides, clignotement) et
        // ne le reprennent qu'une fois la nouvelle fenêtre affichée et peinte.
        auto* leak = dynamic_cast<LeakEditor*> (editor.get());
        if (leak != nullptr)
            leak->beginWindowSwap();

        if (fullScreen)
        {
            windowedState      = getWindowStateAsString();
            windowedEditorSize = { editor->getWidth(), editor->getHeight() };

            const auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto* display  = displays.getDisplayForRect (getScreenBounds());
            if (display == nullptr)
                display = displays.getPrimaryDisplay();

            clearContentComponent();
            windowHolder.setEditor (nullptr);
            fullScreenWindow.fixedScale = processor.uiScale > 0 ? (float) processor.uiScale / 100.f : 0.f;
            fullScreenWindow.setEditor (editor.get());
            fullScreenWindow.open (display != nullptr ? display->logicalBounds.getSmallestIntegerContainer()
                                                   : getScreenBounds(), alwaysOnTop);
            setVisible (false);
        }
        else
        {
            fullScreenWindow.close();
            windowHolder.setEditor (editor.get());
            setContentNonOwned (&windowHolder, true);
            restoreWindowStateFromString (windowedState);
            applyUiScale (windowedEditorSize);   // taille de conception d'avant le plein écran
            setVisible (true);
            applyMinSize();
            applyNoActivate();
            toFront (false);
        }

        if (leak != nullptr)
        {
            leak->setFullScreenState (fullScreen);
            juce::Timer::callAfterDelay (120, [sp = juce::Component::SafePointer<LeakEditor> (leak)]
            {
                if (sp != nullptr)
                    sp->endWindowSwap();
            });
        }

        settings.setValue ("fullScreen", fullScreen);
    }

    void initialiseAudio()
    {
        auto saved = settings.getXmlValue ("audioDeviceState");

        // un état d'une autre famille de pilote (ex. ancien ASIO) est ignoré
        if (saved != nullptr && saved->getStringAttribute ("deviceType") != sharedDeviceType)
            saved = nullptr;

        deviceManager.initialise (numInputs, numOutputs, saved.get(), true);

        if (saved == nullptr)
            preferLoopbackInput();
    }

    // Une carte avec entrée "Loopback" (SSL 1…) est-elle branchée ?
    bool cardLoopbackAvailable()
    {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return false;
        // pas de scanForDevices() ici : JUCE tient déjà la liste à jour (il
        // surveille les branchements) et un nouveau scan WASAPI peut bloquer
        // longtemps le thread de l'interface quand la carte est occupée
        for (const auto& name : type->getDeviceNames (true))
            if (name.containsIgnoreCase ("Loopback"))
                return true;
        return false;
    }

    // Source analysée : entrée de la carte (WASAPI partagé) ou sortie de
    // Windows (capture loopback de ce qui joue sur le PC). Automatique = la
    // carte si elle est là, sinon la sortie Windows ; jamais les deux à la fois.
    void applyAudioSource()
    {
       #if JUCE_WINDOWS
        const bool wantSystem = audioSource == sourceSystem
                             || (audioSource == sourceAuto && ! cardLoopbackAvailable());
       #else
        const bool wantSystem = false;   // Mac : pas encore de capture de la sortie système, toujours la carte
       #endif

        if (wantSystem)
        {
            if (! usingSystem || ! systemCapture.isRunning())
            {
                deviceManager.closeAudioDevice();
                systemCapture.start();
            }
        }
        else if (usingSystem || deviceManager.getCurrentAudioDevice() == nullptr)
        {
            systemCapture.stop();
            deviceManager.restartLastAudioDevice();
            if (deviceManager.getCurrentAudioDevice() == nullptr)
                initialiseAudio();
        }

        if (wantSystem != usingSystem)
        {
            usingSystem = wantSystem;
            stalledSeconds = 0;
        }
    }

    juce::String currentSourceName()
    {
        if (usingSystem)
        {
            const auto n = systemCapture.getDeviceName();
            return TR ("Sortie Windows") + (n.isNotEmpty() ? " : " + n : juce::String());
        }
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            return dev->getName();
        return TR ("aucune entrée ouverte");
    }

    // Premier lancement : prend le loopback 1 de la carte (sortie principale),
    // à défaut n'importe quel périphérique "Loopback".
    void preferLoopbackInput()
    {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return;

        type->scanForDevices();
        const auto inputs = type->getDeviceNames (true);

        juce::String chosen;
        for (const auto& name : inputs)
            if (name.containsIgnoreCase ("Loopback 1"))  { chosen = name; break; }

        if (chosen.isEmpty())
            for (const auto& name : inputs)
                if (name.containsIgnoreCase ("Loopback"))  { chosen = name; break; }

        if (chosen.isNotEmpty())
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup (setup);
            setup.inputDeviceName          = chosen;
            setup.outputDeviceName         = {};
            setup.useDefaultInputChannels  = true;
            setup.useDefaultOutputChannels = false;
            setup.outputChannels.clear();
            deviceManager.setAudioDeviceSetup (setup, true);
        }

        selectLoopbackChannels();
    }

    // Carte dont le loopback est une paire de voies d'une entrée multicanale
    // (SSL et d'autres sur Mac, certaines cartes sous Windows) : on analyse
    // cette paire plutôt que les entrées 1-2.
    void selectLoopbackChannels()
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (dev == nullptr)
            return;

        const auto names = dev->getInputChannelNames();
        int first = -1;
        for (int i = 0; i < names.size(); ++i)
            if (names[i].containsIgnoreCase ("Loopback")) { first = i; break; }

        if (first < 0)
            return;

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setBit (first);
        if (first + 1 < names.size() && names[first + 1].containsIgnoreCase ("Loopback"))
            setup.inputChannels.setBit (first + 1);
        deviceManager.setAudioDeviceSetup (setup, true);
    }

    // Si le pilote suspend le flux Windows (ex. quand la DAW prend l'ASIO),
    // le thread WASAPI de JUCE sort au bout d'1 s sans prévenir : plus aucun
    // callback. Après 3 s sans callback, on rouvre le périphérique.
    void timerCallback() override
    {
        applyNoActivate();   // la fenêtre native peut être recréée par JUCE

        // Automatique : bascule carte <-> sortie Windows quand la carte
        // (son entrée "Loopback") apparaît ou disparaît
        if (audioSource == sourceAuto && ++autoCheckSeconds >= 3)
        {
            autoCheckSeconds = 0;
            applyAudioSource();
        }
        if (usingSystem)
            return;

        auto* dev = deviceManager.getCurrentAudioDevice();
        if (dev == nullptr)
        {
            // ouverture ratée (carte en plein changement de fréquence) : on réessaie
            if (++stalledSeconds >= 3)
            {
                stalledSeconds = 0;
                if (deviceManager.getAudioDeviceSetup().inputDeviceName.isNotEmpty())
                    deviceManager.restartLastAudioDevice();
            }
            return;
        }

        const auto count = analyser.callbackCount.load (std::memory_order_relaxed);
        const bool flowing = count != lastCallbackCount;
        stalledSeconds = flowing ? 0 : stalledSeconds + 1;
        lastCallbackCount = count;

        // La carte a changé de fréquence sans que JUCE le voie : Windows
        // convertit alors en silence et Merlin garderait l'ancienne.
        const auto cardRate = cardRateProbe.poll (deviceManager.getAudioDeviceSetup().inputDeviceName);
        const bool wrongRate = cardRate > 0.0 && std::abs (cardRate - dev->getCurrentSampleRate()) > 1.0;
        wrongRateSeconds = wrongRate ? wrongRateSeconds + 1 : 0;
        if (flowing && ! wrongRate)
            reopenDelay = minReopenDelay;

        if (stalledSeconds >= reopenDelay || wrongRateSeconds >= reopenDelay)
        {
            stalledSeconds = wrongRateSeconds = 0;
            reopenDelay = juce::jmin (maxReopenDelay, reopenDelay * 2);   // la carte ne revient pas : essais espacés
            reopenCardDevice (cardRate);
        }
    }

    // restartLastAudioDevice() ne fait rien tant que le périphérique existe,
    // même arrêté : il faut le fermer d'abord. La fréquence demandée est celle
    // de la carte, jamais celle mémorisée.
    void reopenCardDevice (double cardRate)
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        deviceManager.closeAudioDevice();
        setup.sampleRate = cardRate;
        deviceManager.setAudioDeviceSetup (setup, true);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto xml = deviceManager.createStateXml())
            settings.setValue ("audioDeviceState", xml.get());
    }

    void showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (deviceManager,
                                                                 1, numInputs,   // entrées : 1 à 2 canaux
                                                                 0, 0,           // aucune sortie
                                                                 false, false,   // pas de MIDI
                                                                 true,           // canaux par paires stéréo
                                                                 false);
        selector->setSize (520, 380);

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector);
        o.dialogTitle                  = TR ("Réglages audio — source analysée");
        o.dialogBackgroundColour       = juce::Colour (0xff1a1a1a);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar            = true;
        o.resizable                    = false;
        o.componentToCentreAround      = this;
        o.launchAsync();
    }

    //==============================================================================
    juce::PropertiesFile& settings;
    juce::SharedResourcePointer<WindowBarLook> barLook;
    SharedAudioDeviceManager deviceManager;
    SystemLoopbackCapture    systemCapture;   // sortie de Windows (sans carte son)
    CardRateProbe            cardRateProbe;   // fréquence réelle de la carte
    enum AudioSource { sourceAuto = 0, sourceCard = 1, sourceSystem = 2 };
    int  audioSource = sourceAuto;
    bool usingSystem = false;
    int  autoCheckSeconds = 0;
    LeakAudioProcessor processor;
    AnalyserCallback analyser { processor };
    FullScreenWindow fullScreenWindow;
    ScaledHolder windowHolder;   // contenu de la fenêtre normale : l'éditeur à l'échelle
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    bool applyingUiScale = false;
    juce::Rectangle<int> scaleDisplayArea;   // écran sur lequel l'échelle Auto a été calculée
    bool alwaysOnTop = false;
    bool fullScreen = false;
    juce::String windowedState;

    // Clic dans Merlin, clavier laissé à Ableton : la fenêtre passe devant sans
    // prendre le clavier. Mais JUCE ferme un menu dès que la souris en sort si
    // l'appli n'est pas au premier plan (menus du clic droit qui disparaissaient)
    // : juste avant un menu (PopupFocus::prepare, PluginEditor.h), Merlin passe
    // au premier plan ; une fois le menu et une éventuelle fenêtre modale (nom
    // du preset…) refermés, il rend la main à la fenêtre d'avant. La bascule se
    // fait AVANT l'ouverture : activer la fenêtre pendant qu'un menu est ouvert
    // le referme (JUCE y voit un clic hors du menu).
    struct MenuFocus final : juce::MouseListener, private juce::Timer
    {
        explicit MenuFocus (MainWindow& o) : owner (o) {}
        ~MenuFocus() override { stopTimer(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            frontWindow().toFront (false);
            if (auto* leak = dynamic_cast<LeakEditor*> (owner.editor.get()))
                leak->raiseDetachedWindows();   // les modules détachés restent devant
        }

        void begin()
        {
            if (owner.keyboardInMerlin)
                return;
            auto* p = frontWindow().getPeer();
            if (p == nullptr)
                return;
            void* fg = WindowsDisplay::foregroundWindow();
            if (fg != p->getNativeHandle())
            {
                if (! holding)
                    returnTo = fg;
                WindowsDisplay::setNoActivate (p->getNativeHandle(), false);
                WindowsDisplay::activate (p->getNativeHandle());
            }
            holding = true;
            idleTicks = 0;
            startTimerHz (60);
        }

    private:
        juce::Component& frontWindow() { return owner.fullScreen ? static_cast<juce::Component&> (owner.fullScreenWindow) : owner; }
        void timerCallback() override { poll(); }

        static bool menuOrModalOpen()
        {
            if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
                return true;
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* c = desktop.getComponent (i))
                    if (c->isShowing() && juce::String (typeid (*c).name()).contains ("MenuWindow"))
                        return true;
            return false;
        }

        void poll()
        {
            const bool open = menuOrModalOpen();
            if (holding && ! open && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()
                && ++idleTicks > 2)   // laisse le temps à une fenêtre ouverte par le menu (Enregistrer…)
            {
                holding = false;
                owner.applyNoActivate();
                if (returnTo != nullptr && ! owner.keyboardInMerlin)
                    WindowsDisplay::activate (returnTo);
                returnTo = nullptr;
            }
            if (open)
                idleTicks = 0;
            if (! holding)
                stopTimer();
        }

        MainWindow& owner;
        void* returnTo = nullptr;
        bool  holding = false;
        int   idleTicks = 0;
    };
    MenuFocus menuFocus { *this };

    bool  keyboardInMerlin = false;
    bool  licenceKeyboard = false;      // écran de licence ouvert : saisie / collage de la clé
    void* keyboardReturnTo = nullptr;   // fenêtre (Ableton) qui avait le clavier avant

    void applyNoActivate()
    {
        const bool kb = keyboardInMerlin || licenceKeyboard;
        setMouseClickGrabsKeyboardFocus (kb);
        if (auto* p = getPeer())
            WindowsDisplay::setNoActivate (p->getNativeHandle(), ! kb);
        if (auto* p = fullScreenWindow.getPeer())
            WindowsDisplay::setNoActivate (p->getNativeHandle(), ! kb);
    }

    // L'écran de licence passe outre le bouton clavier : la fenêtre s'active
    // pour qu'on puisse taper ou coller la clé, puis revient au réglage choisi.
    void holdKeyboardForLicence (bool on)
    {
        if (on == licenceKeyboard)
            return;
        licenceKeyboard = on;
        fullScreenWindow.setMouseClickGrabsKeyboardFocus (on || keyboardInMerlin);
        applyNoActivate();
        if (on)
        {
            juce::Component& w = fullScreen ? static_cast<juce::Component&> (fullScreenWindow) : *this;
            if (auto* p = w.getPeer())
                WindowsDisplay::activate (p->getNativeHandle());
            w.toFront (true);
        }
    }

    // Bouton clavier : allumé, Merlin prend le clavier ; éteint, il le rend à
    // la fenêtre qui l'avait (Ableton).
    void setKeyboardInMerlin (bool on)
    {
        keyboardInMerlin = on;
        fullScreenWindow.keyboard = on;
        fullScreenWindow.setMouseClickGrabsKeyboardFocus (on);
        settings.setValue ("keyboardInMerlin", on);
        applyNoActivate();

        juce::Component& w = fullScreen ? static_cast<juce::Component&> (fullScreenWindow) : *this;
        if (on)
        {
            void* fg = WindowsDisplay::foregroundWindow();
            const auto* p = w.getPeer();
            keyboardReturnTo = (p != nullptr && fg == p->getNativeHandle()) ? nullptr : fg;
            w.toFront (true);
            w.grabKeyboardFocus();
        }
        else if (keyboardReturnTo != nullptr)
        {
            WindowsDisplay::activate (keyboardReturnTo);
            keyboardReturnTo = nullptr;
        }

        if (auto* leak = dynamic_cast<LeakEditor*> (editor.get()))
            leak->setKeyboardState (on);
    }
    juce::Point<int> windowedEditorSize;
    uint32_t lastCallbackCount = 0;
    int stalledSeconds = 0;
    int wrongRateSeconds = 0;
    static constexpr int minReopenDelay = 3, maxReopenDelay = 30;
    int reopenDelay = minReopenDelay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

//==============================================================================
class MerlinApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    // une seule fenêtre Merlin : un second lancement réveille la première
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::SystemStats::setApplicationCrashHandler (writeCrashLog);

        // v1.47 : renommage ("Merlin By Mikshake", puis "Merlin Alpha by Mikshake",
        // devenus "Merlin Alpha") : les réglages d'un ancien nom sont repris une fois
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Side Key System");
            const auto newFile = dir.getChildFile (juce::String (ProjectInfo::projectName) + ".settings");
            for (auto* oldName : { "Merlin Alpha by Mikshake.settings", "Merlin By Mikshake.settings" })
            {
                const auto oldFile = dir.getChildFile (oldName);
                if (! newFile.existsAsFile() && oldFile.existsAsFile())
                    oldFile.copyFileTo (newFile);
            }
        }

        juce::PropertiesFile::Options options;
        options.applicationName     = ProjectInfo::projectName;
        options.filenameSuffix      = ".settings";
        options.folderName          = "Side Key System";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);

        mainWindow = std::make_unique<MainWindow> (*appProperties.getUserSettings());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->bringToFront();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MerlinApplication)
