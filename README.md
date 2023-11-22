# pt2-clone
ProTracker 2 clone for Windows/macOS/Linux

Aims to be a highly accurate clone of the classic ProTracker 2.3D software for Amiga. \
Has additional audio filters and audio mixer improvements to make it sound close to a real Amiga computer. \
*What is ProTracker? Read about it on [Wikipedia](https://en.wikipedia.org/wiki/ProTracker).*


# Releases
Windows/macOS binary releases can always be found at [16-bits.org/pt2.php](https://16-bits.org/pt2.php).

# Note to Linux users
- On some distros, a "pt2-clone" package may be available in the distribution's package repository
- `protracker.ini` should be copied to `~/.protracker/`. If you do this, make sure you delete `protracker.ini` if it exists in the same directory as the program executable. You can find an up-to-date copy in the source code tree at `/release/other/` (or inside the Windows/macOS release zips at [16-bits.org/pt2.php](https://16-bits.org/pt2.php))

# Note to macOS users
- To get the config file to load, protracker.ini has to be in the same directory as the .app (program) itself

# Handy keybindings
1) Press F12 to toggle audio output between Amiga 500 and Amiga 1200. Amiga 500 mode uses a 4.42kHz 6dB/oct low-pass filter that matches that of a real A500.
If you want this to always be on, have a look at the config file. \
__Note__: This must not be confused with the "LED" filter, which is a completely different filter.
2) Press SHIFT+F12 to toggle stereo separation between centered (mono), custom (set in the config file) and Amiga (100%)
3) Press CTRL+F12 to toggle BPM timing mode between CIA (most commonly used) and vblank. Only do this if you know what
you are doing!
4) Press ALT+F11 to toggle real VU-meters. This will change the fake VU-meters into real, averaged VU-meters.
This can also be permanently activated by editing the config file
5) Press F11 to toggle fullscreen mode. Again, this can also be permanently activated in the config file

# Screenshots

![Screenshot #1](https://16-bits.org/pt2-clone-1.png)
![Screenshot #2](https://16-bits.org/pt2-clone-2.png)


# Compiling the code
Please read HOW-TO-COMPILE.txt file in the repository.

PS: The source code is quite hackish and hardcoded. \
My first priority is to make an accurate clone, and not to make flexible and easily modifiable code.
