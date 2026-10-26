#pragma once
#include <JuceHeader.h>

/*
    Lang — langue de l'interface (Options > Langue)
    -----------------------------------------------
    Table compilée dans les ressources : Source/translations.tsv (UTF-8, une
    ligne par texte : texte source, puis fr en de it es pt ru ja zh ko séparés
    par des tabulations ; cellule vide = texte source gardé tel quel).

    Le texte source est celui de l'interface d'origine (français pour les
    Options et les dialogues, anglais pour les modules) : en français, Merlin
    reste exactement comme avant. Un texte source peut porter un contexte
    après '|' ("MID|band") pour distinguer deux sens ; le contexte n'est
    jamais affiché.

    TR("…") rend le texte dans la langue courante ; "{0}" se remplace avec
    juce::String::replace. Le changement de langue est appliqué en place par
    LeakEditor::applyLanguage() (pas de recréation de l'éditeur : les modules
    OpenGL ne le supportent pas).
*/
namespace Lang
{
    enum Id { French, English, German, Italian, Spanish, Portuguese, Russian, Japanese, Chinese, Korean, Count };

    int  current();
    void set (int id);
    int  fromSystem();                   // langue de Windows, anglais à défaut

    juce::String nativeName (int id);    // "Deutsch", "日本語"…
    juce::String code (int id);          // "DE"

    // texte source (UTF-8) -> langue courante
    juce::String tr (const char* utf8Source);
    juce::String tr (const juce::String& source);
    bool has (const juce::String& source);   // texte présent dans la table

    // Textes posés une fois sur des boutons ("RESET", "LINE"…) : à chaque
    // appel, ceux dont le texte d'origine est dans la table sont retraduits.
    // Le texte source est mémorisé dans les propriétés du composant au premier
    // passage (avant toute traduction). Les infobulles sont reposées par
    // leurs propriétaires (TopBar::retranslate…).
    void retranslateTree (juce::Component& root);

    // Polices : repli explicite pour le japonais, le chinois et le coréen
    // (même idéogramme, dessin différent selon la langue). Le latin et les
    // chiffres restent dans la police demandée.
    juce::FontOptions withScriptFallback (juce::FontOptions);
}

#define TR(s) Lang::tr (s)
