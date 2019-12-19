#!/bin/bash

rm release/other/pt2-clone &> /dev/null

echo Compiling, please wait...
gcc -DNDEBUG src/gfx/*.c src/*.c -lSDL2 -lm -Wall -Wno-unused-result -Wc++-compat -Wshadow -Winit-self -Wextra -Wunused -Wunreachable-code -Wredundant-decls -Wswitch-default -march=native -mtune=native -O3 -o release/other/pt2-clone
rm src/gfx/*.o src/*.o &> /dev/null

echo Done! The executable is in the folder named \'release/other\'.