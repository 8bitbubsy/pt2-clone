#!/bin/bash

# Modern macOS build script with Homebrew and Apple Silicon support
# Updated for macOS 11+ (Big Sur and later)

set -e

if [ "$1" = "-v" ]; then
    VERBOSE=-v
fi

#
# Detect architecture and set defaults
#
HOST_ARCH=$(uname -m)
echo "Building on ${HOST_ARCH} architecture..."

#
# Detect SDL2 location (Homebrew or system)
#
SDL2_FRAMEWORK=""
SDL2_INCLUDE_ARM64=""
SDL2_LIB_ARM64=""
SDL2_INCLUDE_X86_64=""
SDL2_LIB_X86_64=""

# Check for both Homebrew installations
HOMEBREW_ARM64="/opt/homebrew"
HOMEBREW_X86_64="/usr/local"

HAS_ARM64_SDL=0
HAS_X86_64_SDL=0

if [ -d "$HOMEBREW_ARM64/opt/sdl2" ]; then
    echo "Found arm64 SDL2 in Homebrew at $HOMEBREW_ARM64/opt/sdl2"
    SDL2_INCLUDE_ARM64="$HOMEBREW_ARM64/opt/sdl2/include"
    SDL2_LIB_ARM64="$HOMEBREW_ARM64/opt/sdl2/lib"
    HAS_ARM64_SDL=1
fi

if [ -d "$HOMEBREW_X86_64/opt/sdl2" ]; then
    echo "Found x86_64 SDL2 in Homebrew at $HOMEBREW_X86_64/opt/sdl2"
    SDL2_INCLUDE_X86_64="$HOMEBREW_X86_64/opt/sdl2/include"
    SDL2_LIB_X86_64="$HOMEBREW_X86_64/opt/sdl2/lib"
    HAS_X86_64_SDL=1
fi

# Determine build strategy
if [ $HAS_ARM64_SDL -eq 1 ] && [ $HAS_X86_64_SDL -eq 1 ]; then
    echo "Building universal binary with separate Homebrew SDL2 for each architecture"
    USE_HOMEBREW_SDL=1
    BUILD_UNIVERSAL=1
elif [ $HAS_ARM64_SDL -eq 1 ] || [ $HAS_X86_64_SDL -eq 1 ]; then
    echo "WARNING: Only one architecture of Homebrew SDL2 found"
    echo "Will attempt to build for available architecture only"
    USE_HOMEBREW_SDL=1
    BUILD_UNIVERSAL=0
elif [ -d "/Library/Frameworks/SDL2.framework" ]; then
    echo "Found SDL2 framework in /Library/Frameworks"
    SDL2_FRAMEWORK="/Library/Frameworks"
    USE_HOMEBREW_SDL=0
    BUILD_UNIVERSAL=1
else
    echo "ERROR: SDL2 not found!"
    echo "Please install SDL2 using one of these methods:"
    echo "  1. Universal build: brew install sdl2 on both arm64 and x86_64 Homebrew"
    echo "     - For arm64: install Homebrew at /opt/homebrew"
    echo "     - For x86_64: install Rosetta 2 Homebrew at /usr/local"
    echo "  2. Framework build: Download SDL2.framework from https://libsdl.org"
    echo "     and place in /Library/Frameworks"
    exit 1
fi

#
# Detect macOS SDK
#
if [ -d "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk" ]; then
    SDK_PATH="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
elif [ -d "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk" ]; then
    SDK_PATH="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
else
    echo "WARNING: Could not find macOS SDK, using default"
    SDK_PATH=""
fi

if [ -n "$SDK_PATH" ]; then
    echo "Using SDK: $SDK_PATH"
    export SDKROOT="$SDK_PATH"
fi

#
# Setup variables
#
VERSION=v`grep PROG_VER_STR src/pt2_header.h|cut -d'"' -f 2`

RELEASE_MACOS_DIR=release/macos/
APP_DIR=${RELEASE_MACOS_DIR}pt2-clone-macos.app/

TARGET_X86_64=${APP_DIR}Contents/MacOS/pt2-clone-macos-x86_64
TARGET_ARM64=${APP_DIR}Contents/MacOS/pt2-clone-macos-arm64
TARGET_UNIVERSAL=${APP_DIR}Contents/MacOS/pt2-clone-macos
TARGET_DIR=${APP_DIR}Contents/MacOS/

#
# Prepare
#
if [ ! -d $TARGET_DIR ]; then
    mkdir -p $TARGET_DIR
fi

#
# Compile function
#
function compile() {
    local target=$1
    local arch=$2
    local sdl2_include=$3
    local sdl2_lib=$4
    
    rm $target &> /dev/null || true
    
    if [ $USE_HOMEBREW_SDL -eq 1 ]; then
        # Compile with Homebrew SDL2
        clang $VERBOSE $CFLAGS -I"$sdl2_include" -g0 -DNDEBUG -DHAS_LIBFLAC \
            src/gfx/*.c src/modloaders/*.c src/libflac/*.c src/smploaders/*.c src/*.c \
            -Wall -Winit-self -Wextra -Wunused -Wredundant-decls \
            $LDFLAGS -L"$sdl2_lib" -lSDL2 -framework Cocoa -lm -o $target
    else
        # Compile with Framework SDL2
        clang $VERBOSE $CFLAGS -F "$SDL2_FRAMEWORK" -g0 -DNDEBUG -DHAS_LIBFLAC \
            src/gfx/*.c src/modloaders/*.c src/libflac/*.c src/smploaders/*.c src/*.c \
            -Wall -Winit-self -Wextra -Wunused -Wredundant-decls \
            $LDFLAGS -L "$SDL2_FRAMEWORK" -framework SDL2 -framework Cocoa -lm -o $target
    fi
    
    return $?
}

#
# Build binaries based on available SDL2
#
BUILD_X86_64=0
BUILD_ARM64=0

if [ $BUILD_UNIVERSAL -eq 1 ]; then
    BUILD_X86_64=1
    BUILD_ARM64=1
else
    # Only build for available architecture
    if [ $HAS_X86_64_SDL -eq 1 ]; then
        BUILD_X86_64=1
    fi
    if [ $HAS_ARM64_SDL -eq 1 ]; then
        BUILD_ARM64=1
    fi
fi

#
# Build x86_64 binary
#
if [ $BUILD_X86_64 -eq 1 ]; then
    echo "Compiling x86_64 binary, please wait patiently..."
    CFLAGS="-target x86_64-apple-macos11 -mmacosx-version-min=11.0 -arch x86_64 -mmmx -mfpmath=sse -msse2 -O3"
    LDFLAGS=""
    compile $TARGET_X86_64 "x86_64" "$SDL2_INCLUDE_X86_64" "$SDL2_LIB_X86_64"
    if [ $? -ne 0 ]; then
        echo "ERROR: x86_64 compilation failed"
        exit 1
    fi
    echo "x86_64 binary compiled successfully"
fi

#
# Build arm64 binary
#
if [ $BUILD_ARM64 -eq 1 ]; then
    echo "Compiling arm64 binary, please wait patiently..."
    CFLAGS="-target arm64-apple-macos11 -mmacosx-version-min=11.0 -arch arm64 -mcpu=apple-m1 -O3"
    LDFLAGS=""
    compile $TARGET_ARM64 "arm64" "$SDL2_INCLUDE_ARM64" "$SDL2_LIB_ARM64"
    if [ $? -ne 0 ]; then
        echo "ERROR: arm64 compilation failed"
        exit 1
    fi
    echo "arm64 binary compiled successfully"
fi

#
# Merge binaries or copy single binary
#
# Reference: Building a Universal macOS Binary
#   https://developer.apple.com/documentation/xcode/building_a_universal_macos_binary
echo ""
rm $TARGET_UNIVERSAL &> /dev/null || true

if [ $BUILD_X86_64 -eq 1 ] && [ $BUILD_ARM64 -eq 1 ]; then
    echo "Building universal binary..."
    lipo -create -output $TARGET_UNIVERSAL $TARGET_X86_64 $TARGET_ARM64
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to create universal binary"
        exit 1
    fi
    
    echo "Removing individual architecture binaries..."
    rm $TARGET_X86_64
    rm $TARGET_ARM64
elif [ $BUILD_X86_64 -eq 1 ]; then
    echo "Building x86_64-only binary..."
    mv $TARGET_X86_64 $TARGET_UNIVERSAL
elif [ $BUILD_ARM64 -eq 1 ]; then
    echo "Building arm64-only binary..."
    mv $TARGET_ARM64 $TARGET_UNIVERSAL
fi

echo "Stripping debug symbols..."
strip $TARGET_UNIVERSAL

#
# Fix SDL2 linking based on installation type
#
if [ $USE_HOMEBREW_SDL -eq 0 ]; then
    # Using SDL2.framework - need to fix the rpath
    echo "Fixing SDL2.framework rpath..."
    install_name_tool -change @rpath/SDL2.framework/Versions/A/SDL2 \
        @executable_path/../Frameworks/SDL2.framework/Versions/A/SDL2 \
        $TARGET_UNIVERSAL
else
    # Using Homebrew SDL2 - check if we need to fix the library path
    echo "Using Homebrew SDL2 (dynamic linking)"
    # Optionally, you could use install_name_tool to make it more portable
    # by changing the absolute path to @rpath or @executable_path
    if [ -n "$SDL2_LIB_ARM64" ] && otool -L $TARGET_UNIVERSAL | grep -q "$SDL2_LIB_ARM64"; then
        echo "Note: Binary links to Homebrew SDL2 at $SDL2_LIB_ARM64"
        echo "      Users will need SDL2 installed via Homebrew to run this binary"
    elif [ -n "$SDL2_LIB_X86_64" ] && otool -L $TARGET_UNIVERSAL | grep -q "$SDL2_LIB_X86_64"; then
        echo "Note: Binary links to Homebrew SDL2 at $SDL2_LIB_X86_64"
        echo "      Users will need SDL2 installed via Homebrew to run this binary"
    fi
fi

#
# Code sign the app
#
echo "Code signing application..."
codesign -s - --deep --force --entitlements pt2-clone.entitlements ${APP_DIR} 2>/dev/null || {
    echo "WARNING: Code signing failed (this is usually fine for local development)"
}

echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="
if [ $BUILD_X86_64 -eq 1 ] && [ $BUILD_ARM64 -eq 1 ]; then
    echo "Architecture: Universal (x86_64 + arm64)"
elif [ $BUILD_X86_64 -eq 1 ]; then
    echo "Architecture: x86_64 only"
elif [ $BUILD_ARM64 -eq 1 ]; then
    echo "Architecture: arm64 only"
fi
echo "Minimum macOS: 11.0 (Big Sur)"
echo "SDL2 source: $([ $USE_HOMEBREW_SDL -eq 1 ] && echo 'Homebrew' || echo 'Framework')"
echo "Output: ${APP_DIR}Contents/MacOS/pt2-clone-macos"
echo ""

if [ $USE_HOMEBREW_SDL -eq 1 ]; then
    echo "NOTE: This build uses Homebrew SDL2."
    echo "      Users need: brew install sdl2"
    if [ $BUILD_UNIVERSAL -eq 0 ]; then
        echo ""
        echo "WARNING: Only one architecture built due to missing SDL2."
        echo "         For universal binary, install SDL2 for both architectures:"
        echo "         - arm64: /opt/homebrew (brew install sdl2)"
        echo "         - x86_64: /usr/local (Rosetta 2: arch -x86_64 /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\" && arch -x86_64 /usr/local/bin/brew install sdl2)"
    fi
fi

#
# Cleanup
#
echo "Cleaning up build artifacts..."
rm src/gfx/*.o src/modloaders/*.o src/libflac/*.o src/smploaders/*.o src/*.o &> /dev/null || true
echo "Done!"
