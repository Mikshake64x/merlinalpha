#include "Lang.h"
#include <unordered_map>

namespace Lang
{
namespace
{
    std::atomic<int> currentId { French };

    struct Table
    {
        std::unordered_map<juce::String, std::array<juce::String, Count>> entries;

        Table()
        {
            const juce::String text (juce::CharPointer_UTF8 (BinaryData::translations_tsv),
                                     (size_t) BinaryData::translations_tsvSize);
            for (auto line : juce::StringArray::fromLines (text))
            {
                if (line.isEmpty() || line.startsWithChar ('#'))
                    continue;
                auto cells = juce::StringArray::fromTokens (line, "\t", {});
                if (cells.size() < 2)
                    continue;
                std::array<juce::String, Count> row;
                for (int i = 0; i < Count; ++i)
                    row[(size_t) i] = cells[i + 1].replace ("\\n", "\n").trimEnd();
                entries[cells[0].replace ("\\n", "\n")] = row;
            }
        }
    };

    const Table& table()
    {
        static const Table t;   // lu une fois, ensuite en lecture seule (fils GL compris)
        return t;
    }

    // texte affiché quand une langue n'a pas de traduction : la source sans contexte
    juce::String stripContext (const juce::String& s)
    {
        const int bar = s.indexOfChar ('|');
        return bar > 0 ? s.substring (0, bar) : s;
    }
}

int current()        { return currentId.load(); }
void set (int id)    { currentId = juce::jlimit (0, Count - 1, id); }

int fromSystem()
{
    const auto l = juce::SystemStats::getUserLanguage().toLowerCase().substring (0, 2);
    static const char* const codes[Count] = { "fr", "en", "de", "it", "es", "pt", "ru", "ja", "zh", "ko" };
    for (int i = 0; i < Count; ++i)
        if (l == codes[i])
            return i;
    return English;
}

juce::String nativeName (int id)
{
    static const char* const names[Count] =
    {
        "Fran\xc3\xa7" "ais", "English", "Deutsch", "Italiano", "Espa\xc3\xb1ol", "Portugu\xc3\xaas (Brasil)",
        "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9",                 // Русский
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",                                     // 日本語
        "\xe4\xb8\xad\xe6\x96\x87\xef\xbc\x88\xe7\xae\x80\xe4\xbd\x93\xef\xbc\x89", // 中文（简体）
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"                                      // 한국어
    };
    return juce::String (juce::CharPointer_UTF8 (names[juce::jlimit (0, Count - 1, id)]));
}

juce::String code (int id)
{
    static const char* const codes[Count] = { "FR", "EN", "DE", "IT", "ES", "PT", "RU", "JA", "ZH", "KO" };
    return codes[juce::jlimit (0, Count - 1, id)];
}

juce::String tr (const juce::String& source)
{
    const auto& entries = table().entries;
    const auto it = entries.find (source);
    if (it != entries.end())
    {
        const auto& t = it->second[(size_t) current()];
        if (t.isNotEmpty())
            return t;
    }
    return stripContext (source);
}

bool has (const juce::String& source)
{
    return table().entries.count (source) > 0;
}

void retranslateTree (juce::Component& root)
{
    static const juce::Identifier textKey ("trText");
    auto& props = root.getProperties();
    if (auto* b = dynamic_cast<juce::Button*> (&root))
    {
        if (! props.contains (textKey) && has (b->getButtonText()))
            props.set (textKey, b->getButtonText());
        if (props.contains (textKey))
            b->setButtonText (tr (props[textKey].toString()));
    }
    for (auto* child : root.getChildren())
        retranslateTree (*child);
}

juce::String tr (const char* utf8Source)
{
    return tr (juce::String (juce::CharPointer_UTF8 (utf8Source)));
}

juce::FontOptions withScriptFallback (juce::FontOptions o)
{
   #if JUCE_WINDOWS
    switch (current())
    {
        case Japanese: return o.withFallbacks ({ "Yu Gothic UI", "Meiryo UI", "MS UI Gothic" });
        case Chinese:  return o.withFallbacks ({ "Microsoft YaHei UI", "Microsoft YaHei", "SimHei" });
        case Korean:   return o.withFallbacks ({ "Malgun Gothic" });
        case Russian:  return o.withFallbacks ({ "Segoe UI" });
        default:       break;
    }
   #endif
    return o;
}
}
