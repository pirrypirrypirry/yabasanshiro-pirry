/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "MenuScreen.h"
#include <nanogui/window.h>
#include <nanogui/layout.h>
#include <nanogui/label.h>
#include <nanogui/button.h>
#include <nanogui/popupbutton.h>
#include <nanogui/messagedialog.h>
#include <nanogui/combobox.h>
#include <nanogui/vscrollpanel.h>
#include <nanogui/textbox.h>
#include <nanogui/entypo.h>
#include "../config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
// used by the "Next Disc" button; defined further down, after ends_with()
static std::vector<std::string> disc_list(const std::string & base_path);

#include <experimental/filesystem>
#include <ctime>
#include <iomanip>

#include <nanogui/imageview.h>

namespace fs = std::experimental::filesystem ;

using namespace std;

#include "InputManager.h"

#include "scsp.h"
#include "about.h"

#include "Preference.h"
#include "RetroAchievements.h"

#include "yabause.h"
#include "cheat.h"

/* A popup button inside a popup: nanogui opens its popup to the right of the
   parent popup, which pushes the wide slot lists off the 640 px screen. This
   one opens its popup at the place of the parent popup instead. */
class SubPopupButton : public PopupButton {
public:
  SubPopupButton(Widget *parent, const std::string &caption) : PopupButton(parent, caption) {}
  void performLayout(NVGcontext *ctx) override {
    PopupButton::performLayout(ctx);
    popup()->setAnchorPos(Vector2i(0, popup()->anchorPos().y()));
  }
};

//#define MENU_LOG
#define MENU_LOG(...) do { } while (0)


extern int g_EnagleFPS;
extern int g_resolution_mode;
extern int g_keep_aspect_rate;
extern int g_rotate_resolution_mode;
extern int g_scsp_sync;
extern int g_frame_skip;
extern int g_emulated_bios;


int MenuScreen::onShow(){
  //setupPlayerPsuhButton( 0, player1, "Player1 Input Settings", &p1cb );
  //setupPlayerPsuhButton( 1, player2, "Player2 Input Settings", &p2cb );
  return 0;
}

MenuScreen::MenuScreen( SDL_Window* pwindow, int rwidth, int rheight, const std::string & fname, const std::string & game )
: nanogui::Screen( pwindow, Eigen::Vector2i(rwidth, rheight), "Menu Screen"){
  
  mFocus = nullptr;
  config_file_ = fname;
  current_game_path_ = game;
  swindow = nullptr;
  imageWindow = nullptr;


  std::string title = "Yaba Sanshiro "+ std::string(YAB_VERSION) +" Pirry Edition";
        window = new Window(this, title);
        window->setPosition(Vector2i(0, 0));
        window->setLayout(new GroupLayout(6));

        tools = new Widget(window);
        pushActiveMenu(tools, nullptr );
        tools->setLayout(new BoxLayout(Orientation::Vertical,Alignment::Middle, 0, 2));
        tools->setFixedWidth(256);

        PlayerConfig tmp;
        tmp.player = new PopupButton(tools, "Player1", ENTYPO_ICON_EXPORT);      
        setupPlayerPsuhButton( 0, tmp.player, "Player1 Input Settings", &tmp.cb );
        player_configs_.push_back(tmp);
        tmp.player = new PopupButton(tools, "Player2", ENTYPO_ICON_EXPORT);      
        setupPlayerPsuhButton( 1, tmp.player, "Player2 Input Settings", &tmp.cb );
        player_configs_.push_back(tmp);

       
        PopupButton * ps_config = new PopupButton(tools, "Config");
        ps_config->setFixedWidth(248);
        showConfigDialog(ps_config);
        ps_config->setCallback([this,ps_config]() {      
          pushActiveMenu(ps_config->popup(),ps_config); 
        });

        /* Save and Load State share one submenu, so the main menu fits the screen. */
        PopupButton * ps_save = new PopupButton(tools, "Save");
        ps_save->setFixedWidth(248);
        Popup * save_popup = ps_save->popup();
        save_popup->setLayout(new GroupLayout(4,2,2,2));
        ps_save->setCallback([this,ps_save]() {
          pushActiveMenu(ps_save->popup(), ps_save);
        });

        PopupButton * ps = new SubPopupButton(save_popup, "Save State");
        ps->setFixedWidth(160);
        ps->setCallback([this,ps]() {
          showSaveStateDialog( ps->popup());
          pushActiveMenu(ps->popup(),ps);
        });

        ps = new SubPopupButton(save_popup, "Load State");
        ps->setFixedWidth(160);
        load_state_btn_ = ps;
        ps->setCallback([this,ps]() {
          if( RA_HardcoreActive() ) return;   /* disabled in hardcore mode */
          showLoadStateDialog( ps->popup());
          pushActiveMenu(ps->popup(),ps);
        });

        Button *b0 = new Button(tools, "Exit");
        b0->setFixedWidth(248);
        b0->setCallback([this]() { 
          MENU_LOG("Exit\n"); 
      		SDL_Event* quit = new SDL_Event();
			    quit->type = SDL_QUIT;
			    SDL_PushEvent(quit);  
        });

        Button *b1 = new Button(tools, "Reset");
        b1->setFixedWidth(248);
        b1->setCallback([this]() { 
          MENU_LOG("Reset\n");  
          SDL_Event event = {};
          event.type = reset_;
          event.user.code = 0;
          event.user.data1 = 0;
          event.user.data2 = 0;
          SDL_PushEvent(&event);          
        });        

        /* Separate submenu for the volume sliders. */
        PopupButton * ps_sound = new PopupButton(tools, "Sound Settings");
        ps_sound->setFixedWidth(248);
        showSoundDialog(ps_sound);
        ps_sound->setCallback([this,ps_sound]() {
          pushActiveMenu(ps_sound->popup(), ps_sound);
        });

        PopupButton * ps_cheat = new PopupButton(tools, "Cheats");
        ps_cheat->setFixedWidth(248);
        ps_cheat->setCallback([this,ps_cheat]() {
          showCheatDialog(ps_cheat);
          /* Hand over the inner container: pushActiveMenu looks for the first button only among
             the direct children, and the popup's only child is the scroll panel. */
          pushActiveMenu(cheat_wrapper_, ps_cheat);
        });

        /* RetroAchievements login */
        PopupButton * ps_ra = new PopupButton(tools, "RetroAchievements");
        ps_ra->setFixedWidth(248);
        showRetroAchievementsDialog(ps_ra);
        ps_ra->setCallback([this,ps_ra]() {
          raStatusUpdate(true);
          pushActiveMenu(ps_ra->popup(), ps_ra);
        });

        bCdTray = new Button(tools, "Open CD Tray");
        bCdTray->setFixedWidth(248);
        bCdTray->setCallback([this]() { 
          if( this->is_cdtray_open_ ){
            MENU_LOG("Close CD Tray\n"); 
            bCdTray->setCaption("Open CD Tray");
            this->is_cdtray_open_ = false;

            size_t pos = current_game_path_.find_last_of("/");
            string base_path = current_game_path_.substr(0,pos);
            showFileSelectDialog( tools, bCdTray, base_path);

            /*
            SDL_Event event = {};
            event.type = close_tray_;
            event.user.code = 0;
            //event.user.data1 = malloc( 256* sizeof(char) );
            //strcpy( (char*)event.user.data1, "filename" );
            event.user.data2 = 0;
            SDL_PushEvent(&event);               
            */

          }else{
            MENU_LOG("Open CD Tray\n"); 
            bCdTray->setCaption("Close CD Tray");
            this->is_cdtray_open_ = true;
            SDL_Event event = {};
            event.type = open_tray_;
            event.user.code = 0;
            event.user.data1 = 0;
            event.user.data2 = 0;
            SDL_PushEvent(&event);
          }
        });

        // "Next Disc": switch to the next image in the game directory with one click.
        // Multi-disc games are stored as separate .chd files in one directory.
        // Manual disc change: the Saturn has no "insert disc 2" signal, the game
        // only polls the disc-change flag.
        // With the HLE BIOS, closing the tray resets the system and boots the new disc
        // (YabauseReset in Cs2ForceCloseTray). With a real BIOS (-b) the system currently
        // drops to the BIOS menu; Start there boots the new disc (known issue).
        bNextDisc = new Button(tools, "Next Disc");
        bNextDisc->setFixedWidth(248);
        bNextDisc->setCallback([this]() {
          size_t pos = current_game_path_.find_last_of("/");
          if( pos == std::string::npos ) return;
          std::string base_path = current_game_path_.substr(0,pos);
          std::vector<std::string> discs = disc_list(base_path);
          if( discs.size() < 2 ){
            // Single-disc title: nothing to switch. Do nothing rather than reinsert
            // the same disc, which would restart the game under the HLE BIOS.
            MENU_LOG("Next Disc: only %d image(s) in %s, nothing to switch\n",
                     (int)discs.size(), base_path.c_str());
            return;
          }
          size_t idx = 0; bool found = false;
          for( size_t i = 0; i < discs.size(); i++ )
            if( discs[i] == current_game_path_ ){ idx = i; found = true; break; }
          // Not found (game lies elsewhere than the list): start at 0.
          size_t next_idx = found ? ((idx + 1) % discs.size()) : 0;
          std::string goal = discs[next_idx];
          MENU_LOG("Next Disc: %s -> %s\n", current_game_path_.c_str(), goal.c_str());

          // Tray open ...
          SDL_Event ev_open = {};
          ev_open.type = open_tray_;
          ev_open.user.code = 0; ev_open.user.data1 = 0; ev_open.user.data2 = 0;
          SDL_PushEvent(&ev_open);
          // ... and closed again with the next disc. Both events are handled in the
          // same pass of the main loop, as with the file dialog.
          SDL_Event ev_zu = {};
          ev_zu.type = close_tray_;
          ev_zu.user.code = 0;
          ev_zu.user.data1 = malloc( (goal.size()+1) * sizeof(char) );
          strcpy( (char*)ev_zu.user.data1, goal.c_str() );
          ev_zu.user.data2 = 0;
          SDL_PushEvent(&ev_zu);

          // Track the path here: main.cpp only calls setCurrentGamePath on
          // OpenTray, when cdpath is still the old path.
          current_game_path_ = goal;

          // Keep the menu consistent: the tray is closed again after the close event.
          this->is_cdtray_open_ = false;
          if( this->bCdTray != nullptr ) this->bCdTray->setCaption("Open CD Tray");
        });


        Button *b2 = new Button(tools, "Show/Hide FPS");
        b2->setFixedWidth(248);
        b2->setCallback([this]() { 
          MENU_LOG("Show/Hide FPS\n");  
          SDL_Event event = {};
          event.type = toggile_fps_;
          event.user.code = 0;
          event.user.data1 = 0;
          event.user.data2 = 0;
          SDL_PushEvent(&event);          
        });

        Button *b3 = new Button(tools, "Enable/Disable Frame Skip");
        b3->setFixedWidth(248);
        b3->setCallback([this]() { 
          MENU_LOG("Reset\n");  
          SDL_Event event = {};
          event.type = toggile_frame_skip_;
          event.user.code = 0;
          event.user.data1 = 0;
          event.user.data2 = 0;
          SDL_PushEvent(&event);          
        });        
        Button *b4 = new Button(tools, "About");
        b4->setFixedWidth(248);
        b4->setCallback([this,b4]() {
          imageWindow = new Window(this, "About");
          imageWindow->setLayout(new GroupLayout(10, 4, 6, 0));
          const int w = this->width() - 80;
          auto line = [this, w](const std::string & s, int size) {
            Label * l = new Label(imageWindow, s, "sans", size);
            l->setFixedWidth(w);
            return l;
          };
          /* the two project pages stand out: bold and coloured */
          auto link = [&line](const std::string & s) {
            Label * l = line(s, 18);
            l->setFont("sans-bold");
            l->setColor(Color(120, 200, 255, 255));
          };
          line("Yaba Sanshiro " + std::string(YAB_VERSION) + " Pirry Edition", 22);
          line("Sega Saturn emulator for the R36S handheld, based on Yaba Sanshiro by devMiyax, "
               "which is based on Yabause.", 16);
          link("Source code: github.com/pirrypirrypirry/yabasanshiro-pirry");
          link("Releases: github.com/pirrypirrypirry/yabasanshiro-pirry-release");
          line("Thanks to devMiyax, the Yabause team and RetroAchievements (rcheevos).", 16);
          line("License: GNU General Public License version 2 or later. Included components and "
               "their licenses: THIRD-PARTY-NOTICES.md in the source code.", 16);
          line("Privacy: nothing is sent unless you log in to RetroAchievements. Then the user name, "
               "the password (only for the first login), the game disc hash, unlocks and the game status "
               "go to retroachievements.org; the privacy policy of retroachievements.org applies there. "
               "This program has no server and no telemetry. On the device only the user name and a "
               "login token are kept, in ~/.yabasanshiro/retroachievements.cfg, until you log out.", 16);
          Button *btn = new Button(imageWindow, "Close");
          btn->setCallback([this]() {
            this->popActiveMenu();
            if( this->imageWindow != nullptr ){
              this->imageWindow->dispose();
              this->imageWindow = nullptr;
            }
          });
          imageWindow->center();
          imageWindow->setModal(true);
          imageWindow->requestFocus();
          pushActiveMenu(imageWindow,b4);
        });
        /* 14 entries have to fit into 480 lines: slightly smaller buttons in the main menu. */
        for (Widget * w : tools->children()) {
          if (Button * b = dynamic_cast<Button *>(w)) b->setFontSize(19);
        }
        player_configs_[0].player->focusEvent(true);
        player_configs_[0].player->mouseEnterEvent(player_configs_[0].player->absolutePosition(),true);
        mFocus = player_configs_[0].player;

        performLayout();
        
}


void MenuScreen::showInputCheckDialog( const std::string & key ){
    swindow = new Window(this, key);
    swindow->setPosition(Vector2i(0, 0));
    swindow->setLayout(new GroupLayout(32));
    new Label(swindow,"Push key for " + key, "sans", 64);
    swindow->center();
    swindow->setModal(true);
    swindow->requestFocus();
    current_key_ = key;
}

#include <dirent.h>

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

// Collect the disc images of a directory in sorted order (readdir order is
// filesystem dependent). Uses the same four extensions as showFileSelectDialog.
static std::vector<std::string> disc_list(const std::string & base_path)
{
  std::vector<std::string> liste;
  DIR *dir; struct dirent *ent;
  if ((dir = opendir(base_path.c_str())) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      std::string dname = ent->d_name;
      std::transform(dname.begin(), dname.end(), dname.begin(), ::tolower);
      if (ends_with(dname, ".cue") || ends_with(dname, ".mdf") ||
          ends_with(dname, ".ccd") || ends_with(dname, ".chd"))
        liste.push_back(base_path + "/" + std::string(ent->d_name));
    }
    closedir(dir);
  }
  std::sort(liste.begin(), liste.end());
  return liste;
}



/* Steps of the two sliders.
   CD goes down to 0 (replaces the old "Disable CD music" switch). Both go up
   to 150: the mix is integer and clamped to 16 bit only at the end, so higher
   values can overdrive and distort. Above 100, loud CD music mastered near
   full scale may be clipped at the end of the mix. */
static const int SNDVOL_CDDA[] = { 0, 25, 50, 75, 100, 125, 150 };
static const int SNDVOL_SFX[]  = { 0, 25, 50, 75, 100, 125, 150 };
static const int SNDVOL_CDDA_N = 7;
static const int SNDVOL_SFX_N  = 7;

extern "C" {
  void ScspSetCddaVolume(int v);
  void SmpcSetSystemLanguage(int id);
  void ScspSetSfxVolume(int v);
}

static std::string sndvol_text(const char * name, int cur_value, bool cdda){
  char buf[64];
  if (cdda && cur_value == 0) snprintf(buf, sizeof(buf), "%s:  Off", name);
  else                   snprintf(buf, sizeof(buf), "%s:  %d%%", name, cur_value);
  return std::string(buf);
}

/* Step a slider by one level. direction: -1 left, +1 right. */
void MenuScreen::soundVolStep( nanogui::Button * b, int direction ){
  if (b == nullptr) return;
  /* `preference` is not a class member: showConfigDialog creates its own
     instance (via current_game_path_). Do the same here so the values land in
     the same game config. */
  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));
  const bool cdda = (b == snd_cdda_btn_);
  const int * steps = cdda ? SNDVOL_CDDA : SNDVOL_SFX;
  const int n        = cdda ? SNDVOL_CDDA_N : SNDVOL_SFX_N;
  const char * key   = cdda ? "cd volume" : "sfx volume";
  const char * name  = cdda ? "CD Music" : "Sound Effects";
  int now_val = preference->getInt(key, 100);
  /* Pick the nearest level so values not exactly in the list snap cleanly. */
  int idx = 0;
  int best = -1;
  for (int i = 0; i < n; i++) {
    const int d = (steps[i] > now_val) ? (steps[i] - now_val) : (now_val - steps[i]);
    if (best < 0 || d < best) { best = d; idx = i; }
  }
  idx += direction;
  if (idx < 0) idx = 0;
  if (idx >= n) idx = n - 1;
  const int new_val = steps[idx];
  if (new_val == now_val) return;
  preference->setInt(key, new_val);
  if (cdda) ScspSetCddaVolume(new_val); else ScspSetSfxVolume(new_val);
  b->setCaption(sndvol_text(name, new_val, cdda));
}

extern "C" int use_new_scsp;   /* scsp.c: the core the current game is running with */
extern "C" int g_scsp_dsp_off;  /* scsp.c: 1 = DSP off, switchable live */
extern "C" int g_interlace_half_menu;  /* vidogl.c: 1 = half-height interlaced backgrounds, live */
/* The selected entry is tinted green (tintToggle). DSP Off / DSP On
   (selected: 1 = DSP on) takes effect immediately, no restart needed. */
void MenuScreen::soundCoreShow( int selected ){
  tintToggle(snd_core_btn_, selected == 0);
  tintToggle(snd_core_new_btn_, selected == 1);
  if (snd_core_hint_ != nullptr)
    snd_core_hint_->setCaption(" ");
}

void MenuScreen::soundCoreStep( int direction ){
  if (snd_core_btn_ == nullptr) return;
  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));
  int now_val = preference->getIntOnlyGlobal("sound dsp", 1) ? 1 : 0;   /* global setting, not per game */
  int new_val = (direction > 0) ? 1 : 0;
  if (direction == 0) new_val = !now_val;
  if (new_val != now_val) preference->setIntGlobal("sound dsp", new_val);
  g_scsp_dsp_off = new_val ? 0 : 1;
  soundCoreShow(new_val);
}

/* Cheats of the running game (libretro .cht, loaded in main.cpp). One entry = one group of
   Action Replay codes (cheatN of the file, stored back to back); pressing it switches the group
   at once, green = on. The choice is kept in the game config as "cheat <N>" and applied again
   on the next start. Long lists scroll like the file selector. */
void MenuScreen::showCheatDialog( PopupButton *parent ){
  if (cheat_built_) return;
  cheat_built_ = true;
  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));
  Popup *popup = parent->popup();
  int popup_height = std::min(this->size()[1] - 40, 440);
  popup->setFixedSize(Vector2i(300, popup_height));
  auto vscroll = new VScrollPanel(popup);
  vscroll->setFixedSize(Vector2i(300, popup_height));
  Widget *wrapper = new Widget(vscroll);
  wrapper->setFixedSize(Vector2i(300, popup_height));
  wrapper->setLayout(new GroupLayout(4,2,2,2));
  cheat_wrapper_ = wrapper;

  /* RetroAchievements note at the top (a label; keyboard focus goes to the first button) */
  static const char * const RA_OFF  = "Cheats activated -> RetroAchievements disabled";
  static const char * const RA_NOTE = "A cheat disables RetroAchievements until reset";
  RAStatus ra_st;
  RA_GetStatus(ra_st);
  Label *ra_hint = (ra_st.state == RA_LOGGED_IN) ? new Label(wrapper, ra_st.cheats ? RA_OFF : RA_NOTE) : nullptr;

  int count = 0, last = -2, entries = 0;
  cheatlist_struct *list = CheatGetList(&count);
  for (int i = 0; list != NULL && i < count; i++) {
    int g = list[i].group;
    if (g < 0 || g == last) continue;
    last = g;
    entries++;
    Button *b = new Button(wrapper, list[i].desc ? list[i].desc : "?");
    b->setFlags(Button::ToggleButton);
    b->setPushed(list[i].enable != 0);
    MenuScreen::tintToggle(b, b->pushed());
    b->setChangeCallback([this,preference,b,g,ra_hint](bool state) {
      MenuScreen::tintToggle(b, state);
      CheatEnableGroup(g, state ? 1 : 0);
      preference->setBool("cheat " + std::to_string(g), state);
      if (state) RA_CheatsUsed();   /* no achievements with cheats */
      if (ra_hint != nullptr) {
        RAStatus s;
        RA_GetStatus(s);
        ra_hint->setCaption(s.cheats ? RA_OFF : RA_NOTE);
        performLayout();
      }
    });
  }
  if (entries == 0)
    new Button(wrapper, "No cheats for this game");   /* a button, not a label: the page needs something focusable */
  performLayout();
}

/* "Sound Volume": the two sliders, adjusted with left/right on the d-pad
   (see keyboardEvent below). */
void MenuScreen::showSoundDialog( PopupButton *parent ){
  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));
  Popup *popup = parent->popup();
  popup->setLayout(new GroupLayout(4,2,2,2));
  new Label(popup, "Left/Right to adjust");

  int v = preference->getInt("cd volume", 100);
  snd_cdda_btn_ = new Button(popup, sndvol_text("CD Music", v, true));
  ScspSetCddaVolume(v);
  snd_cdda_btn_->setCallback([this](){ soundVolStep(snd_cdda_btn_, +1); });

  v = preference->getInt("sfx volume", 100);
  snd_sfx_btn_ = new Button(popup, sndvol_text("Sound Effects", v, false));
  ScspSetSfxVolume(v);
  snd_sfx_btn_->setCallback([this](){ soundVolStep(snd_sfx_btn_, +1); });

  /* two entries plus a hint line below */
  snd_core_btn_ = new Button(popup, "DSP Off (less heat, no reverb)");
  snd_core_btn_->setCallback([this](){ soundCoreStep(-1); });
  snd_core_new_btn_ = new Button(popup, "DSP On (more heat, reverb)");
  snd_core_new_btn_->setCallback([this](){ soundCoreStep(+1); });
  snd_core_hint_ = new Label(popup, " ");
  soundCoreShow(preference->getIntOnlyGlobal("sound dsp", 1) ? 1 : 0);
}

/* "RetroAchievements": status line, Login / Logout. */
void MenuScreen::showRetroAchievementsDialog( PopupButton *parent ){
  ra_popup_btn_ = parent;
  Popup *popup = parent->popup();
  ra_popup_ = popup;
  popup->setLayout(new GroupLayout(4,2,2,2));

  ra_status_lbl_ = new Label(popup, " ");
  ra_status_lbl_->setFixedWidth(340);   /* the menu window ends at ~270 px, the popup starts 15 px right of it */
  ra_login_btn_ = new Button(popup, "Login");
  ra_login_btn_->setCallback([this](){ raKbOpen(); });
  ra_logout_btn_ = new Button(popup, "Logout");
  ra_logout_btn_->setCallback([this](){
    RA_Logout();
    raStatusUpdate(true);
  });
  ra_ach_btn_ = new Button(popup, "Achievements");
  ra_ach_btn_->setCallback([this](){ raAchOpen(ra_ach_btn_); });
  /* Mode with a red Reset button beside it while hardcore waits for the reset */
  Widget * mode_row = new Widget(popup);
  mode_row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 6));
  ra_mode_btn_ = new Button(mode_row, "Mode: Softcore");
  ra_mode_btn_->setFixedWidth(340);   /* 240 while the red Reset button is shown beside it */
  ra_hcreset_btn_ = new Button(mode_row, "Reset");
  ra_hcreset_btn_->setFixedWidth(94);
  ra_hcreset_btn_->setTextColor(Color(255, 255, 255, 255));
  ra_hcreset_btn_->setBackgroundColor(Color(200, 0, 0, 255));
  ra_hcreset_btn_->setVisible(false);
  ra_hcreset_btn_->setCallback([this]() {
    this->popActiveMenu();   /* close the popup first, the reset event hides the menu */
    SDL_Event event = {};
    event.type = reset_;
    SDL_PushEvent(&event);   /* same as the Reset entry of the main menu: the game restarts in hardcore */
  });
  ra_hc_lbl_ = new Label(popup, "Hardcore starts with a reset of the game: press the red Reset button.", "sans", 18);
  ra_hc_lbl_->setFixedWidth(340);
  ra_hc_lbl_->setColor(Color(255, 80, 80, 255));
  ra_hc_lbl_->setVisible(false);
  ra_mode_btn_->setCallback([this](){
    RAStatus st;
    RA_GetStatus(st);
    RA_SetHardcore(!st.hardcore);
    raStatusUpdate(true);
  });
  ra_ind_btn_ = new Button(popup, "Indicators: On");
  ra_ind_btn_->setCallback([this](){
    RAStatus st;
    RA_GetStatus(st);
    RA_SetIndicators(!st.indicators);
    raStatusUpdate(true);
  });
  Label * hint = new Label(popup, "Hardcore: no Load State. Switching to Hardcore needs a reset of the game (red Reset button), switching back works at once. Or create ~/.yabasanshiro/retroachievements.cfg with username= and password=", "sans", 18);
  hint->setFixedWidth(340);
  ra_status_lbl_->setCaption("Not logged in");   /* refreshed when the dialog opens */
}

/* Achievement list of the running game, 6 entries per page. The menu moves the focus between buttons only,
   so the pages are switched with buttons instead of a scroll panel. */
void MenuScreen::raAchOpen( Widget * from ){
  if( imageWindow != nullptr ) return;
  RA_GetAchievements(ra_ach_);
  ra_ach_page_ = 0;
  imageWindow = new Window(this, "Achievements");
  imageWindow->setLayout(new GroupLayout(10, 4, 6, 0));
  const int w = this->width() - 80;
  ra_ach_head_ = new Label(imageWindow, " ", "sans", 18);
  ra_ach_head_->setFixedWidth(w);
  for( int i = 0; i < 6; i++ ){
    /* one row: status icon (green check / grey lock) + text */
    Widget * row = new Widget(imageWindow);
    row->setLayout(new BoxLayout(Orientation::Horizontal, Alignment::Middle, 0, 6));
    ra_ach_icons_[i] = new Label(row, " ", "icons", 20);
    ra_ach_icons_[i]->setFixedWidth(22);
    ra_ach_rows_[i] = new Label(row, " ", "sans", 15);
    ra_ach_rows_[i]->setFixedWidth(w - 28);
  }
  Button * next = new Button(imageWindow, "Next page");
  next->setCallback([this](){ raAchShowPage(ra_ach_page_ + 1); });
  Button * prev = new Button(imageWindow, "Previous page");
  prev->setCallback([this](){ raAchShowPage(ra_ach_page_ - 1); });
  Button * close = new Button(imageWindow, "Close");
  ra_ach_nav_[0] = next; ra_ach_nav_[1] = prev; ra_ach_nav_[2] = close;
  close->setCallback([this]() {
    this->popActiveMenu();
    if( this->imageWindow != nullptr ){
      this->imageWindow->dispose();
      this->imageWindow = nullptr;
    }
    ra_ach_head_ = nullptr;
    for( int i = 0; i < 6; i++ ){ ra_ach_rows_[i] = nullptr; ra_ach_icons_[i] = nullptr; }
    for( int i = 0; i < 3; i++ ) ra_ach_nav_[i] = nullptr;
  });
  raAchShowPage(0);
  imageWindow->center();
  imageWindow->setModal(true);
  imageWindow->requestFocus();
  pushActiveMenu(imageWindow, from);
}

void MenuScreen::raAchShowPage( int page ){
  if( ra_ach_head_ == nullptr ) return;
  const int n = (int)ra_ach_.size();
  const int pages = n > 0 ? (n + 5) / 6 : 1;
  if( page < 0 ) page = pages - 1;
  if( page >= pages ) page = 0;
  ra_ach_page_ = page;
  RAStatus st;
  RA_GetStatus(st);
  std::stringstream hs;
  if( n == 0 ) hs << "No achievements: log in and start a game that RetroAchievements knows.";
  else hs << st.game_title << ": " << st.ach_unlocked << "/" << st.ach_total << "   page " << (page + 1) << "/" << pages;
  ra_ach_head_->setCaption(hs.str());
  for( int i = 0; i < 6; i++ ){
    const int k = page * 6 + i;
    if( k < n ){
      const RAAchievement & a = ra_ach_[k];
      std::stringstream ss;
      ss << a.title << " (" << a.points << ")";
      if( !a.unlocked && !a.progress.empty() ) ss << " [" << a.progress << "]";   /* measured progress */
      ss << " - " << a.description;
      ra_ach_rows_[i]->setCaption(ss.str());
      ra_ach_rows_[i]->setColor(a.unlocked ? Color(255, 255) : Color(160, 255));
      ra_ach_icons_[i]->setCaption(utf8(a.unlocked ? ENTYPO_ICON_CHECK : ENTYPO_ICON_LOCK).data());
      ra_ach_icons_[i]->setColor(a.unlocked ? Color(60, 200, 80, 255) : Color(120, 255));
    } else {
      ra_ach_rows_[i]->setCaption(" ");
      ra_ach_icons_[i]->setCaption(" ");
    }
  }
  /* the text height changes from page to page: measure the window again */
  if( imageWindow != nullptr ){
    imageWindow->setSize(Vector2i(0, 0));
    imageWindow->center();
  }
}

void MenuScreen::raStatusUpdate( bool force ){
  if( ra_status_lbl_ == nullptr ) return;
  RAStatus st;
  RA_GetStatus(st);
  std::string t;
  switch( st.state ){
  case RA_LOGGING_IN: t = "Logging in..."; break;
  case RA_LOGGED_IN: {
    std::stringstream ss;
    ss << "Logged in as " << st.name << " (" << st.score << " points)";
    t = ss.str();
    break;
  }
  case RA_ERROR: t = "Error: " + st.error; break;
  case RA_NOT_AVAILABLE: t = "Error: " + st.error; break;
  default: t = "Not logged in"; break;
  }
  if( st.state == RA_LOGGED_IN && st.game_loaded ){
    std::stringstream ss;
    ss << "\n" << st.game_title << ": " << st.ach_unlocked << "/" << st.ach_total;
    if( st.cheats ) ss << "\nCheats activated -> RetroAchievements disabled\n(reset without cheats to enable them)";
    t += ss.str();
  }
  if( ra_mode_btn_ != nullptr ){
    ra_mode_btn_->setCaption( st.hardcore ? "Mode: Hardcore" : "Mode: Softcore" );
    tintToggle( ra_mode_btn_, st.hardcore );
  }
  if( ra_ind_btn_ != nullptr ){
    ra_ind_btn_->setCaption( st.indicators ? "Indicators: On" : "Indicators: Off" );
    tintToggle( ra_ind_btn_, st.indicators );
  }
  /* Load State follows the mode at once (hardcore -> casual is allowed mid-session) */
  if( load_state_btn_ != nullptr ) load_state_btn_->setEnabled( !RA_HardcoreActive() );
  /* red Reset + note while hardcore waits for the reset */
  if( ra_hcreset_btn_ != nullptr && ra_hc_lbl_ != nullptr ){
    const bool pend = RA_HardcorePending();
    if( ra_hcreset_btn_->visible() != pend ){
      ra_hcreset_btn_->setVisible(pend);
      ra_hc_lbl_->setVisible(pend);
      ra_mode_btn_->setFixedWidth(pend ? 240 : 340);
      if( !pend && mFocus == ra_hcreset_btn_ ){ mFocus = ra_mode_btn_; ra_mode_btn_->mouseEnterEvent(ra_mode_btn_->position(), true); }
      force = true;   /* layout below */
    }
  }
  /* the popup sits at the top of the screen (more room); the arrow still points at its button */
  if( ra_popup_ != nullptr ) ra_popup_->setAnchorHeight( std::max(30, ra_popup_->anchorPos().y() - 8) );
  if( !force && t == ra_status_text_ ) return;
  ra_status_text_ = t;
  ra_status_lbl_->setCaption(t);
  performLayout();   /* the label may wrap to a different height */
}

/* ---- on-screen keyboard (large buttons, d-pad focus) ---- */

void MenuScreen::raKbOpen(){
  if( kb_window_ != nullptr ) return;
  const char * rows[5] = { "1234567890", "qwertyuiop", "asdfghjkl.", "zxcvbnm_-@", "!?#$%&*+=" };
  const int kw = 54, kh = 44, gap = 4, margin = 12, head = 36;
  const int ww = margin * 2 + 10 * kw + 9 * gap;
  const int wh = head + 8 + kh + 8 + 6 * (kh + gap) + margin;

  kb_step_ = 0;
  kb_shift_ = false;
  kb_show_pw_ = false;
  kb_text_.clear();
  kb_user_.clear();
  kb_rows_.clear();

  kb_window_ = new Window(this, "RetroAchievements: Username");
  kb_window_->setFixedSize(Vector2i(ww, wh));
  kb_window_->setPosition(Vector2i((this->size()[0] - ww) / 2, std::max(0, (this->size()[1] - wh) / 2)));

  const int fw = 10 * kw + 9 * gap - kw - gap;   /* field + eye = width of the key rows */
  kb_display_ = new TextBox(kb_window_, "");
  kb_display_->setEditable(false);
  kb_display_->setAlignment(TextBox::Alignment::Left);
  kb_display_->setFontSize(28);
  kb_display_->setPosition(Vector2i(margin, head + 8));
  kb_display_->setFixedSize(Vector2i(fw, kh));
  kb_maxchars_ = std::max(8, (fw - 24) / 15);
  kb_eye_ = new Button(kb_window_, "", ENTYPO_ICON_EYE);
  kb_eye_->setFontSize(26);
  kb_eye_->setPosition(Vector2i(margin + fw + gap, head + 8));
  kb_eye_->setFixedSize(Vector2i(kw, kh));
  {
    std::vector<KbKey> row;
    KbKey k; k.btn = kb_eye_; k.ch = 0; k.special = 5;
    row.push_back(k);
    kb_rows_.push_back(row);   /* row 0: eye, only reachable while typing the password */
  }

  int y = head + 8 + kh + 8;
  for( int r = 0; r < 5; r++ ){
    std::vector<KbKey> row;
    int n = (int)strlen(rows[r]);
    for( int i = 0; i < n; i++ ){
      std::string cap(1, rows[r][i]);
      Button * b = new Button(kb_window_, cap);
      b->setFontSize(26);
      b->setPosition(Vector2i(margin + i * (kw + gap), y));
      b->setFixedSize(Vector2i(kw, kh));
      KbKey k; k.btn = b; k.ch = rows[r][i]; k.special = 0;
      row.push_back(k);
    }
    kb_rows_.push_back(row);
    y += kh + gap;
  }
  /* control row: Shift, Delete, OK, Cancel */
  const char * caps[4] = { "Shift", "Delete", "OK", "Cancel" };
  const int cw = (10 * kw + 9 * gap - 3 * gap) / 4;
  std::vector<KbKey> row;
  for( int i = 0; i < 4; i++ ){
    Button * b = new Button(kb_window_, caps[i]);
    b->setFontSize(24);
    b->setPosition(Vector2i(margin + i * (cw + gap), y));
    b->setFixedSize(Vector2i(cw, kh));
    KbKey k; k.btn = b; k.ch = 0; k.special = i + 1;
    row.push_back(k);
  }
  kb_rows_.push_back(row);

  kb_window_->setModal(true);
  kb_window_->requestFocus();
  performLayout();

  /* focus moves from the Login button to the first key */
  if( mFocus != nullptr ) mFocus->mouseEnterEvent(mFocus->position(), false);
  kb_r_ = 1; kb_c_ = 0;
  mFocus = kb_rows_[1][0].btn;
  mFocus->mouseEnterEvent(mFocus->position(), true);
  raKbRefresh();
}

void MenuScreen::raKbClose(){
  if( kb_window_ == nullptr ) return;
  if( mFocus != nullptr ) mFocus->mouseEnterEvent(mFocus->position(), false);
  kb_rows_.clear();
  kb_display_ = nullptr;
  kb_eye_ = nullptr;
  kb_show_pw_ = false;
  Window * w = kb_window_;
  kb_window_ = nullptr;
  /* wipe the typed secrets */
  for( size_t i = 0; i < kb_text_.size(); i++ ) kb_text_[i] = ' ';
  kb_text_.clear();
  kb_user_.clear();
  mFocus = ra_login_btn_;
  if( mFocus != nullptr ){
    mFocus->mouseEnterEvent(mFocus->position(), true);
    mFocus->requestFocus();
  }
  w->dispose();
}

/* text field: password masked, long text shows its tail */
void MenuScreen::raKbRefresh(){
  if( kb_display_ == nullptr ) return;
  std::string s = (kb_step_ == 1 && !kb_show_pw_) ? std::string(kb_text_.size(), '*') : kb_text_;
  if( (int)s.size() > kb_maxchars_ ) s = "..." + s.substr(s.size() - (kb_maxchars_ - 3));
  kb_display_->setValue(s + "_");
  if( kb_eye_ != nullptr ){
    kb_eye_->setVisible(kb_step_ == 1);
    tintToggle(kb_eye_, kb_show_pw_);
  }
  if( kb_window_ != nullptr )
    kb_window_->setTitle(kb_step_ == 0 ? "RetroAchievements: Username" : "RetroAchievements: Password");
  /* letter case follows Shift */
  for( size_t r = 0; r < kb_rows_.size(); r++ )
    for( size_t i = 0; i < kb_rows_[r].size(); i++ ){
      KbKey & k = kb_rows_[r][i];
      if( k.special == 0 && k.ch >= 'a' && k.ch <= 'z' )
        k.btn->setCaption(std::string(1, kb_shift_ ? (char)(k.ch - 'a' + 'A') : k.ch));
    }
}

void MenuScreen::raKbMove( int dr, int dc ){
  if( kb_rows_.empty() ) return;
  int nr = kb_r_, nc = kb_c_;
  if( dr != 0 ){
    /* nearest button by horizontal centre in the target row */
    nr = (kb_r_ + dr + (int)kb_rows_.size()) % (int)kb_rows_.size();
    if( nr == 0 && kb_step_ == 0 ) nr = (nr + dr + (int)kb_rows_.size()) % (int)kb_rows_.size();
    Button * cur = kb_rows_[kb_r_][kb_c_].btn;
    int cx = cur->position().x() + cur->size().x() / 2;
    int best = 1 << 30;
    for( size_t i = 0; i < kb_rows_[nr].size(); i++ ){
      Button * b = kb_rows_[nr][i].btn;
      int d = std::abs(b->position().x() + b->size().x() / 2 - cx);
      if( d < best ){ best = d; nc = (int)i; }
    }
  }else{
    int n = (int)kb_rows_[kb_r_].size();
    nc = (kb_c_ + dc + n) % n;
  }
  mFocus->mouseEnterEvent(mFocus->position(), false);
  kb_r_ = nr; kb_c_ = nc;
  mFocus = kb_rows_[kb_r_][kb_c_].btn;
  mFocus->mouseEnterEvent(mFocus->position(), true);
}

void MenuScreen::raKbPress(){
  KbKey k = kb_rows_[kb_r_][kb_c_];
  switch( k.special ){
  case 0:
    if( kb_text_.size() < 64 )
      kb_text_ += (kb_shift_ && k.ch >= 'a' && k.ch <= 'z') ? (char)(k.ch - 'a' + 'A') : k.ch;
    break;
  case 1: kb_shift_ = !kb_shift_; break;
  case 2: if( !kb_text_.empty() ) kb_text_.erase(kb_text_.size() - 1); break;
  case 3:   /* OK */
    if( kb_text_.empty() ) break;
    if( kb_step_ == 0 ){
      kb_user_ = kb_text_;
      kb_text_.clear();
      kb_step_ = 1;
      kb_shift_ = false;
      kb_show_pw_ = false;   /* password starts hidden */
    }else{
      std::string u = kb_user_, p = kb_text_;
      raKbClose();
      RA_LoginWithPassword(u, p);
      for( size_t i = 0; i < p.size(); i++ ) p[i] = ' ';
      raStatusUpdate(true);
      return;
    }
    break;
  case 4: raKbClose(); return;   /* Cancel */
  case 5: kb_show_pw_ = !kb_show_pw_; break;   /* eye: show / hide the password */
  }
  raKbRefresh();
}

/* Input while the keyboard is open: d-pad moves, A presses (B = Cancel is
   handled in onBackButtonPressed). */
bool MenuScreen::raKbKey( const std::string & keycode, int action ){
  if( action == 0 ) return false;
  if( keycode.find("down") != string::npos ) raKbMove(1, 0);
  else if( keycode.find("up") != string::npos ) raKbMove(-1, 0);
  else if( keycode.find("left") != string::npos ) raKbMove(0, -1);
  else if( keycode.find("right") != string::npos ) raKbMove(0, 1);
  else if( keycode == "a" ) raKbPress();
  return false;
}

void MenuScreen::showConfigDialog( PopupButton *parent ){

  // Todo setCurrentGamePath
  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));

  Popup *popup = parent->popup();    
  popup->setLayout(new GroupLayout(4,2,2,2)); 
  new Label(popup, "Resolution");
  ComboBox * cb = new ComboBox(popup);  
  vector<string> items;
  items.push_back("Native");
  items.push_back("4x");
  items.push_back("2x");
  items.push_back("Original");
  items.push_back("720p");
  items.push_back("1080p");

  cb->setItems(items);
  Popup *cbpopup = cb->popup(); 
  cb->setCallback([this,cbpopup,cb]() {       
    pushActiveMenu(cbpopup, cb );
  });

  cb->setSelectedIndex( preference->getInt("Resolution",g_resolution_mode) );
  cb->setCallbackSelect([this,preference]( int idx ) {
    popActiveMenu();
    preference->setInt("Resolution",idx);
    VideoSetSetting(VDP_SETTING_RESOLUTION_MODE, idx);
  });
  cfg_resolution_cb_ = cb;

  new Label(popup, "Aspect rate");
  cb = new ComboBox(popup);  
  items.clear();
  items.push_back("Original");
  items.push_back("4:3");
  items.push_back("16:9");
  items.push_back("Full Screen");
  cb->setItems(items);
  cbpopup = cb->popup(); 
  cb->setCallback([this,cbpopup,cb]() {       
    pushActiveMenu(cbpopup, cb );
  });

  cb->setSelectedIndex( preference->getInt("Aspect rate",g_keep_aspect_rate) );
  cb->setCallbackSelect([this,preference]( int idx ) {
    popActiveMenu();
    preference->setInt("Aspect rate",idx);
  });
  cfg_aspect_cb_ = cb;

  /* System language (game config; without an entry default.config applies, else English). Takes effect on the next game start. */
  new Label(popup, "System Language (restart game)");
  cb = new ComboBox(popup);
  items.clear();
  items.push_back("English");
  items.push_back("Deutsch");
  items.push_back("Francais");
  items.push_back("Espanol");
  items.push_back("Italiano");
  items.push_back("Japanese");
  cb->setItems(items);
  cbpopup = cb->popup();
  cb->setCallback([this,cbpopup,cb]() {
    pushActiveMenu(cbpopup, cb );
  });
  { int sp = preference->getIntGlobal("system language", 0); if (sp < 0 || sp > 5) sp = 0; cb->setSelectedIndex(sp); }
  cb->setCallbackSelect([this,preference]( int idx ) {
    popActiveMenu();
    preference->setInt("system language", idx);
    SmpcSetSystemLanguage(idx);
  });

  new Label(popup, "Rotate screen resolution");
  cb = new ComboBox(popup);  
  items.clear();
  items.push_back("Original");
  items.push_back("2x");
  items.push_back("720p");
  items.push_back("1080p");
  items.push_back("Native");
  cb->setItems(items);
  cbpopup = cb->popup(); 
  cb->setCallback([this,cbpopup,cb]() {       
    pushActiveMenu(cbpopup, cb );
  });

  cb->setSelectedIndex( preference->getInt("Rotate screen resolution",g_rotate_resolution_mode) );
  cb->setCallbackSelect([this,preference]( int idx ) {
    popActiveMenu();
    preference->setInt("Rotate screen resolution",idx);
    VideoSetSetting(VDP_SETTING_RBG_RESOLUTION_MODE, idx);
  });
  cfg_rotate_res_cb_ = cb;


  Button * ba = new Button(popup,"Use compute shader");
  ba->setFlags(Button::ToggleButton);
  /* The default must be the same (true) in every reader, including main.cpp. */
  ba->setPushed( preference->getBool("Use compute shader", true) );
  MenuScreen::tintToggle(ba, ba->pushed());
  ba->setChangeCallback([this,preference,ba](bool state) {
    MenuScreen::tintToggle(ba, state);
    preference->setBool("Use compute shader",state);
    VideoSetSetting(VDP_SETTING_RBG_USE_COMPUTESHADER, state);
  });
  cfg_compute_shader_btn_ = ba;

  /* Rasterise backgrounds of interlaced modes (>= 448 lines) at half height.
     Per game (`interlace half`), default off, takes effect immediately. */
  new Label(popup, "Interlace (faster)");
  Button * bih = new Button(popup, "Half-height backgrounds");
  bih->setFlags(Button::ToggleButton);
  bih->setPushed( preference->getInt("interlace half", 0) != 0 );
  MenuScreen::tintToggle(bih, bih->pushed());
  bih->setChangeCallback([this,preference,bih](bool state) {
    MenuScreen::tintToggle(bih, state);
    preference->setInt("interlace half", state ? 1 : 0);
    g_interlace_half_menu = state ? 1 : 0;
  });
  cfg_interlace_half_btn_ = bih;




  ba = new Button(popup,"Rotate screen");
  ba->setFlags(Button::ToggleButton);
  ba->setPushed( preference->getBool("Rotate screen",false) );
  MenuScreen::tintToggle(ba, ba->pushed());
  ba->setChangeCallback([this,preference,ba](bool state) {
    MenuScreen::tintToggle(ba, state);
    preference->setBool("Rotate screen",state);
    VideoSetSetting(VDP_SETTING_ROTATE_SCREEN, state);
  });
  cfg_rotate_screen_btn_ = ba;

}

void MenuScreen::tintToggle( Button * b, bool on ){
  // Green text and background for "on"; the theme gradient covers most of the
  // background colour, so the text colour carries the state. Off = theme default.
  if( b == nullptr ) return;
  if( on ){
    b->setTextColor( Color(140, 255, 140, 255) );
    b->setBackgroundColor( Color(0, 150, 0, 255) );
  }else{
    b->setTextColor( Color(0, 0) );
    b->setBackgroundColor( Color(0, 0) );
  }
}

void MenuScreen::refreshConfigSwitches(){
  // The widgets are built once in the constructor; on every menu open the
  // game config is re-read and only mirrored into them. Settings are still
  // written by the widget callbacks alone.
  if( load_state_btn_ != nullptr ) load_state_btn_->setEnabled( !RA_HardcoreActive() );
  if( cfg_compute_shader_btn_ == nullptr ) return;

  std::shared_ptr<Preference> preference(new Preference( current_game_path_ ));

  if( cfg_resolution_cb_ != nullptr ){
    cfg_resolution_cb_->setSelectedIndex( preference->getInt("Resolution", g_resolution_mode) );
  }
  if( cfg_aspect_cb_ != nullptr ){
    cfg_aspect_cb_->setSelectedIndex( preference->getInt("Aspect rate", g_keep_aspect_rate) );
  }
  if( cfg_rotate_res_cb_ != nullptr ){
    cfg_rotate_res_cb_->setSelectedIndex( preference->getInt("Rotate screen resolution", g_rotate_resolution_mode) );
  }
  cfg_compute_shader_btn_->setPushed( preference->getBool("Use compute shader", true) );
  MenuScreen::tintToggle( cfg_compute_shader_btn_, cfg_compute_shader_btn_->pushed() );
  if( cfg_interlace_half_btn_ != nullptr ){
    cfg_interlace_half_btn_->setPushed( preference->getInt("interlace half", 0) != 0 );
    MenuScreen::tintToggle( cfg_interlace_half_btn_, cfg_interlace_half_btn_->pushed() );
  }
  if( cfg_rotate_screen_btn_ != nullptr ){
    cfg_rotate_screen_btn_->setPushed( preference->getBool("Rotate screen", false) );
    MenuScreen::tintToggle( cfg_rotate_screen_btn_, cfg_rotate_screen_btn_->pushed() );
  }
}

void MenuScreen::showSaveStateDialog( Popup *popup ){

  while (popup->childCount() != 0)
    popup->removeChild(popup->childCount()-1);

  popup->setLayout(new GroupLayout(4,2,2,2)); 

  for( int i = 0; i< 5; i++ ){

    string title;

    std::stringstream stream;

    stream << "Save Slot ";
    stream << i;

    std::stringstream img_stream;
    img_stream << this->cuurent_game_id_;
    img_stream << i;
    img_stream << ".yss";

    if( fs::exists( img_stream.str() ) ){
      auto ftime = fs::last_write_time(img_stream.str());
      namespace chrono = std::chrono;
      auto sec = chrono::duration_cast<chrono::seconds>(ftime.time_since_epoch());
      std::time_t t = sec.count();
      const tm* lt = std::localtime(&t);
      std::ostringstream ss;
      stream << "  " << std::put_time(lt, "%c");
    }

    Button *tmp = new Button(popup, stream.str() );
    tmp->setCallback([this,i,popup]() { 
      SDL_Event event = {};
      event.type = save_state_;
      event.user.code = i;
      event.user.data1 = 0;
      event.user.data2 = 0;
      SDL_PushEvent(&event);
      popActiveMenu();
    });  


    tmp->setOnSetFocusCallback([this,i,popup]() {
      std::stringstream img_stream;
      img_stream << this->cuurent_game_id_;
      img_stream << i;
      img_stream << ".png";
      this->setTmpBackGroundImage(img_stream.str());
    });

    tmp->setOnLeaveCallback([this,i,popup]() {
      this->setDefalutBackGroundImage();
    });

  }
  this->performLayout();
}

void MenuScreen::showLoadStateDialog( Popup *popup ){
  while (popup->childCount() != 0)
    popup->removeChild(popup->childCount()-1);

  popup->setLayout(new GroupLayout(4,2,2,2)); 

  for( int i = 0; i< 5; i++ ){

    string title;

    std::stringstream stream;

    stream << "Load Slot ";
    stream << i;

    std::stringstream img_stream;
    img_stream << this->cuurent_game_id_;
    img_stream << i;
    img_stream << ".yss";

    if( fs::exists( img_stream.str() ) ){
      auto ftime = fs::last_write_time(img_stream.str());
      namespace chrono = std::chrono;
      auto sec = chrono::duration_cast<chrono::seconds>(ftime.time_since_epoch());
      std::time_t t = sec.count();
      const tm* lt = std::localtime(&t);
      std::ostringstream ss;
      stream << "  " << std::put_time(lt, "%c");

      Button *tmp = new Button(popup, stream.str() );
      tmp->setCallback([this,i,popup]() { 
        SDL_Event event = {};
        event.type = load_state_;
        event.user.code = i;
        event.user.data1 = 0;
        event.user.data2 = 0;
        SDL_PushEvent(&event);
        popActiveMenu();
      });  

      tmp->setOnSetFocusCallback([this,i,popup]() {
        std::stringstream img_stream;
        img_stream << this->cuurent_game_id_;
        img_stream << i;
        img_stream << ".png";
          this->setTmpBackGroundImage(img_stream.str());
      });
    
      tmp->setOnLeaveCallback([this,i,popup]() {
        this->setDefalutBackGroundImage();
      });
    }
  }
  /* No slot saved yet: show a note instead of an empty popup, so the focus has
     a button to land on and A or B leads back to the Save menu. */
  if( popup->childCount() == 0 ){
    Button *none = new Button(popup, "No save state found");
    none->setCallback([this]() { popActiveMenu(); });
  }
  this->performLayout();

}


void MenuScreen::showFileSelectDialog( Widget * parent, Widget * toback, const std::string & base_path ){
  const int dialog_width = 512;
  const int dialog_height = this->size()[1] - 20 ;
    swindow = new Window(this, "Select File");
    swindow->setPosition(Vector2i(  this->size()[0]/2 - (dialog_width/2) ,   this->size()[1]/2 - (dialog_height/2) ));
    swindow->setFixedSize({dialog_width, dialog_height});
    //swindow->setLayout(new GroupLayout());

    auto vscroll = new VScrollPanel(swindow);
    vscroll->setFixedSize({dialog_width, dialog_height });
    auto wrapper = new Widget(vscroll);
    wrapper->setFixedSize({dialog_width, dialog_height });
    wrapper->setLayout(new GroupLayout());

    //pushActiveMenu(parent,toback);

    DIR *dir;
    struct dirent *ent;
    bool first_object = true;
    if ((dir = opendir (base_path.c_str())) != NULL) {
      /* print all the files and directories within directory */
      while ((ent = readdir (dir)) != NULL) {
        string dname = ent->d_name;
        std::transform(dname.begin(), dname.end(), dname.begin(), ::tolower);
        if( ends_with(dname, ".cue") || ends_with(dname, ".mdf") || ends_with(dname, ".ccd") || ends_with(dname, ".chd") ){
            Button *tmp = new Button(wrapper, ent->d_name );
            string path = base_path + "/" + string(ent->d_name);
            tmp->setCallback([this,path]() { 
              MENU_LOG("CD Close: %s\n", path.c_str() ); 
              SDL_Event event = {};
              event.type = close_tray_;
              event.user.code = 0;
              event.user.data1 = malloc( (path.size()+1) * sizeof(char) );
              strcpy( (char*)event.user.data1, path.c_str() );
              event.user.data2 = 0;
              SDL_PushEvent(&event);
              this->popActiveMenu();
              this->swindow->dispose();
              this->swindow = nullptr;
            });  
            if( first_object ){
              first_object = false;
              pushActiveMenu(wrapper,toback);
            }
        }
      }
      closedir (dir);
    } else {
    }
    
    Button *b0 = new Button(wrapper, "Cancel");
    if( first_object ){
       first_object = false;
       pushActiveMenu(wrapper,toback);
    }    
    b0->setCallback([this]() { 
      MENU_LOG("Cancel\n"); 
      // Cancelling does not change the drive: the tray is already open (the
      // open-tray event was sent when the button was pressed). Restore that
      // state in the menu instead of sending a close event, which would
      // restart the game under the HLE BIOS (cs2.c).
      this->is_cdtray_open_ = true;
      if( this->bCdTray != nullptr ) this->bCdTray->setCaption("Close CD Tray");
      SDL_Event event = {};
      event.type = close_tray_;
      event.user.code = 0;
      //event.user.data1 = malloc( 256* sizeof(char) );
      //strcpy( (char*)event.user.data1, "filename" );
      //event.user.data2 = 0;
      //SDL_PushEvent(&event);
      this->popActiveMenu();
      swindow->dispose();
      swindow = nullptr;
     //this->performLayout();
    });

    //new Label(swindow,"Push key for " + key, "sans", 64);
    //swindow->center();
    swindow->setModal(true);
    swindow->requestFocus();

    this->performLayout();

}

void MenuScreen::getSelectedGUID( int user_index, std::string & selguid ){

  selguid = "-1";
  json j;
  std::stringstream ss;
  std::string userid;

  try{
    std::ifstream fin( config_file_ );
    fin >> j;
    fin.close();
    ss << "player" << (user_index+1);
    userid = ss.str();
    if( j.find(userid) != j.end() ) {
      InputManager::genJoyString( selguid, j[userid]["DeviceID"], j[userid]["deviceName"], j[userid]["deviceGUID"] );
    }
  }catch ( json::exception& e ){

  }

}


void MenuScreen::setupPlayerPsuhButton( int user_index, PopupButton *player, const std::string & label, ComboBox **cbo ){
  player->setFixedWidth(248);
  Popup *popup = player->popup();

  // 19 widgets do not fit on a 640x480 screen: as in showFileSelectDialog(),
  // the popup holds a single VScrollPanel, and keyboardEvent() scrolls the
  // focused button into view.
  int popup_height = std::min(this->size()[1] - 40, 440);
  popup->setFixedSize(Vector2i(260, popup_height));
  auto vscroll = new VScrollPanel(popup);
  vscroll->setFixedSize(Vector2i(260, popup_height));
  Widget *wrapper = new Widget(vscroll);
  wrapper->setFixedSize(Vector2i(260, popup_height));
  wrapper->setLayout(new GroupLayout(4,2,2,2));
  new Label(wrapper, label);

  std::string username;


  json j;
  string selguid="BADGUID";
  string selname="BADNAME";
  int padmode=0;
  std::stringstream ss;
  std::string userid;

  try{
    std::ifstream fin( config_file_ );
    fin >> j;
    fin.close();
    ss << "player" << (user_index+1);
    userid = ss.str();
    if( j.find(userid) != j.end() ) {
      selguid = j[userid]["deviceGUID"];
      InputManager::genJoyString( selguid, j[userid]["DeviceID"], j[userid]["deviceName"], j[userid]["deviceGUID"] );
      padmode = j[userid]["padmode"];
    }
  }catch ( json::exception& e ){

  }


  // No device selection ComboBox in the player pad popup (it would not fit on a 640x480 screen).

  Button * ba = new Button(wrapper,"Analog mode");
  ba->setFlags(Button::ToggleButton);
  if( padmode != 0 ){
    ba->setPushed(true);
  }else{
    ba->setPushed(false);
  }
  MenuScreen::tintToggle( ba, ba->pushed() );
  ba->setChangeCallback([this, userid, ba](bool state) {
    MenuScreen::tintToggle( ba, state );

    json j;
    std::ifstream fin( this->config_file_ );
    fin >> j;
    fin.close();
    //if( j.find(userid) == j.end() ) {
    //  return;
    //}

    try{
      if( state )
        j[userid]["padmode"] = 1;
      else
        j[userid]["padmode"] = 0;

      std::ofstream out(this->config_file_);
      out << j.dump(2);
      out.close();

      SDL_Event event = {};
      event.type = this->update_config_;
      event.user.code = 0;
      event.user.data1 = 0;
      event.user.data2 = 0;
      SDL_PushEvent(&event);   
    }catch ( json::exception& e ){

    }

  }); 

  
  player->setCallback([this,wrapper,player,user_index]() {
    pushActiveMenu(wrapper,player);

  });


  Button * b;
  b = new Button(wrapper, "UP");
  b->setCallback([this, user_index ]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("up");
  });
  b = new Button(wrapper, "DOWN");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("down");
  });
  b = new Button(wrapper, "LEFT");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("left");
  });
  b = new Button(wrapper, "RIGHT");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("right");
  });

  b = new Button(wrapper, "START");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("start");
  });

  b = new Button(wrapper, "A");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("a");
  }); 

  b = new Button(wrapper, "B");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("b");
  }); 

  b = new Button(wrapper, "C");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("c");
  });   
  b = new Button(wrapper, "X");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("x");
  });   
  b = new Button(wrapper, "Y");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("y");
  });     
  b = new Button(wrapper, "Z");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("z");
  });     
  b = new Button(wrapper, "L");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("l");
  });   
  b = new Button(wrapper, "R");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    showInputCheckDialog("r");
  });   
  b = new Button(wrapper, "Analog X");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    if( this->cuurent_deviceguid_ != "-1_Keyboard_-1" ){
      showInputCheckDialog("analogx");
    }
  });   
  b = new Button(wrapper, "Analog Y");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    if( this->cuurent_deviceguid_ != "-1_Keyboard_-1" ){
      showInputCheckDialog("analogy");
    }
  });   
  b = new Button(wrapper, "Analog L");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    if( this->cuurent_deviceguid_ != "-1_Keyboard_-1"){
      showInputCheckDialog("analogl");
    }
  });   
  b = new Button(wrapper, "Analog R");
  b->setCallback([this, user_index]{
    getSelectedGUID( user_index, this->cuurent_deviceguid_ );
    if( this->cuurent_deviceguid_ != "-1_Keyboard_-1"){
      showInputCheckDialog("analogr");
    }
  });   
}

/*
int MenuScreen::OnInputSelected( string & type, int & id, int & value ){
  current_player[ buttons[ current_setting_index ] ] = { {"id",id},{"value",value},{"type",type}};
  current_setting_index++;

  b
}
*/

int MenuScreen::onRawInputEvent( InputManager & imp, const std::string & deviceguid, const std::string & type, int id, int value ){
  if( swindow == nullptr ){ return -1; }
  if( swindow->title() == "Select File"){ return -1; }


  // The deviceguid comparison is disabled (#if 0), as in the 1.20 fork.
  // deviceguid comes from InputManager.cpp with the live SDL instance ID
  // (joy_name_and_guid, see genJoyString: "<InstanceID>_<Name>_<GUID>"), while
  // The device GUID is not compared: SDL instance IDs change with the connection order.

  // wait for key input?
  if( current_key_ != "l" && current_key_ != "r" ) {
    if( current_key_.find("analog") == std::string::npos  ){
      //if( type == "axis" ){
      //  return -1;
      //}
    }else{
      if( type != "axis" ){
        return -1;
      }
    }
  }

  imp.saveInputConfig( deviceguid , current_key_, type, id, value);
  swindow->dispose();
  swindow = nullptr;

  SDL_Event event = {};
  event.type = this->update_config_;
  event.user.code = 0;
  event.user.data1 = 0;
  event.user.data2 = 0;
  SDL_PushEvent(&event);   
  return 0;
}

bool MenuScreen::keyboardEvent( std::string & keycode , int scancode, int action, int modifiers){

  if( swindow != nullptr && swindow->title() != "Select File"){ return false; }

  /* on-screen keyboard has all input while it is open */
  if( kb_window_ != nullptr ) return raKbKey(keycode, action);

  /* RetroAchievements dialog: up/down cycles through its buttons
     (the labels in the popup must not take the focus) */
  if( action != 0 && ra_popup_ != nullptr && getActiveMenu() == ra_popup_ &&
      (keycode.find("up") != string::npos || keycode.find("down") != string::npos) ){
    /* cycle Login -> Logout -> Achievements -> Mode -> (Reset) -> Indicators */
    bool up = keycode.find("up") != string::npos;
    Button * all[6] = { ra_login_btn_, ra_logout_btn_, ra_ach_btn_, ra_mode_btn_, ra_hcreset_btn_, ra_ind_btn_ };
    Button * cyc[6];
    int n = 0, cur = 0;
    for( int i = 0; i < 6; i++ ) if( all[i] != nullptr && all[i]->visible() ) cyc[n++] = all[i];
    for( int i = 0; i < n; i++ ) if( mFocus == cyc[i] ) cur = i;
    Button * to = cyc[ (cur + (up ? n - 1 : 1)) % n ];
    mFocus->mouseEnterEvent(mFocus->position(), false);
    mFocus = to;
    mFocus->mouseEnterEvent(mFocus->position(), true);
    return false;
  }

  /* achievement list: up/down cycles Next page -> Previous page -> Close (the rows are labels) */
  if( action != 0 && ra_ach_nav_[0] != nullptr && imageWindow != nullptr && getActiveMenu() == imageWindow &&
      (keycode.find("up") != string::npos || keycode.find("down") != string::npos) ){
    bool up = keycode.find("up") != string::npos;
    int cur = 0;
    for( int i = 0; i < 3; i++ ) if( mFocus == ra_ach_nav_[i] ) cur = i;
    mFocus->mouseEnterEvent(mFocus->position(), false);
    mFocus = ra_ach_nav_[ (cur + (up ? 2 : 1)) % 3 ];
    mFocus->mouseEnterEvent(mFocus->position(), true);
    return false;
  }

  MENU_LOG("%s %d %d\n",keycode.c_str(),scancode,action);
  if (action != 0) {
          
    if ( keycode.find("down") != string::npos ) {
      nanogui::Button* btn = (nanogui::Button*)mFocus;
      //btn->setPushed(false);
      mFocus->mouseEnterEvent(mFocus->position(), false);
      mFocus = getActiveMenu()->getNearestWidget(mFocus, 1);
      MENU_LOG("%s is selected\n",((nanogui::Button*)mFocus)->caption().c_str());
      mFocus->mouseEnterEvent(mFocus->position(), true);
     
      auto wrapper = mFocus->parent();
      if( wrapper != nullptr ){
          auto vscroll = wrapper->parent();
          if( vscroll != nullptr && dynamic_cast<VScrollPanel*>(vscroll) != nullptr  ){
              MENU_LOG("pos=%d vpod = %f\n",mFocus->position().y(),((VScrollPanel*)vscroll)->getScrollPos()  );
              if( mFocus->position().y() - ((VScrollPanel*)vscroll)->getScrollPos() > vscroll->height() ){
                Vector2i p(0,0);
                Vector2f rel(0.0,-1.0);
                (VScrollPanel*)vscroll->scrollEvent(p,rel);
            }
        }
      }

    }else if ( keycode.find("up") != string::npos ) {
      nanogui::Button* btn = (nanogui::Button*)mFocus;
      //btn->setPushed(false);
      mFocus->mouseEnterEvent(mFocus->position(), false);
      mFocus = getActiveMenu()->getNearestWidget(mFocus, 0);
      mFocus->mouseEnterEvent(mFocus->position(), true);

      auto wrapper = mFocus->parent();
      if( wrapper != nullptr ){
          auto vscroll = wrapper->parent();
          if( vscroll != nullptr ){
              MENU_LOG("pos=%d vpod = %f\n",mFocus->position().y(),((VScrollPanel*)vscroll)->getScrollPos()  );
              if( mFocus->position().y() - ((VScrollPanel*)vscroll)->getScrollPos() < 0 ){
                Vector2i p(0,0);
                Vector2f rel(0.0,1.0);
                (VScrollPanel*)vscroll->scrollEvent(p,rel);
            }
        }
      }

    }
    /* Left/Right adjust the volume sliders and the sound core selection. */
    else if ( keycode.find("left") != string::npos ) {
      nanogui::Button* fb = dynamic_cast<nanogui::Button*>(mFocus);
      if (fb != nullptr && (fb == snd_cdda_btn_ || fb == snd_sfx_btn_))
        soundVolStep(fb, -1);
      else if (fb != nullptr && (fb == snd_core_btn_ || fb == snd_core_new_btn_)) soundCoreStep(-1);
    }else if ( keycode.find("right") != string::npos ) {
      nanogui::Button* fb = dynamic_cast<nanogui::Button*>(mFocus);
      if (fb != nullptr && (fb == snd_cdda_btn_ || fb == snd_sfx_btn_))
        soundVolStep(fb, +1);
      else if (fb != nullptr && (fb == snd_core_btn_ || fb == snd_core_new_btn_)) soundCoreStep(+1);
    }

    if (keycode == "a") {
        mFocus->mouseButtonEvent(mFocus->position(), SDL_BUTTON_LEFT, true, 0);
    }
  }
 
  if (action == 0 && keycode == "a" ) {
    mFocus->mouseButtonEvent(mFocus->position(), SDL_BUTTON_LEFT, false, 0);
  }    

  return false;
}

void MenuScreen::setBackGroundImage( const std::string & fname ){
  if(imageid_!=0){
    nvgDeleteImage(mNVGContext,imageid_);
  }
  imageid_ = nvgCreateImage(mNVGContext, fname.c_str(), 0 );
  nvgImageSize(mNVGContext, imageid_, &imgw_, &imgh_);
  MENU_LOG("imageid_:%d w:%d h:%d\n",imageid_,imgw_,imgh_);
	imgPaint_ = nvgImagePattern(mNVGContext, 0, 0, imgw_,imgh_, 0, imageid_, 0.5f);
}

void MenuScreen::setTmpBackGroundImage( const std::string & fname ){
  if( !fs::exists(fname) ) {
      setDefalutBackGroundImage();
      return;
  }

  if(imageid_tmp_!=0){
    nvgDeleteImage(mNVGContext,imageid_tmp_);
  }
  imageid_tmp_ = nvgCreateImage(mNVGContext, fname.c_str(), 0 );
  nvgImageSize(mNVGContext, imageid_, &imgw_, &imgh_);
  MENU_LOG("imageid_:%d w:%d h:%d\n",imageid_,imgw_,imgh_);
	imgPaint_ = nvgImagePattern(mNVGContext, 0, 0, imgw_,imgh_, 0, imageid_tmp_, 0.5f);
}

void MenuScreen::setDefalutBackGroundImage(){
  if(imageid_==0){
    return;
  }
  nvgImageSize(mNVGContext, imageid_, &imgw_, &imgh_);
	imgPaint_ = nvgImagePattern(mNVGContext, 0, 0, imgw_,imgh_, 0, imageid_, 0.5f);
}


void MenuScreen::draw(NVGcontext *ctx){

  if( imageid_ != 0 ){
    nvgSave(ctx);
		nvgBeginPath(ctx);
		nvgRect(ctx, 0, 0, imgw_,imgh_);
		nvgFillPaint(ctx, imgPaint_);
    //nvgFillColor(ctx, nvgRGBA(0,160,192,255));
		nvgFill(ctx);
    nvgRestore(ctx);
  }

  if( ra_popup_ != nullptr && ra_popup_->visible() ) raStatusUpdate(false);

  //window->center();
  Screen::draw(ctx);
}

void MenuScreen::setCurrentInputDevices( std::map<SDL_JoystickID, SDL_Joystick*> & joysticks ){

  joysticks_ = joysticks;
  
  vector<string> items;
  vector<string> itemsShort;

  json j;
  string selguid="BADGUID";
  std::stringstream ss;
  std::string userid;

  try{
    std::ifstream fin( config_file_ );
    fin >> j;
    fin.close();
  }catch ( json::exception& e ){

  }


  for( auto it = joysticks_.begin(); it != joysticks_.end() ; ++it ) {
      SDL_Joystick* joy = it->second;
      SDL_JoystickID joyId = SDL_JoystickInstanceID(joy);
      char guid[65];
      SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, 65);
      string key_string;
      InputManager::genJoyString( key_string, joyId, SDL_JoystickName(joy), guid );

      items.push_back(key_string);
      itemsShort.push_back(SDL_JoystickName(joy));
  }  
  itemsShort.push_back("KeyBoard");
  items.push_back("-1_Keyboard_-1");
  itemsShort.push_back("Disable");
  items.push_back("Disable_-2");

  for( int i=0; i< player_configs_.size(); i++ ){
    ComboBox * cb = player_configs_[i].cb ;
    if( cb != nullptr ){
      cb->setItems(itemsShort,itemsShort); 
      std::stringstream ss;
      ss << "player" << (i+1);
      std::string userid = ss.str();      
      if( j.find(userid) != j.end() ) {
        InputManager::genJoyString( selguid, j[userid]["DeviceID"], j[userid]["deviceName"], j[userid]["deviceGUID"] );
      }else{
        selguid = "Disable_-2";
      }
      for( int i=0; i<items.size(); i++  ){
        if( items[i] == selguid ){
          cb->setSelectedIndex(i);
          break;
        }
      }
    }
  }
  performLayout();
}

void MenuScreen::pushActiveMenu( Widget *active, Widget * button  ){
    PreMenuInfo tmp;
    tmp.window = active;
    tmp.button = button;
    menu_stack.push(tmp);
    if( mFocus != nullptr ){
      mFocus->mouseEnterEvent(mFocus->position(), false);
      const std::vector<Widget *> &children = active->children();
      for( int i=0; i<children.size(); i++ ){
        if(  dynamic_cast<Button*>(children[i]) != nullptr ){
          mFocus = children[i];
          mFocus->requestFocus();
          MENU_LOG("%s is selected\n",((nanogui::Button*)mFocus)->caption().c_str());
          mFocus->mouseEnterEvent(mFocus->position(), true);
          break;
        }
      }
  }
}

void MenuScreen::popActiveMenu(){
  MENU_LOG("popActiveMenu ");
  if( menu_stack.size() <= 1 ) return;
  PreMenuInfo tmp;
  tmp = menu_stack.top();
  PopupButton * p = dynamic_cast<PopupButton*>(tmp.button);
  if( p != nullptr ) {
    MENU_LOG("Hide menu\n");
    mFocus->mouseEnterEvent(p->absolutePosition(),false);
    p->setPushed(false);
    p->popup()->setVisible(false);
    p->mouseEnterEvent(p->absolutePosition(),true);
    mFocus = p;
    mFocus->requestFocus();
    menu_stack.pop();
  }else{
    tmp = menu_stack.top();
    /*
    const std::vector<Widget *> &children = tmp.window->children();
    for( int i=0; i<children.size(); i++ ){
      if(  dynamic_cast<Button*>(children[i]) != nullptr ){
        Widget * px = children[i];
        if( tmp.button != nullptr ) {
          mFocus->mouseEnterEvent(tmp.button->absolutePosition(),false);
          px->mouseEnterEvent(tmp.button->absolutePosition(),true);
          MENU_LOG("%s is selected\n",((nanogui::Button*)mFocus)->caption().c_str());
        }
        mFocus = px;
        mFocus->requestFocus();        
        break;
      }
    }
    */
    if( tmp.button != nullptr ) {
      mFocus->mouseEnterEvent(tmp.button->absolutePosition(),false);
      tmp.button->mouseEnterEvent(tmp.button->absolutePosition(),true);
      MENU_LOG("%s is selected\n",((nanogui::Button*)tmp.button)->caption().c_str());
    }
    mFocus = tmp.button;
    mFocus->requestFocus();        
    menu_stack.pop();
  }
  return;
}

Widget * MenuScreen::getActiveMenu(){
  PreMenuInfo tmp;
  tmp = menu_stack.top();
  return tmp.window;
}

int MenuScreen::onBackButtonPressed(){
  MENU_LOG("onBackButtonPressed\n");

  if( kb_window_ != nullptr ){   /* B = Cancel in the on-screen keyboard */
    raKbClose();
    return 1;
  }
  
  if( swindow != nullptr ){

    if( swindow->title() == "Select File"){ 
      this->popActiveMenu();
    }
    swindow->dispose();
    swindow = nullptr;
    return 1;    
  }

  if( imageWindow != nullptr ){
    this->popActiveMenu();
    imageWindow->dispose();
    imageWindow = nullptr;    
    ra_ach_head_ = nullptr;   /* the achievement list may have been this window */
    for( int i = 0; i < 6; i++ ){ ra_ach_rows_[i] = nullptr; ra_ach_icons_[i] = nullptr; }
    for( int i = 0; i < 3; i++ ) ra_ach_nav_[i] = nullptr;
    return 1;
  }
  
 //if( swindow != nullptr ){ 
 //  printf("swindow != null\n");
 //  return 1; 
 //}

  Widget * item = getActiveMenu();
  if( item == tools ){
    return 0;
  }
  popActiveMenu();
/*
  if( activeWindow == tools ){
    return 0;
  }
  player1->setPushed(false);
  player2->setPushed(false);
  activeWindow = tools;
  Widget * p = tools->children()[0];
  if( p != nullptr ) {
    p->mouseEnterEvent(p->absolutePosition(),true);
    mFocus = p;
    mFocus->focusEvent(true);
  }
*/  
  return 1;
}

