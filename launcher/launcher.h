// launcher.h -- the launcher window: choose the game folder and the cars (the original ones or the
// Street Rod SE data disk's), the game's own switches and the display options, then Play. Game settings >
// Sounds shows its page in the window.
#pragma once

#include <wx/frame.h>

#include "game.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSimplebook;
class wxSlider;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherFrame : public wxFrame {
public:
    LauncherFrame();

private:
    wxWindow* CreateMainPage(wxWindow* parent);
    wxWindow* CreateSoundsPage(wxWindow* parent);
    void CreateMenus();
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    // Checks the folder and the program again and enables Play and the controls to match.
    void UpdateState();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Preferences();
    void SetTopmost(bool on);
    void Save();
    // The folder the game runs in: the chosen folder, or its datadisk folder for the SE cars.
    wxString PlayFolder() const;

    void FitPage();
    // A settings page in place of the main one, and back.
    void OpenPage(int page);
    void ClosePage();
    // Sounds: the controls from / to a Sounds.
    void ShowSounds(const Sounds& s);
    Sounds EditedSounds() const;

    wxSimplebook* book_ = nullptr;
    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* cars_ = nullptr;     // the original cars / the data disk's
    wxStaticText* carsNote_ = nullptr;
    wxCheckBox* demo_ = nullptr;
    wxCheckBox* autoDrive_ = nullptr;
    wxCheckBox* noMouse_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxChoice* graphics_ = nullptr;   // enhanced / original
    wxChoice* resolution_ = nullptr;
    wxChoice* aa_ = nullptr;
    wxCheckBox* town_ = nullptr;
    wxButton* play_ = nullptr;
    // The Sounds page: a slider and a number field for each volume.
    wxSlider* musicSlider_ = nullptr;
    wxSpinCtrl* musicField_ = nullptr;
    wxSlider* effectsSlider_ = nullptr;
    wxSpinCtrl* effectsField_ = nullptr;
    Sounds sounds_;  // saved

    GameFolder game_;
    bool topmost_ = false;
    bool loading_ = true;  // no edits are recorded while the window is built
};
