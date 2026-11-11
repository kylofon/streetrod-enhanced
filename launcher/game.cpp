// game.cpp -- the files the game needs, the data disk, saved games and starting the game.
#include "game.h"

#include <wx/file.h>

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <vector>

namespace {

// Whether `dir` holds the file `name` as written, in upper or in lower case (case matters outside
// Windows; the port itself finds files whatever their case).
bool FilePresent(const wxString& dir, const wxString& name) {
    if (dir.empty()) return false;
    for (const wxString& n : {name, name.Upper(), name.Lower()})
        if (wxFileName::FileExists(wxFileName(dir, n).GetFullPath())) return true;
    return false;
}

// The first file the port needs that `dir` lacks (SR.EXE or SRSE.EXE, LIB1, LIB2, HOT_DATA).
wxString Missing(const wxString& dir, bool* se) {
    *se = !FilePresent(dir, "SR.EXE") && FilePresent(dir, "SRSE.EXE");
    if (!*se && !FilePresent(dir, "SR.EXE")) return "SR.EXE";
    for (const char* name : {"LIB1", "LIB2", "HOT_DATA"})
        if (!FilePresent(dir, name)) return name;
    return wxString();
}

}  // namespace

GameFolder ReadGameFolder(const wxString& dir) {
    GameFolder f;
    if (dir.empty()) {
        f.missing = "SR.EXE";
        return f;
    }
    f.missing = Missing(dir, &f.se);
    for (int n = 1; n <= 15; ++n)
        if (FilePresent(dir, wxString::Format("HOTROD%d.SAV", n))) ++f.saves;
    const wxString disk = wxFileName(dir, "datadisk").GetFullPath();
    bool se = false;
    if (wxFileName::DirExists(disk) && Missing(disk, &se).empty()) f.dataDisk = disk;
    return f;
}

wxString LauncherDir() { return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(); }

wxString DefaultGameDir() { return wxFileName(LauncherDir(), "Game").GetFullPath(); }

wxString DefaultProgram() {
    wxFileName name(LauncherDir(), "streetrod-enhanced");
#ifdef __WXMSW__
    name.SetExt("exe");
#endif
    return name.GetFullPath();
}

bool IsDosProgram(const wxString& path) {
    wxFile f;
    if (!wxFileName::FileExists(path) || !f.Open(path)) return false;
    unsigned char h[0x40] = {};
    if (f.Read(h, sizeof h) != (ssize_t)sizeof h || h[0] != 'M' || h[1] != 'Z') return false;
    const wxFileOffset pe = h[0x3C] | h[0x3D] << 8 | h[0x3E] << 16 | (wxFileOffset)h[0x3F] << 24;
    unsigned char sig[4] = {};
    if (pe <= 0 || pe >= f.Length() - 4 || f.Seek(pe) != pe || f.Read(sig, 4) != 4) return true;
    return !(sig[0] == 'P' && sig[1] == 'E' && sig[2] == 0 && sig[3] == 0);
}

bool LaunchGame(const GameOptions& o, wxString& error) {
    if (!wxFileName::FileExists(o.program)) {
        error = wxString::Format("The game's program isn't there:\n\n%s", o.program);
        return false;
    }
    if (IsDosProgram(o.program)) {
        error = wxString::Format("%s is the original DOS game, which Windows can't run. The program to start "
                                 "is streetrod-enhanced.exe:\n\n%s", wxFileName(o.program).GetFullName(),
                                 DefaultProgram());
        return false;
    }
    std::vector<wxString> args{o.program,
                               "--game-dir", o.gameDir,
                               "--scale", wxString::Format("%d", o.scale)};
    if (o.fullscreen) args.push_back("--fullscreen");
    if (o.uncapped) args.insert(args.end(), {"--fps", "0", "--vsync", "off"});
    if (o.classic) {
        args.push_back("--classic");
    } else {
        args.insert(args.end(), {"--res-scale", wxString::Format("%d", o.resScale),
                                 "--aa", wxString::Format("%d", o.aa), "--town", o.town ? "on" : "off"});
        if (o.drawDistance != 60)
            args.insert(args.end(), {"--draw-distance", wxString::Format("%d", o.drawDistance)});
        if (!o.reducedSky)
            args.insert(args.end(), {"--reduced-sky-rotation", "off"});
    }
    // The volumes only when turned down, so the default command line stays as it was.
    if (o.sounds.music != 100) args.insert(args.end(), {"--music-volume", wxString::Format("%d", o.sounds.music)});
    if (o.sounds.effects != 100) args.insert(args.end(), {"--sfx-volume", wxString::Format("%d", o.sounds.effects)});
    // The original's own command-line switches, passed on as the port receives them.
    if (o.noMouse) args.push_back("nomouse");
    if (o.demo) args.push_back("demo");
    if (o.autoDrive) args.push_back("auto");

    std::vector<std::wstring> wide;
    for (const wxString& a : args) wide.push_back(a.ToStdWstring());
    std::vector<const wchar_t*> argv;
    for (const std::wstring& w : wide) argv.push_back(w.c_str());
    argv.push_back(nullptr);

    wxExecuteEnv env;  // an empty variable map: the game inherits the launcher's environment
    env.cwd = wxFileName(o.program).GetPath();
    long pid;
    {
        wxLogNull quiet;  // wxExecute would show its own error box
        pid = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &env);
    }
    if (pid == 0) {
        error = wxString::Format("Couldn't start %s.\n\n%s", wxFileName(o.program).GetFullName(),
                                 wxSysErrorMsgStr(wxSysErrorCode()));
        return false;
    }
    return true;
}
