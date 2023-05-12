#!/bin/bash

rm release/other/pt2-clone &> /dev/null

echo Compiling, please wait...
gcc -DNDEBUG -DHAS_LIBFLAC src/gfx/*.c src/modloaders/*.c src/libflac/*.c src/smploaders/*.c src/*.c -lSDL2 -lm -Wshadow -Winit-self -Wall -Wno-missing-field-initializers -Wno-unused-result -Wno-strict-aliasing -Wextra -Wunused -Wunreachable-code -Wswitch-default -Wno-stringop-overflow -march=native -mtune=native -O3 -o release/other/pt2-clone
rm src/gfx/*.o src/*.o &> /dev/null

echo Done. The executable can be found in \'release/other\' if everything went well.
