#!/bin/bash

rm release/other/pt_pal_editor &> /dev/null

echo Compiling, please wait...
gcc src/tinyfiledialogs/*.c src/gfx/*.c src/*.c -Wno-unused-result -lSDL2 -lm -march=native -mtune=native -O3 -o release/other/pt_pal_editor
rm src/tinyfiledialogs/*.o src/gfx/*.o src/*.o &> /dev/null

echo Done. The executable can be found in \'release/other\' if everything went well.
