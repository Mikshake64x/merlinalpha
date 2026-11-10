#pragma once

// Merlin α existe en appli autonome (projet « Merlin By Mikshake ») et en plugin
// (projet « Merlin Alpha Plugin », mêmes sources) : le projet plugin définit
// MERLIN_PLUGIN=1. En plugin, le signal vient de la piste et ressort intact ;
// les réglages propres à l'appli (source audio, volume Windows, plein écran,
// premier plan, clavier) sont masqués.
#ifndef MERLIN_PLUGIN
 #define MERLIN_PLUGIN 0
#endif

namespace AppMode
{
    constexpr bool isPlugin = MERLIN_PLUGIN != 0;
}

// Étiquette de version (v1.81.42 : « beta », comme Sevenpack Alpha) : affichée
// après le numéro (À propos, Options) et envoyée au serveur de licence comme
// plugin_version, pour que /admin montre quelle build tourne. Le numéro du
// .jucer reste purement numérique (codes de version VST3, CFBundleVersion du
// Mac). Chaîne vide = version finale. L'installeur Windows (MyAppLabel) et le
// build Mac (LABEL dans .github/workflows/macos.yml) ont la même étiquette.
#define MERLIN_BUILD_LABEL "beta"

#include <JuceHeader.h>
inline juce::String merlinVersionLabel()
{
    const juce::String number (ProjectInfo::versionString), label (MERLIN_BUILD_LABEL);
    return label.isEmpty() ? number : number + " " + label;
}
