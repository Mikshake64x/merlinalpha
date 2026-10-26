/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   sidekey_monogram_png;
    const int            sidekey_monogram_pngSize = 717595;

    extern const char*   sidekey_blackbird_png;
    const int            sidekey_blackbird_pngSize = 243057;

    extern const char*   merle_png;
    const int            merle_pngSize = 1476102;

    extern const char*   AudiowideRegular_ttf;
    const int            AudiowideRegular_ttfSize = 69916;

    extern const char*   mikshake_about_png;
    const int            mikshake_about_pngSize = 68539;

    extern const char*   translations_tsv;
    const int            translations_tsvSize = 75840;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 6;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
