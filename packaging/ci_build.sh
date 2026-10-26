#!/bin/bash
# Compile un projet Xcode (toutes les cibles, Release) ; en cas d'échec, les
# premières erreurs sont publiées en annotations (lisibles sans connexion).
# $1 = projet .xcodeproj, $2 = fichier journal
xcodebuild -project "$1" -alltargets -configuration Release \
    CODE_SIGN_IDENTITY="-" CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM="" > "$2" 2>&1
status=$?
if [ $status -ne 0 ]; then
    grep -E "error:|fatal error|Undefined symbol|ld: " "$2" | sort -u | head -10 | while IFS= read -r l; do
        echo "::error::${l:0:900}"
    done
    tail -n 15 "$2"
else
    echo "::notice::$(basename "$1") : BUILD SUCCEEDED"
fi
exit $status
