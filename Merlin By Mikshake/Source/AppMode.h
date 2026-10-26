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
