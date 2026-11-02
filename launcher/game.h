// game.h -- what a game folder holds (Street Rod, or the Street Rod SE data disk in its datadisk
// folder), where streetrod-enhanced is, and starting it.
#pragma once

#include <wx/string.h>

// A game folder as the launcher sees it.
struct GameFolder {
    wxString missing;       // the first of the game's files that isn't there, empty if all are
    bool se = false;        // the program is SRSE.EXE (Street Rod SE, the data-disk edition)
    int saves = 0;          // saved games (HOTROD1..15.SAV)
    wxString dataDisk;      // <folder>/datadisk when it holds a complete Street Rod SE, else empty
};

GameFolder ReadGameFolder(const wxString& dir);

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and streetrod-enhanced(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

// True if `path` is a DOS program (MZ without a Windows PE header), such as the original SR.EXE: Windows
// can't start it, and it isn't the port.
bool IsDosProgram(const wxString& path);

// Game settings > Sounds: the volumes in percent, 0..100.
struct Sounds {
    int music = 100;        // --music-volume: the background tune
    int effects = 100;      // --sfx-volume: the effects, the engine and the garage's loop sounds
};

struct GameOptions {
    wxString program;       // streetrod-enhanced(.exe)
    wxString gameDir;       // --game-dir
    bool demo = false;      // the game's "demo" switch: it plays itself
    bool autoDrive = false; // the game's "auto" switch: races drive themselves
    bool noMouse = false;   // the game's "nomouse" switch
    int scale = 3;          // --scale: the window is 320x240 times this
    bool fullscreen = false;  // --fullscreen
    bool uncapped = false;  // --fps 0 --vsync off: as many frames as the computer draws
    bool classic = false;   // --classic: the original's picture only
    int resScale = 4;       // --res-scale: the picture is 320x200 times this
    int aa = 2;             // --aa: anti-aliasing, aa x aa samples per pixel
    bool town = true;       // --town on|off: the drives through town enhanced too
    Sounds sounds;
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
