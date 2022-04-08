// for finding memory leaks in debug mode with Visual Studio 
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#include <SDL2/SDL_syswm.h>
#else
#include <signal.h>
#include <unistd.h> // chdir()
#endif
#include <sys/stat.h>
#include "pt2_header.h"
#include "pt2_helpers.h"
#include "pt2_keyboard.h"
#include "pt2_textout.h"
#include "pt2_mouse.h"
#include "pt2_diskop.h"
#include "pt2_sampler.h"
#include "pt2_config.h"
#include "pt2_visuals.h"
#include "pt2_edit.h"
#include "pt2_module_loader.h"
#include "pt2_module_saver.h"
#include "pt2_sample_loader.h"
#include "pt2_unicode.h"
#include "pt2_scopes.h"
#include "pt2_audio.h"
#include "pt2_bmp.h"
#include "pt2_sync.h"
#include "pt2_sampling.h"
#include "pt2_hpc.h"

#define CRASH_TEXT "Oh no!\nThe ProTracker 2 clone has crashed...\n\nA backup .mod was hopefully " \
                   "saved to the current module directory.\n\nPlease report this bug if you can.\n" \
                   "Try to mention what you did before the crash happened."

module_t *song = NULL; // globalized

static bool backupMadeAfterCrash;

#ifdef _WIN32
#define SYSMSG_FILE_ARG (WM_USER + 1)
#define ARGV_SHARED_MEM_MAX_LEN ((MAX_PATH * 2) + 2)

#ifndef _DEBUG
bool windowsKeyIsDown;
HHOOK g_hKeyboardHook;
#endif

static HWND hWnd_to;
static HANDLE oneInstHandle, hMapFile;
static LPCTSTR sharedMemBuf;
static TCHAR sharedHwndName[] = TEXT("Local\\PT2CloneHwnd");
static TCHAR sharedFileName[] = TEXT("Local\\PT2CloneFilename");
static bool handleSingleInstancing(int32_t argc, char **argv);
static void closeSingleInstancing(void);
static void handleSysMsg(SDL_Event inputEvent);
#endif

#ifndef _DEBUG
#ifdef _WIN32
static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *ptr);
#else
static void exceptionHandler(int32_t signal);
#endif
#endif

#ifdef _WIN32
static void makeSureDirIsProgramDir(void);
static void disableWasapi(void);
#endif

#ifdef __APPLE__
static void osxSetDirToProgramDirFromArgs(char **argv);
static bool checkIfAppWasTranslocated(int argc, char **argv);
#endif

static void handleInput(void);
static bool initializeVars(void);
static void handleSigTerm(void);
static void cleanUp(void);

static void clearStructs(void)
{
	memset(&keyb,   0, sizeof (keyb));
	memset(&mouse,  0, sizeof (mouse));
	memset(&video,  0, sizeof (video));
	memset(&editor, 0, sizeof (editor));
	memset(&diskop, 0, sizeof (diskop));
	memset(&cursor, 0, sizeof (cursor));
	memset(&ui,     0, sizeof (ui));
	memset(&config, 0, sizeof (config));
	memset(&audio,  0, sizeof (audio));

	audio.rescanAudioDevicesSupported = true;
}

int main(int argc, char *argv[])
{
#ifndef _WIN32
	struct sigaction act, oldAct;
#endif

#if defined _WIN32 || defined __APPLE__
	SDL_version sdlVer;
#endif

	// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

#if SDL_PATCHLEVEL < 5
	#pragma message("WARNING: The SDL2 dev lib is older than ver 2.0.5. You'll get fullscreen mode issues.")
#endif

	// set up crash handler
#ifndef _DEBUG
#ifdef _WIN32
	SetUnhandledExceptionFilter(exceptionHandler);
#else
	memset(&act, 0, sizeof (act));
	act.sa_handler = exceptionHandler;
	act.sa_flags = SA_RESETHAND;

	sigaction(SIGILL | SIGABRT | SIGFPE | SIGSEGV, &act, &oldAct);
	sigaction(SIGILL, &act, &oldAct);
	sigaction(SIGABRT, &act, &oldAct);
	sigaction(SIGFPE, &act, &oldAct);
	sigaction(SIGSEGV, &act, &oldAct);
#endif
#endif

	clearStructs();

	// on Windows and macOS, test what version SDL2.DLL is (against library version used in compilation)
#if defined _WIN32 || defined __APPLE__
	SDL_GetVersion(&sdlVer);
	if (sdlVer.major != SDL_MAJOR_VERSION || sdlVer.minor != SDL_MINOR_VERSION || sdlVer.patch != SDL_PATCHLEVEL)
	{
#ifdef _WIN32
		showErrorMsgBox("SDL2.dll is not the expected version, the program will terminate.\n\n" \
		                "Loaded dll version: %d.%d.%d\n" \
		                "Required (compiled with) version: %d.%d.%d",
		                sdlVer.major, sdlVer.minor, sdlVer.patch,
		                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#else
		showErrorMsgBox("The loaded SDL2 library is not the expected version, the program will terminate.\n\n" \
		                "Loaded library version: %d.%d.%d\n" \
		                "Required (compiled with) version: %d.%d.%d",
		                sdlVer.major, sdlVer.minor, sdlVer.patch,
		                SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
#endif
		return 0;
	}
#endif

#ifdef _WIN32

#ifndef _MSC_VER
	SetProcessDPIAware();
#endif

	if (!SDL_HasSSE())
	{
		showErrorMsgBox("Your computer's processor doesn't have the SSE+SSE2 instruction set\n" \
		                "which is needed for this program to run. Sorry!");
		return 0;
	}

	if (!SDL_HasSSE2())
	{
		showErrorMsgBox("Your computer's processor doesn't have the SSE2 instruction set\n" \
		                "which is needed for this program to run. Sorry!");
		return 0;
	}

	disableWasapi(); // disable problematic WASAPI SDL2 audio driver on Windows (causes clicks/pops sometimes...)
	                 // 13.03.2020: This is still needed with SDL 2.0.12...
#endif

	/* SDL 2.0.9 for Windows has a serious bug where you need to initialize the joystick subsystem
	** (even if you don't use it) or else weird things happen like random stutters, keyboard (rarely) being
	** reinitialized in Windows and what not.
	** Ref.: https://bugzilla.libsdl.org/show_bug.cgi?id=4391
	*/
#if defined _WIN32 && SDL_PATCHLEVEL == 9
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0)
#else
	if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO) != 0)
#endif
	{
		showErrorMsgBox("Couldn't initialize SDL: %s", SDL_GetError());
		return 0;
	}

	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
	SDL_EnableScreenSaver(); // allow screensaver to activate

	/* Text input is started by default in SDL2, turn it off to remove ~2ms spikes per key press.
	** We manuallay start it again when someone clicks on a text edit box, and stop it when done.
	** Ref.: https://bugzilla.libsdl.org/show_bug.cgi?id=4166
	*/
	SDL_StopTextInput();

 #ifdef __APPLE__
	if (checkIfAppWasTranslocated(argc, argv))
	{
		SDL_Quit();
		return 0;
	}
#endif

#ifdef __APPLE__
	osxSetDirToProgramDirFromArgs(argv);
#endif

#ifdef _WIN32

	// Windows: the Win key has to be taken over to work like the Amiga key
#ifndef _DEBUG
	windowsKeyIsDown = false;
	g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardProc, GetModuleHandle(NULL), 0);
#endif

	makeSureDirIsProgramDir();
#endif

	if (!initializeVars())
	{
		cleanUp();
		SDL_Quit();
		return 1;
	}

	loadConfig();

	if (!setupVideo())
	{
		cleanUp();
		SDL_Quit();
		return 1;
	}

#ifdef _WIN32
	// allow only one instance, and send arguments to it (song to play)
	if (handleSingleInstancing(argc, argv))
	{
		cleanUp();
		SDL_Quit();
		return 0; // close current instance, the main instance got a message now
	}

	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif

	hpc_Init();
	hpc_SetDurationInHz(&video.vblankHpc, VBLANK_HZ);

	if (!initKaiserTable() || !setupAudio() || !unpackBMPs())
	{
		cleanUp();
		SDL_Quit();
		return 1;
	}

	setupSprites();

	song = createEmptyMod();
	if (song == NULL)
	{
		cleanUp();
		SDL_Quit();
		return 1;
	}

	if (!initScopes())
	{
		cleanUp();
		SDL_Quit();
		return 1;
	}

	modSetTempo(editor.initialTempo, false);
	modSetSpeed(editor.initialSpeed);

	updateWindowTitle(MOD_NOT_MODIFIED);
	pointerSetMode(POINTER_MODE_IDLE, DO_CARRY);
	statusAllRight();
	setStatusMessage("PROTRACKER V2.3D", NO_CARRY);

	// load a .MOD from the command arguments if passed (also ignore OS X < 10.9 -psn argument on double-click launch)
	if ((argc >= 2 && argv[1][0] != '\0') && (argc != 2 || strncmp(argv[1], "-psn_", 5)))
	{
		loadModFromArg(argv[1]);

		// play song
		if (song->loaded)
		{
			editor.playMode = PLAY_MODE_NORMAL;
			modPlay(DONT_SET_PATTERN, 0, DONT_SET_ROW);
			editor.currMode = MODE_PLAY;
			pointerSetMode(POINTER_MODE_PLAY, DO_CARRY);
			statusAllRight();
		}
	}
	else if (!editor.configFound)
	{
		displayErrorMsg("CONFIG NOT FOUND!");
	}

	displayMainScreen();
	fillToVuMetersBgBuffer();
	updateCursorPos();

	SDL_ShowWindow(video.window);

	if (config.startInFullscreen)
		toggleFullScreen();

	changePathToHome(); // set path to home/user-dir now
	diskOpSetInitPath(); // set path to custom path in config (if present)

	SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

	hpc_ResetEndTime(&video.vblankHpc);
	while (editor.programRunning)
	{
		sinkVisualizerBars();
		updateChannelSyncBuffer();
		readMouseXY();
		readKeyModifiers(); // set/clear CTRL/ALT/SHIFT/AMIGA key states
		handleInput();
		updateMouseCounters();
		handleKeyRepeat(keyb.lastRepKey);

		if (!mouse.buttonWaiting && ui.sampleMarkingPos == -1 &&
			!ui.forceSampleDrag && !ui.forceVolDrag && !ui.forceSampleEdit)
		{
			handleGUIButtonRepeat();
		}

		renderFrame();
		flipFrame();
	}

	cleanUp();
	SDL_Quit();

	return 0;
}

static void handleInput(void)
{
	char inputChar;
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_WINDOWEVENT)
		{
			if (event.window.event == SDL_WINDOWEVENT_HIDDEN)
				video.windowHidden = true;
			else if (event.window.event == SDL_WINDOWEVENT_SHOWN)
				video.windowHidden = false;

			// reset vblank end time if we minimize window
			if (event.window.event == SDL_WINDOWEVENT_MINIMIZED || event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				hpc_ResetEndTime(&video.vblankHpc);
		}

#ifdef _WIN32
		handleSysMsg(event);
#endif
		if (ui.editTextFlag && event.type == SDL_TEXTINPUT)
		{
			// text input when editing texts/numbers

			inputChar = event.text.text[0];
			if (inputChar == '\0')
				continue;

			handleTextEditInputChar(inputChar);
			continue; // continue SDL event loop
		}
		else if (event.type == SDL_MOUSEWHEEL)
		{
			     if (event.wheel.y < 0) mouseWheelDownHandler();
			else if (event.wheel.y > 0) mouseWheelUpHandler();
		}
		else if (event.type == SDL_DROPFILE)
		{
			loadDroppedFile(event.drop.file, (uint32_t)strlen(event.drop.file), false, true);
			SDL_free(event.drop.file);
			SDL_RaiseWindow(video.window); // set window focus
		}
		if (event.type == SDL_QUIT)
		{
			handleSigTerm();
		}
		else if (event.type == SDL_KEYUP)
		{
			keyUpHandler(event.key.keysym.scancode, event.key.keysym.sym);
		}
		else if (event.type == SDL_KEYDOWN)
		{
			if (editor.repeatKeyFlag || keyb.lastRepKey != event.key.keysym.scancode)
				keyDownHandler(event.key.keysym.scancode, event.key.keysym.sym);
		}
		else if (event.type == SDL_MOUSEBUTTONUP)
		{
			mouseButtonUpHandler(event.button.button);

			if (!ui.askScreenShown && ui.introScreenShown)
			{
				if (!ui.clearScreenShown && !ui.diskOpScreenShown && !editor.errorMsgActive)
					statusAllRight();

				ui.introScreenShown = false;
			}
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN)
		{
			if (ui.sampleMarkingPos == -1 &&
				!ui.forceSampleDrag && !ui.forceVolDrag &&
				!ui.forceSampleEdit)
			{
				mouseButtonDownHandler(event.button.button);
			}
		}

		if (ui.throwExit)
		{
			editor.programRunning = false;

			if (diskop.isFilling)
			{
				diskop.isFilling = false;

				diskop.forceStopReading = true;
				SDL_WaitThread(diskop.fillThread, NULL);
			}

			if (editor.isWAVRendering)
			{
				editor.isWAVRendering = false;
				editor.abortMod2Wav = true;
				SDL_WaitThread(editor.mod2WavThread, NULL);
			}
		}
	}
}

static bool initializeVars(void)
{
	setDefaultPalette();

	editor.repeatKeyFlag = (SDL_GetModState() & KMOD_CAPS) ? true : false;

	// set key repeat rate to 49.9204Hz (Amiga PAL vblank rate)
	const double dVblankHzRatio = AMIGA_PAL_VBLANK_HZ / (double)VBLANK_HZ;
	keyb.repeatDelta = (uint64_t)floor((UINT32_MAX+1.0) * dVblankHzRatio);

	strcpy(editor.mixText, "MIX 01+02 TO 03");

	// allocate some memory

	if (!allocSamplerVars() || !allocDiskOpVars())
		goto oom;

	config.defModulesDir = (char *)calloc(PATH_MAX + 1, sizeof (char));
	config.defSamplesDir = (char *)calloc(PATH_MAX + 1, sizeof (char));
	editor.tempSample = (int8_t *)calloc(131070, 1);

	if (config.defModulesDir == NULL || config.defSamplesDir == NULL || editor.tempSample == NULL)
		goto oom;

	turnOffVoices();

	// set various non-zero values
	
	editor.vol1 = 100;
	editor.vol2 = 100;
	editor.note1 = 36;
	editor.note2 = 36;
	editor.note3 = 36;
	editor.note4 = 36;
	editor.f7Pos = 16;
	editor.f8Pos = 32;
	editor.f9Pos = 48;
	editor.f10Pos = 63;
	editor.oldNote1 = 36;
	editor.oldNote2 = 36;
	editor.oldNote3 = 36;
	editor.oldNote4 = 36;
	editor.tuningVol = 32;
	editor.sampleVol = 100;
	editor.tuningNote = 24; // C-3
	editor.metroSpeed = 4;
	editor.editMoveAdd = 1;
	editor.initialTempo = 125;
	editor.initialSpeed = 6;
	editor.resampleNote = 24;
	editor.currPlayNote = 24;
	editor.effectMacros[0] = 0x102;
	editor.effectMacros[1] = 0x202;
	editor.effectMacros[2] = 0x037;
	editor.effectMacros[3] = 0x047;
	editor.effectMacros[4] = 0x304;
	editor.effectMacros[5] = 0xF06;
	editor.effectMacros[6] = 0xC10;
	editor.effectMacros[7] = 0xC20;
	editor.effectMacros[8] = 0xE93;
	editor.effectMacros[9] = 0xA0F;
	editor.multiModeNext[0] = 2;
	editor.multiModeNext[1] = 3;
	editor.multiModeNext[2] = 4;
	editor.multiModeNext[3] = 1;
	ui.introScreenShown = true;
	editor.normalizeFiltersFlag = true;
	editor.markStartOfs = -1;
	ui.sampleMarkingPos = -1;
	ui.previousPointerMode = ui.pointerMode;

	// setup GUI text pointers
	editor.vol1Disp = &editor.vol1;
	editor.vol2Disp = &editor.vol2;
	editor.sampleToDisp = &editor.sampleTo;
	editor.lpCutOffDisp = &editor.lpCutOff;
	editor.hpCutOffDisp = &editor.hpCutOff;
	editor.samplePosDisp = &editor.samplePos;
	editor.sampleVolDisp = &editor.sampleVol;
	editor.currSampleDisp = &editor.currSample;
	editor.metroSpeedDisp = &editor.metroSpeed;
	editor.sampleFromDisp = &editor.sampleFrom;
	editor.chordLengthDisp = &editor.chordLength;
	editor.metroChannelDisp = &editor.metroChannel;
	editor.quantizeValueDisp = &config.quantizeValue;

	editor.programRunning = true;
	return true;

oom:
	showErrorMsgBox("Out of memory!");
	return false;
}

static void handleSigTerm(void)
{
	if (song->modified)
	{
		resetAllScreens();

		if (!video.fullscreen)
		{
			// de-minimize window and set focus so that the user sees the message box
			SDL_RestoreWindow(video.window);
			SDL_RaiseWindow(video.window);
		}

		ui.askScreenShown = true;
		ui.askScreenType = ASK_QUIT;

		pointerSetMode(POINTER_MODE_MSG1, NO_CARRY);
		setStatusMessage("REALLY QUIT ?", NO_CARRY);
		renderAskDialog();
	}
	else
	{
		ui.throwExit = true;
	}
}

// macOS/OS X specific routines
#ifdef __APPLE__
static void osxSetDirToProgramDirFromArgs(char **argv)
{
	char *tmpPath;
	int32_t i, tmpPathLen;

	/* OS X/macOS: hackish way of setting the current working directory to the place where we double clicked
	** on the icon (for protracker.ini loading) */

	// if we launched from the terminal, argv[0][0] would be '.'
	if (argv[0] != NULL && argv[0][0] == DIR_DELIMITER) // don't do the hack if we launched from the terminal
	{
		tmpPath = strdup(argv[0]);
		if (tmpPath != NULL)
		{
			// cut off program filename
			tmpPathLen = strlen(tmpPath);
			for (i = tmpPathLen-1; i >= 0; i--)
			{
				if (tmpPath[i] == DIR_DELIMITER)
				{
					tmpPath[i] = '\0';
					break;
				}
			}

			chdir(tmpPath); // path to binary
			chdir("../../../"); // we should now be in the directory where the config can be.

			free(tmpPath);
		}
	}
}

static bool checkIfAppWasTranslocated(int argc, char **argv)
{
	const char startOfStrToCmp[] = "/private/var/folders/";

	// this is not 100% guaranteed to work, but it's Good Enough
	if (argc > 0 && !_strnicmp(argv[0], startOfStrToCmp, sizeof (startOfStrToCmp)-1))
	{
		showErrorMsgBox(
		 "The program was translocated to a random sandbox environment for security reasons, and thus it can't find and load protracker.ini.\n\n" \
		 "Don't worry, this is normal. To fix the issue you need to move the program/.app somewhere to clear its QTN_FLAG_TRANSLOCATE flag.\n\n" \
		 "Instructions:\n" \
		 "1) Close the window.\n" \
		 "2) Move/drag (do NOT copy) the program (pt2-clone-macos) to another folder, then move it back to where it was. Don't move the folder, move the executable itself.\n" \
		 "3) Run the program again, and if you did it right it should be permanently fixed.\n\n" \
		 "This is not my fault, it's a security concept introduced in macOS 10.12 for unsigned programs downloaded and unzipped from the internet."
		);

		return true;
	}

	return false;
}
#endif

// Windows specific routines
#ifdef _WIN32
static void disableWasapi(void)
{
	// disable problematic WASAPI SDL2 audio driver on Windows (causes clicks/pops sometimes...)

	const int32_t numAudioDrivers = SDL_GetNumAudioDrivers();
	if (numAudioDrivers <= 1)
		return;

	// look for directsound and enable it if found
	for (int32_t i = 0; i < numAudioDrivers; i++)
	{
		const char *audioDriver = SDL_GetAudioDriver(i);
		if (audioDriver != NULL && strcmp("directsound", audioDriver) == 0)
		{
			SDL_setenv("SDL_AUDIODRIVER", "directsound", true);
			audio.rescanAudioDevicesSupported = false;
			return;
		}
	}

	// directsound is not available, try winmm
	for (int32_t i = 0; i < numAudioDrivers; i++)
	{
		const char *audioDriver = SDL_GetAudioDriver(i);
		if (audioDriver != NULL && strcmp("winmm", audioDriver) == 0)
		{
			SDL_setenv("SDL_AUDIODRIVER", "winmm", true);
			audio.rescanAudioDevicesSupported = false;
			return;
		}
	}

	// we didn't find directsound or winmm, let's use wasapi after all...
}

static void makeSureDirIsProgramDir(void)
{
#ifndef _DEBUG
	UNICHAR *allocPtr, *path;
	int32_t i, pathLen;

	// this can return two paths in Windows, but first one is .exe path
	path = GetCommandLineW();
	if (path == NULL)
		return;

	allocPtr = UNICHAR_STRDUP(path);
	if (allocPtr == NULL)
		return; // out of memory (but it doesn't matter)

	path = allocPtr;
	pathLen = (int32_t)UNICHAR_STRLEN(path);

	// remove first "
	if (path[0] == L'\"')
		path++;

	// end string if we find another " (there can be multiple paths)
	for (i = 0; i < pathLen; i++)
	{
		if (path[i] == L'\"')
		{
			path[i] = L'\0';
			pathLen = (int32_t)UNICHAR_STRLEN(path);
			break;
		}
	}

	// get path without filename now
	for (i = pathLen-1; i >= 0; i--)
	{
		if (i < pathLen-1 && path[i] == L'\\')
		{
			path[i] = L'\0';
			break;
		}
	}

	if (i <= 0)
	{
		// argv[0] doesn't contain the path, we're most likely in the clone's directory now
		free(allocPtr);
		return;
	}

	// "C:" -> "C:\"
	if (path[i] == L':')
		path[i + 1] = L'\\';

	SetCurrentDirectoryW(path); // this *can* fail, but it doesn't matter
	free(allocPtr);
#else
	return;
#endif
}

static bool instanceAlreadyOpen(void)
{
	hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, sharedHwndName);
	if (hMapFile != NULL)
		return true; // another instance is already open

	// no instance is open, let's created a shared memory file with hWnd in it
	oneInstHandle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof (HWND), sharedHwndName);
	if (oneInstHandle != NULL)
	{
		sharedMemBuf = (LPTSTR)MapViewOfFile(oneInstHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (HWND));
		if (sharedMemBuf != NULL)
		{
			CopyMemory((PVOID)sharedMemBuf, &video.hWnd, sizeof (HWND));
			UnmapViewOfFile(sharedMemBuf);
			sharedMemBuf = NULL;
		}
	}

	return false;
}

static bool handleSingleInstancing(int32_t argc, char **argv)
{
	SDL_SysWMinfo wmInfo;

	SDL_VERSION(&wmInfo.version);
	if (!SDL_GetWindowWMInfo(video.window, &wmInfo))
		return false;

	video.hWnd = wmInfo.info.win.window;
	if (instanceAlreadyOpen() && argc >= 2 && argv[1][0] != '\0')
	{
		sharedMemBuf = (LPTSTR)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (HWND));
		if (sharedMemBuf != NULL)
		{
			memcpy(&hWnd_to, sharedMemBuf, sizeof (HWND));

			UnmapViewOfFile(sharedMemBuf);
			sharedMemBuf = NULL;
			CloseHandle(hMapFile);
			hMapFile = NULL;

			hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, ARGV_SHARED_MEM_MAX_LEN, sharedFileName);
			if (hMapFile != NULL)
			{
				sharedMemBuf = (LPTSTR)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, ARGV_SHARED_MEM_MAX_LEN);
				if (sharedMemBuf != NULL)
				{
					strcpy((char *)sharedMemBuf, argv[1]);

					UnmapViewOfFile(sharedMemBuf);
					sharedMemBuf = NULL;

					SendMessage(hWnd_to, SYSMSG_FILE_ARG, 0, 0);
					Sleep(80); // wait a bit to make sure first instance received msg

					CloseHandle(hMapFile);
					hMapFile = NULL;

					return true; // quit instance now
				}
			}

			return true;
		}

		CloseHandle(hMapFile);
		hMapFile = NULL;
	}

	return false;
}

static void handleSysMsg(SDL_Event inputEvent)
{
	if (inputEvent.type != SDL_SYSWMEVENT)
		return;

	SDL_SysWMmsg *wmMsg = inputEvent.syswm.msg;
	if (wmMsg->subsystem == SDL_SYSWM_WINDOWS && wmMsg->msg.win.msg == SYSMSG_FILE_ARG)
	{
		hMapFile = OpenFileMapping(FILE_MAP_READ, FALSE, sharedFileName);
		if (hMapFile != NULL)
		{
			sharedMemBuf = (LPTSTR)MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, ARGV_SHARED_MEM_MAX_LEN);
			if (sharedMemBuf != NULL)
			{
				loadDroppedFile((char *)sharedMemBuf, (uint32_t)strlen(sharedMemBuf), true, true);

				UnmapViewOfFile(sharedMemBuf);
				sharedMemBuf = NULL;
			}

			CloseHandle(hMapFile);
			hMapFile = NULL;
		}
	}
}

void closeSingleInstancing(void)
{
	if (oneInstHandle != NULL)
	{
		CloseHandle(oneInstHandle);
		oneInstHandle = NULL;
	}
}

static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *ptr)
#else
static void exceptionHandler(int32_t signal)
#endif
{
#define BACKUP_FILES_TO_TRY 1000
	char fileName[32];
	uint16_t i;
	struct stat statBuffer;

#ifdef _WIN32
	(void)ptr;
	if (oneInstHandle != NULL) CloseHandle(oneInstHandle);
#else
	if (signal == 15) return;
#endif

	if (!backupMadeAfterCrash)
	{
		if (editor.modulesPathU != NULL && UNICHAR_CHDIR(editor.modulesPathU) == 0)
		{
			// find a free filename
			for (i = 1; i < 1000; i++)
			{
				sprintf(fileName, "backup%03d.mod", i);
				if (stat(fileName, &statBuffer) != 0)
					break; // file doesn't exist, we're good
			}

			if (i != 1000)
				modSave(fileName);
		}

		backupMadeAfterCrash = true; // set this flag to prevent multiple backups from being saved at once
		showErrorMsgBox(CRASH_TEXT);
	}

#ifdef _WIN32
	return EXCEPTION_CONTINUE_SEARCH;
#endif
}

static void cleanUp(void) // never call this inside the main loop!
{
	modStop();
	modFree();
	audioClose();
	deAllocSamplerVars();
	freeDiskOpMem();
	freeDiskOpEntryMem();
	freeBMPs();
	videoClose();
	freeSprites();
	freeAudioDeviceList(); // pt2_sampling.c
	freeKaiserTable(); // pt2_sampling.c

	if (config.defModulesDir != NULL) free(config.defModulesDir);
	if (config.defSamplesDir != NULL) free(config.defSamplesDir);
	if (editor.tempSample != NULL) free(editor.tempSample);

#ifdef _WIN32
#ifndef _DEBUG
	UnhookWindowsHookEx(g_hKeyboardHook);
#endif
	if (oneInstHandle != NULL)
	{
		CloseHandle(oneInstHandle);
		oneInstHandle = NULL;
	}
#endif
}
