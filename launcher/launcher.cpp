// launcher.cpp -- the launcher window, its menus and the Sounds page.
#include "launcher.h"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#ifdef __WXMSW__
#include <wx/msw/wrapcctl.h>
#include <shellapi.h>
#endif

#include "icon.h"
#include "settings.h"
#include "version.h"

const char* const APP_TITLE = "Street Rod Enhanced";

namespace {

const char* const WEBSITE = "https://kkania.com";
const char* const SUPPORT = "https://buymeacoffee.com/krzysztofkania";
const char* const SECTION = "Game";
const char* const SOUND = "Sound";

const int ID_SOUNDS = wxID_HIGHEST + 1, ID_ABOUT = wxID_HIGHEST + 2;
// The book's pages.
const int PAGE_MAIN = 0, PAGE_SOUNDS = 1;

const int MIN_SCALE = 1, MAX_SCALE = 6, DEFAULT_SCALE = 3;
const int MIN_RES = 2, MAX_RES = 8, DEFAULT_RES = 4;     // --res-scale
const int MAX_AA = 4, DEFAULT_AA = 2;                    // --aa (1 = off)

#ifdef __WXMSW__
HRESULT CALLBACK AboutCallback(HWND hwnd, UINT msg, WPARAM, LPARAM lp, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(hwnd, L"open", reinterpret_cast<LPCWSTR>(lp), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
#else
// A label and a link on one line, for the portable About box.
void AddLink(wxWindow* parent, wxSizer* sizer, const wxString& label, const wxString& text, const wxString& url) {
    auto* line = new wxBoxSizer(wxHORIZONTAL);
    line->Add(new wxStaticText(parent, wxID_ANY, label + " "), 0, wxALIGN_CENTER_VERTICAL);
    line->Add(new wxHyperlinkCtrl(parent, wxID_ANY, text, url), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(line);
}
#endif

wxStaticText* GreyText(wxWindow* parent, const wxString& text = wxEmptyString) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    return label;
}

// A label, a text field and a Browse button in a row of a two-column grid.
wxTextCtrl* PathRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& tip,
                    wxButton** browse) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* text = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(300), -1));
    text->SetToolTip(tip);
    *browse = new wxButton(parent, wxID_ANY, "B&rowse...");
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(8));
    row->Add(*browse, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(row, 1, wxEXPAND);
    return text;
}

// A label, a slider, a number field and a note in a row of a four-column grid; the slider and the field
// follow each other.
void SliderRow(wxWindow* parent, wxFlexGridSizer* grid, const wxString& label, const wxString& note,
               const wxString& tip, int min, int max, int step, wxSlider** slider, wxSpinCtrl** spin) {
    grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    *slider = new wxSlider(parent, wxID_ANY, min, min, max, wxDefaultPosition, wxSize(parent->FromDIP(220), -1));
    (*slider)->SetPageSize(step);
    (*slider)->SetLineSize(1);
    (*slider)->SetToolTip(tip);
    *spin = new wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(parent->FromDIP(64), -1),
                           wxSP_ARROW_KEYS, min, max, min);
    (*spin)->SetToolTip(tip);
    grid->Add(*slider, 0, wxALIGN_CENTER_VERTICAL | wxEXPAND);
    grid->Add(*spin, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(GreyText(parent, note), 0, wxALIGN_CENTER_VERTICAL);
    wxSlider* s = *slider;
    wxSpinCtrl* n = *spin;
    s->Bind(wxEVT_SLIDER, [s, n](wxCommandEvent&) { n->SetValue(s->GetValue()); });
    n->Bind(wxEVT_SPINCTRL, [s, n](wxSpinEvent&) { s->SetValue(n->GetValue()); });
    // A typed number moves the slider as it is typed (the field keeps it in range when it loses focus).
    n->Bind(wxEVT_TEXT, [s, n](wxCommandEvent& event) {
        long v;
        if (event.GetString().ToLong(&v) && v >= n->GetMin() && v <= n->GetMax()) s->SetValue(static_cast<int>(v));
        event.Skip();
    });
}

wxString SavesText(int n) {
    return n == 0 ? wxString("no saved games") : n == 1 ? wxString("1 saved game") : wxString::Format("%d saved games", n);
}

}  // namespace

LauncherFrame::LauncherFrame()
    : wxFrame(nullptr, wxID_ANY, APP_TITLE, wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)) {
    SetIcons(AppIcons());
    topmost_ = settings::GetInt("Preferences", "AlwaysOnTop", 0) != 0;
    if (topmost_) SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    sounds_.music = wxMax(0, wxMin(100, settings::GetInt(SOUND, "MusicVolume", sounds_.music)));
    sounds_.effects = wxMax(0, wxMin(100, settings::GetInt(SOUND, "EffectsVolume", sounds_.effects)));

    // The launcher and, in its place while one is open, a settings page (PAGE_*).
    book_ = new wxSimplebook(this);
    book_->AddPage(CreateMainPage(book_), "Game");
    book_->AddPage(CreateSoundsPage(book_), "Sounds");
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(book_, 1, wxEXPAND);
    SetSizer(all);
    CreateMenus();

    // Settings from the last run.
    wxString dir = settings::GetString(SECTION, "GameFolder", "");
    wxString program = settings::GetString(SECTION, "Program", "");
    folder_->ChangeValue(dir.empty() ? DefaultGameDir() : dir);
    program_->ChangeValue(program.empty() ? DefaultProgram() : program);
    // The data disk's cars are the default (key "Cars": 1 = data disk, 0 = original). Launchers before
    // 0.1.0 stored DataDisk=0 without asking; that old key is no longer read.
    cars_->SetSelection(settings::GetInt(SECTION, "Cars", 1) != 0 ? 1 : 0);
    demo_->SetValue(settings::GetInt(SECTION, "Demo", 0) != 0);
    autoDrive_->SetValue(settings::GetInt(SECTION, "Auto", 0) != 0);
    noMouse_->SetValue(settings::GetInt(SECTION, "NoMouse", 0) != 0);
    scale_->SetSelection(
        wxMax(MIN_SCALE, wxMin(MAX_SCALE, settings::GetInt(SECTION, "Scale", DEFAULT_SCALE))) - MIN_SCALE);
    fullscreen_->SetValue(settings::GetInt(SECTION, "Fullscreen", 0) != 0);
    frameRate_->SetSelection(settings::GetInt(SECTION, "Uncapped", 0) != 0 ? 1 : 0);
    graphics_->SetSelection(settings::GetInt(SECTION, "Classic", 0) != 0 ? 1 : 0);
    resolution_->SetSelection(
        wxMax(MIN_RES, wxMin(MAX_RES, settings::GetInt(SECTION, "ResScale", DEFAULT_RES))) - MIN_RES);
    aa_->SetSelection(wxMax(1, wxMin(MAX_AA, settings::GetInt(SECTION, "AA", DEFAULT_AA))) - 1);
    town_->SetValue(settings::GetInt(SECTION, "Town", 1) != 0);
    ShowSounds(sounds_);
    loading_ = false;
    UpdateState();

    Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
        if (event.GetActive()) UpdateState();  // files may have been copied in, games saved meanwhile
        event.Skip();
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        Save();
        Destroy();
    });

    FitPage();
    if (!settings::RestoreWindowPosition(SECTION, this)) Centre();
}

wxWindow* LauncherFrame::CreateMainPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    // Game files
    auto* filesBox = new wxStaticBoxSizer(wxVERTICAL, page, "Game files");
    wxWindow* fb = filesBox->GetStaticBox();
    auto* filesGrid = new wxFlexGridSizer(2, gap, gap);
    filesGrid->AddGrowableCol(1);
    wxButton* browseFolder = nullptr;
    wxButton* browseProgram = nullptr;
    folder_ = PathRow(fb, filesGrid, "&Folder:",
                      "The folder with the original game's files (SR.EXE, LIB1, LIB2, HOT_DATA ...).", &browseFolder);
    program_ = PathRow(fb, filesGrid, "&Program:", "streetrod-enhanced, the game.", &browseProgram);
    auto* statusRow = new wxBoxSizer(wxHORIZONTAL);
    statusIcon_ = new wxStaticBitmap(fb, wxID_ANY, wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
    statusNote_ = new wxStaticText(fb, wxID_ANY, wxEmptyString);
    statusRow->Add(statusIcon_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, small);
    statusRow->Add(statusNote_, 1, wxALIGN_CENTER_VERTICAL);
    filesBox->Add(filesGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    filesBox->Add(statusRow, 0, wxEXPAND | wxALL, gap);
    folder_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    program_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        if (!loading_) UpdateState();
    });
    browseFolder->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseFolder(); });
    browseProgram->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseProgram(); });

    // Start
    auto* startBox = new wxStaticBoxSizer(wxVERTICAL, page, "Start");
    wxWindow* sb = startBox->GetStaticBox();
    auto* carsRow = new wxBoxSizer(wxHORIZONTAL);
    carsRow->Add(new wxStaticText(sb, wxID_ANY, "&Cars:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    cars_ = new wxChoice(sb, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(300), -1));
    cars_->Append("The original cars");
    cars_->Append("Street Rod SE: the data disk's 25 cars (default)");
    cars_->SetToolTip("Street Rod SE (in the game folder's datadisk folder) replaces the cars with those of the "
                      "Street Rod car data disks. It keeps its own saved games.");
    carsRow->Add(cars_, 0, wxALIGN_CENTER_VERTICAL);
    startBox->Add(carsRow, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    carsNote_ = GreyText(sb);
    startBox->Add(carsNote_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    startBox->Add(GreyText(sb, "The game's own switches:"), 0, wxLEFT | wxRIGHT | wxTOP, gap);
    demo_ = new wxCheckBox(sb, wxID_ANY, "&Demo: the game plays itself (Esc ends it)");
    demo_->SetToolTip("The original's \"demo\" switch: a self-running game with $10,000 and an opponent called "
                      "The Geek.");
    autoDrive_ = new wxCheckBox(sb, wxID_ANY, "Races &drive themselves");
    autoDrive_->SetToolTip("The original's \"auto\" switch: the computer steers, accelerates and shifts in races.");
    noMouse_ = new wxCheckBox(sb, wxID_ANY, "&No mouse (keyboard and joystick only)");
    noMouse_->SetToolTip("The original's \"nomouse\" switch.");
    // One column, indented like the rows above: the same left margin (gap) for every checkbox.
    auto* switches = new wxBoxSizer(wxVERTICAL);
    switches->Add(demo_);
    switches->Add(autoDrive_, 0, wxTOP, small);
    switches->Add(noMouse_, 0, wxTOP, small);
    startBox->Add(switches, 0, wxALL, gap);
    cars_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateState(); });

    // Options
    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, page, "Options");
    wxWindow* ob = optionsBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->Add(new wxStaticText(ob, wxID_ANY, "&Window size:"), 0, wxALIGN_CENTER_VERTICAL);
    scale_ = new wxChoice(ob, wxID_ANY);
    for (int s = MIN_SCALE; s <= MAX_SCALE; ++s) scale_->Append(wxString::Format(L"%d × %d", 320 * s, 240 * s));
    scale_->SetToolTip("The window's size when the game starts. Alt+Enter switches to full screen.");
    grid->Add(scale_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(new wxStaticText(ob, wxID_ANY, "Fra&me rate:"), 0, wxALIGN_CENTER_VERTICAL);
    frameRate_ = new wxChoice(ob, wxID_ANY);
    frameRate_->Append("Display refresh rate (VSync)");
    frameRate_->Append("Uncapped (no VSync)");
    frameRate_->SetToolTip("The display's refresh rate: a new picture for every refresh, without tearing. "
                           "Uncapped: as many pictures as the computer draws, which may tear.");
    grid->Add(frameRate_, 0, wxALIGN_CENTER_VERTICAL);
    optionsBox->Add(grid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    fullscreen_ = new wxCheckBox(ob, wxID_ANY, "Start in f&ull screen (Alt+Enter switches)");
    optionsBox->Add(fullscreen_, 0, wxALL, gap);

    // Keys
    auto* keysBox = new wxStaticBoxSizer(wxVERTICAL, page, "Keys in the game");
    wxWindow* kb = keysBox->GetStaticBox();
    auto* keys = new wxFlexGridSizer(2, small, FromDIP(16));
    const char* const KEYS[][2] = {
        {"Mouse", "point and click (the game draws its own pointer)"},
        {"Arrows / keypad", "move the pointer"},
        {"Space  Ins  keypad 0", "click"},
        {"", ""},
        {"Races:", ""},
        {"Up  Down", "gas, brake"},
        {"Left  Right", "steer"},
        {"Space  Ins", "shift (down when braking, else up)"},
        {"A  Z", "gear up / down (added by the port)"},
        {"Mouse", "right button gas, move down brake,"},
        {"", "sideways steer, left button shift"},
        {"", ""},
        {"Esc", "skip the drive through town; end the demo"},
        {"M", "music on / off"},
        {"Ctrl", "sound on / off"},
        {"Alt+Enter", "full screen"},
    };
    for (const auto& k : KEYS) {
        keys->Add(new wxStaticText(kb, wxID_ANY, k[0]));
        keys->Add(GreyText(kb, k[1]));
    }
    keysBox->Add(keys, 0, wxALL, gap);

    // Picture: the enhanced road view
    auto* pictureBox = new wxStaticBoxSizer(wxVERTICAL, page, "Picture");
    wxWindow* pb = pictureBox->GetStaticBox();
    auto* pgrid = new wxFlexGridSizer(2, gap, gap);
    pgrid->Add(new wxStaticText(pb, wxID_ANY, "&Graphics:"), 0, wxALIGN_CENTER_VERTICAL);
    graphics_ = new wxChoice(pb, wxID_ANY);
    graphics_->Append("Enhanced: smooth, high-resolution road (default)");
    graphics_->Append("Original: the game's own picture");
    graphics_->SetToolTip("Enhanced draws the road view of races and town drives again at a high resolution, "
                          "moving smoothly at your screen's refresh rate. Original shows the game as it was.");
    pgrid->Add(graphics_, 0, wxALIGN_CENTER_VERTICAL);
    pgrid->Add(new wxStaticText(pb, wxID_ANY, "&Resolution:"), 0, wxALIGN_CENTER_VERTICAL);
    resolution_ = new wxChoice(pb, wxID_ANY);
    for (int s = MIN_RES; s <= MAX_RES; ++s) resolution_->Append(wxString::Format(L"%d × %d", 320 * s, 200 * s));
    resolution_->SetToolTip("The size of the picture the game draws; the window scales it.");
    pgrid->Add(resolution_, 0, wxALIGN_CENTER_VERTICAL);
    pgrid->Add(new wxStaticText(pb, wxID_ANY, "&Anti-aliasing:"), 0, wxALIGN_CENTER_VERTICAL);
    aa_ = new wxChoice(pb, wxID_ANY);
    aa_->Append("Off");
    for (int a = 2; a <= MAX_AA; ++a) aa_->Append(wxString::Format(L"%d × %d", a, a));
    aa_->SetToolTip("Smooth edges: samples per pixel. Higher costs more time per frame.");
    pgrid->Add(aa_, 0, wxALIGN_CENTER_VERTICAL);
    pictureBox->Add(pgrid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    town_ = new wxCheckBox(pb, wxID_ANY, "Drives through &town enhanced too");
    pictureBox->Add(town_, 0, wxALL, gap);
    graphics_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateState(); });

    // Buttons
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    play_ = new wxButton(page, wxID_ANY, "&Play");
    auto* close = new wxButton(page, wxID_CLOSE, "Close");
    buttons->AddStretchSpacer();
    buttons->Add(play_, 0, wxRIGHT, gap);
    buttons->Add(close);
    play_->SetDefault();
    play_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Play(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });

    // Two columns: the files, the start and the options on the left, the keys on the right.
    auto* left = new wxBoxSizer(wxVERTICAL);
    left->Add(filesBox, 0, wxEXPAND);
    left->Add(startBox, 0, wxEXPAND | wxTOP, margin);
    left->Add(optionsBox, 1, wxEXPAND | wxTOP, margin);
    auto* right = new wxBoxSizer(wxVERTICAL);
    right->Add(keysBox, 0, wxEXPAND);
    right->Add(pictureBox, 1, wxEXPAND | wxTOP, margin);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    columns->Add(left, 0, wxEXPAND);
    columns->Add(right, 0, wxEXPAND | wxLEFT, margin);
    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(columns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

wxWindow* LauncherFrame::CreateSoundsPage(wxWindow* parent) {
    auto* page = new wxPanel(parent);
    const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

    auto* volumeBox = new wxStaticBoxSizer(wxVERTICAL, page, "Volume");
    wxWindow* vb = volumeBox->GetStaticBox();
    auto* grid = new wxFlexGridSizer(4, small, gap);
    grid->AddGrowableCol(1);
    SliderRow(vb, grid, "&Music:", "% (the background tune)",
              "The volume of the background tune of the garage and the town. M switches the music on and off "
              "in the game.",
              0, 100, 10, &musicSlider_, &musicField_);
    SliderRow(vb, grid, "Sound &effects:", "% (the engine, clicks, crashes, garage tools)",
              "The volume of everything else: the engine and the police siren, the tyres, crashes, the "
              "buttons' clicks and the garage's tools.",
              0, 100, 10, &effectsSlider_, &effectsField_);
    volumeBox->Add(grid, 0, wxEXPAND | wxALL, gap);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* defaults = new wxButton(page, wxID_ANY, "&Default");
    defaults->SetToolTip("Put both volumes back to 100 %.");
    auto* apply = new wxButton(page, wxID_APPLY, "&Apply");
    apply->SetToolTip("Keep these volumes and go back.");
    auto* cancel = new wxButton(page, wxID_CANCEL, "Cancel");
    cancel->SetToolTip("Go back without changing the volumes.");
    buttons->Add(defaults);
    buttons->AddStretchSpacer();
    buttons->Add(apply, 0, wxRIGHT, gap);
    buttons->Add(cancel);
    defaults->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowSounds(Sounds()); });
    apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        sounds_ = EditedSounds();
        settings::SetInt(SOUND, "MusicVolume", sounds_.music);
        settings::SetInt(SOUND, "EffectsVolume", sounds_.effects);
        ClosePage();
    });
    cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ClosePage(); });

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(volumeBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
    all->Add(buttons, 0, wxEXPAND | wxALL, margin);
    page->SetSizer(all);
    return page;
}

void LauncherFrame::ShowSounds(const Sounds& s) {
    musicField_->SetValue(s.music);
    musicSlider_->SetValue(s.music);
    effectsField_->SetValue(s.effects);
    effectsSlider_->SetValue(s.effects);
}

Sounds LauncherFrame::EditedSounds() const {
    // The sliders have the numbers as typed, even while a field hasn't taken its number yet.
    Sounds s;
    s.music = musicSlider_->GetValue();
    s.effects = effectsSlider_->GetValue();
    return s;
}

void LauncherFrame::CreateMenus() {
    auto* file = new wxMenu;
    file->Append(wxID_PREFERENCES, wxString::FromUTF8("&Preferences…"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    auto* settingsMenu = new wxMenu;
    settingsMenu->Append(ID_SOUNDS, wxString::FromUTF8("&Sounds…"));

    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(settingsMenu, "&Game settings");
#ifndef __WXMSW__
    auto* about = new wxMenu;
    about->Append(ID_ABOUT, wxString::FromUTF8("&About ") + APP_TITLE + wxString::FromUTF8("…"));
    bar->Append(about, "A&bout");
#endif
    SetMenuBar(bar);
#ifdef __WXMSW__
    // A top-level About item that acts straight away, which wxWidgets menus
    // can't express; MSWWindowProc handles its command.
    const HMENU native = static_cast<HMENU>(bar->GetHMenu());
    AppendMenuW(native, MF_STRING, ID_ABOUT, L"A&bout");
    DrawMenuBar(static_cast<HWND>(GetHWND()));
#endif

    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Preferences(); }, wxID_PREFERENCES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPage(PAGE_SOUNDS); }, ID_SOUNDS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { About(); }, ID_ABOUT);
}

#ifdef __WXMSW__
WXLRESULT LauncherFrame::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) {
    if (msg == WM_COMMAND && LOWORD(wParam) == ID_ABOUT && lParam == 0) {
        CallAfter([this] { About(); });
        return 0;
    }
    return wxFrame::MSWWindowProc(msg, wParam, lParam);
}
#endif

wxString LauncherFrame::PlayFolder() const {
    if (cars_->GetSelection() == 1 && !game_.dataDisk.empty()) return game_.dataDisk;
    return folder_->GetValue();
}

void LauncherFrame::UpdateState() {
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    game_ = ReadGameFolder(dir);
    const bool dosProgram = IsDosProgram(program);
    const bool haveProgram = wxFileName::FileExists(program) && !dosProgram;

    // The data disk choice: only when the folder has a complete datadisk folder.
    const bool diskChoice = game_.missing.empty() && !game_.dataDisk.empty();
    cars_->Enable(diskChoice);
    const bool useDisk = diskChoice && cars_->GetSelection() == 1;
    const GameFolder played = useDisk ? ReadGameFolder(game_.dataDisk) : game_;
    if (!game_.missing.empty())
        carsNote_->SetLabel(wxEmptyString);
    else if (useDisk)
        carsNote_->SetLabel(wxString::Format("Plays in %s: %s.", wxFileName(game_.dataDisk).GetFullName(),
                                             SavesText(played.saves)));
    else if (game_.se)
        carsNote_->SetLabel("This folder is Street Rod SE: its cars are the data disk's.");
    else if (game_.dataDisk.empty())
        carsNote_->SetLabel("No datadisk folder with Street Rod SE here: the original cars are used.");
    else
        carsNote_->SetLabel(wxEmptyString);

    wxString note;
    if (!game_.missing.empty())
        note = wxString::Format("This folder needs the game's files: %s is missing.", game_.missing);
    else if (dosProgram)
        note = wxString::Format("%s is the original DOS game. Program should be streetrod-enhanced.exe.",
                                wxFileName(program).GetFullName());
    else if (!haveProgram)
        note = wxString::Format("%s isn't there.", wxFileName(program).GetFullName());
    else
        note = wxString::Format("Found %s, %s.", game_.se ? "Street Rod SE" : "Street Rod", SavesText(game_.saves));
    const bool ok = haveProgram && game_.missing.empty();
    statusIcon_->Show(!ok);
    statusNote_->SetLabel(note);
    play_->Enable(ok);
    const bool enhanced = graphics_->GetSelection() == 0;
    resolution_->Enable(enhanced);
    aa_->Enable(enhanced);
    town_->Enable(enhanced);
    book_->GetPage(PAGE_MAIN)->Layout();
    if (book_->GetSelection() == PAGE_MAIN) FitPage();  // the notes change the page's size
}

void LauncherFrame::BrowseFolder() {
    wxDirDialog dialog(this, "Choose the folder with the game's files", folder_->GetValue(),
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) folder_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::BrowseProgram() {
    wxFileName current(program_->GetValue());
#ifdef __WXMSW__
    const char* const filter = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
    const char* const filter = "All files|*";
#endif
    wxFileDialog dialog(this, "Choose streetrod-enhanced", current.GetPath(), current.GetFullName(), filter,
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) program_->SetValue(dialog.GetPath());  // raises wxEVT_TEXT
}

void LauncherFrame::Play() {
    Save();
    GameOptions options;
    options.program = wxFileName(program_->GetValue()).GetFullPath();
    options.gameDir = wxFileName(PlayFolder()).GetFullPath();
    options.demo = demo_->GetValue();
    options.autoDrive = autoDrive_->GetValue();
    options.noMouse = noMouse_->GetValue();
    options.scale = scale_->GetSelection() + MIN_SCALE;
    options.fullscreen = fullscreen_->GetValue();
    options.uncapped = frameRate_->GetSelection() == 1;
    options.classic = graphics_->GetSelection() == 1;
    options.resScale = resolution_->GetSelection() + MIN_RES;
    options.aa = aa_->GetSelection() + 1;
    options.town = town_->GetValue();
    options.sounds = sounds_;
    wxString error;
    if (!LaunchGame(options, error)) wxMessageBox(error, APP_TITLE, wxOK | wxICON_ERROR, this);
}

void LauncherFrame::Save() {
    // A folder or program left at its default is stored empty, so it follows the launcher if it moves.
    const wxString dir = folder_->GetValue(), program = program_->GetValue();
    settings::SetString(SECTION, "GameFolder",
                        wxFileName(dir).SameAs(wxFileName(DefaultGameDir())) ? wxString() : dir);
    settings::SetString(SECTION, "Program",
                        wxFileName(program).SameAs(wxFileName(DefaultProgram())) ? wxString() : program);
    settings::SetInt(SECTION, "Cars", cars_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "Demo", demo_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Auto", autoDrive_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "NoMouse", noMouse_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Scale", scale_->GetSelection() + MIN_SCALE);
    settings::SetInt(SECTION, "Fullscreen", fullscreen_->GetValue() ? 1 : 0);
    settings::SetInt(SECTION, "Uncapped", frameRate_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "Classic", graphics_->GetSelection() == 1 ? 1 : 0);
    settings::SetInt(SECTION, "ResScale", resolution_->GetSelection() + MIN_RES);
    settings::SetInt(SECTION, "AA", aa_->GetSelection() + 1);
    settings::SetInt(SECTION, "Town", town_->GetValue() ? 1 : 0);
    settings::SaveWindowPosition(SECTION, this);
}

void LauncherFrame::Preferences() {
    wxDialog dialog(this, wxID_ANY, "Preferences");
    auto* all = new wxBoxSizer(wxVERTICAL);
    auto* topmost = new wxCheckBox(&dialog, wxID_ANY, "Always on &top");
    topmost->SetValue(topmost_);
    topmost->SetToolTip("Keep the launcher above other windows.");
    all->Add(topmost, 0, wxALL, dialog.FromDIP(12));
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
             dialog.FromDIP(12));
    dialog.SetSizerAndFit(all);
    dialog.SetMinSize(wxSize(dialog.FromDIP(300), -1));
    dialog.Fit();
    dialog.CentreOnParent();
    topmost->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return;
    if (topmost->GetValue() != topmost_) SetTopmost(topmost->GetValue());
}

void LauncherFrame::SetTopmost(bool on) {
    topmost_ = on;
    const long style = GetWindowStyleFlag();
    SetWindowStyleFlag(on ? style | wxSTAY_ON_TOP : style & ~wxSTAY_ON_TOP);
    settings::SetInt("Preferences", "AlwaysOnTop", on ? 1 : 0);
}

// The window takes the size of the page shown (the book on its own would take the largest page's).
void LauncherFrame::FitPage() {
    SetClientSize(book_->GetCurrentPage()->GetBestSize());
}

// A settings page starts from the saved settings; the menu's page waits until it is closed.
void LauncherFrame::OpenPage(int page) {
    if (book_->GetSelection() == page) return;
    ShowSounds(sounds_);
    book_->ChangeSelection(page);
    FitPage();
    GetMenuBar()->Enable(ID_SOUNDS, false);
    SetDefaultItem(book_->GetPage(page)->FindWindow(wxID_APPLY));
    musicSlider_->SetFocus();
}

void LauncherFrame::ClosePage() {
    book_->ChangeSelection(PAGE_MAIN);
    UpdateState();  // fits the window to the main page
    GetMenuBar()->Enable(ID_SOUNDS, true);
    SetDefaultItem(play_);
    play_->SetFocus();
}

void LauncherFrame::About() {
    const wxString title = wxString("About ") + APP_TITLE;
    const wxString heading = wxString(APP_TITLE) + " " + APP_VERSION_TEXT;
    const wxString blurb = "Starts streetrod-enhanced, Street Rod (California Dreams, 1989) on SDL3, enhanced.";
#ifdef __WXMSW__
    // The Windows task dialog.
    const wxString content = wxString::Format(
        "%s\n\n"
        "Author: Krzysztof Kania\n"
        "Website: <a href=\"%s\">kkania.com</a>\n"
        "Support: <a href=\"%s\">buymeacoffee.com/krzysztofkania</a>",
        blurb, WEBSITE, SUPPORT);
    wxIcon icon;
    icon.CopyFromBitmap(AppBitmap(FromDIP(32)));
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof dialog;
    dialog.hwndParent = static_cast<HWND>(GetHWND());
    dialog.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    dialog.dwCommonButtons = TDCBF_OK_BUTTON;
    dialog.pszWindowTitle = title.wc_str();
    dialog.hMainIcon = static_cast<HICON>(icon.GetHICON());
    dialog.pszMainInstruction = heading.wc_str();
    dialog.pszContent = content.wc_str();
    dialog.pfCallback = AboutCallback;
    TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr);
#else
    wxDialog dialog(this, wxID_ANY, title);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(&dialog, wxID_ANY, AppBitmap(dialog.FromDIP(48))), 0, wxALL, dialog.FromDIP(12));

    auto* text = new wxBoxSizer(wxVERTICAL);
    auto* headingText = new wxStaticText(&dialog, wxID_ANY, heading);
    headingText->SetFont(dialog.GetFont().Bold().Scaled(1.3f));
    text->Add(headingText, 0, wxBOTTOM, dialog.FromDIP(8));
    text->Add(new wxStaticText(&dialog, wxID_ANY, blurb), 0, wxBOTTOM, dialog.FromDIP(12));
    text->Add(new wxStaticText(&dialog, wxID_ANY, "Author: Krzysztof Kania"));
    AddLink(&dialog, text, "Website:", "kkania.com", WEBSITE);
    AddLink(&dialog, text, "Support:", "buymeacoffee.com/krzysztofkania", SUPPORT);
    body->Add(text, 1, wxTOP | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));

    auto* all = new wxBoxSizer(wxVERTICAL);
    all->Add(body, 1, wxEXPAND);
    all->Add(dialog.CreateStdDialogButtonSizer(wxOK), 0, wxEXPAND | wxALL, dialog.FromDIP(8));
    dialog.SetSizerAndFit(all);
    dialog.CentreOnParent();
    dialog.ShowModal();
#endif
}
