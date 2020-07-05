#!/bin/bash

arch=$(arch)
if [ $arch == "ppc" ]; then
    echo Sorry, PowerPC \(PPC\) is not supported...
else
    echo Compiling 64-bit binary, please wait...
    
    rm release/macos/pt_pal_editor-macos.app/Contents/MacOS/pt_pal_editor-macos &> /dev/null
    
    clang -mmacosx-version-min=10.6 -arch x86_64 -mmmx -mfpmath=sse -msse2 -I/Library/Frameworks/SDL2.framework/Headers -F/Library/Frameworks src/tinyfiledialogs/*.c src/gfx/*.c src/*.c -O3 -framework SDL2 -framework Cocoa -o release/macos/pt_pal_editor-macos.app/Contents/MacOS/pt_pal_editor-macos
    install_name_tool -change @rpath/SDL2.framework/Versions/A/SDL2 @executable_path/../Frameworks/SDL2.framework/Versions/A/SDL2 release/macos/pt_pal_editor-macos.app/Contents/MacOS/pt_pal_editor-macos
    
    rm src/tinyfiledialogs/*.o src/gfx/*.o src/*.o &> /dev/null
    echo Done. The executable can be found in \'release/macos\' if everything went well.
fi
