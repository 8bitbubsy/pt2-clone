# pt2-clone
ProTracker 2 clone for Windows/macOS/Linux, by [8bitbubsy](https://16-bits.org).

Aims to be a **highly accurate** clone of the classic ProTracker 2.3D software for Amiga. \
Has additional audio filters and audio mixer improvements to make it sound close to a real Amiga computer. \
*What is ProTracker? Read about it on [Wikipedia](https://en.wikipedia.org/wiki/ProTracker).*


# Releases
Windows/macOS binary releases can always be found at [16-bits.org](https://16-bits.org/pt2.php).

Linux users can try to search for the "pt2-clone" package in the distribution's package repository, but it may not be present. \
If it's not present, you will unfortunately have to compile the program manually, which may or may not be successful. Please don't contact me if it didn't go well, as I don't fully support Linux on my tracker clones.


# Handy functions
1) Press F12 to toggle "Amiga 500" audio mode. This will activate an RC low-pass filter that (closely) matches that of a real A500.
If you want this to always be on, have a look at the config file (protracker.ini).
Note: This must not be confused with the "LED" filter, which is something entirely different! (Yes, every Amiga has three filter stages)
2) Press SHIFT+F12 to enable 100% stereo separation. This gives the same hard-panned audio that you get from an Amiga. This can also be permanently activated by changing the stereo separation to 100
in protracker.ini. Setting it to 0 will result in mono.
3) Press CTRL+F12 to toggle BPM timing mode between CIA (most commonly used) and vblank. Only do this if you know what
you are doing!
4) Press ALT+F11 to toggle real VU-meters. This will change the fake VU-meters into real, averaged VU-meters.
This can also be permanently activated by editing protracker.ini
5) Press F11 to toggle fullscreen mode. Again, this can also be permanently activated in protracker.ini

# Screenshots

![Screenshot #1](https://16-bits.org/pt2-clone-1.png)
![Screenshot #2](https://16-bits.org/pt2-clone-2.png)


# Compiling the code
Please read HOW-TO-COMPILE.txt file in the repository.

PS: The source code is quite hackish and hardcoded. \
My first priority is to make an _accurate_ 1:1 clone, and not to make flexible and easily modifiable code.
