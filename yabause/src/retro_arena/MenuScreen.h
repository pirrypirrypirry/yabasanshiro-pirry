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

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>
#undef Success 
#include "nanogui/screen.h"
#include <nanogui/textbox.h>
#include "nanovg.h"
#include <string.h>
#include <stack>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


#include "InputConfig.h"
#include "RetroAchievements.h"
#include <vector>

using namespace nanogui;

class InputManager;

struct PreMenuInfo {
    Widget* window = nullptr;
    Widget* button = nullptr;
};

struct PlayerConfig {
    ComboBox * cb = nullptr;
    PopupButton *player = nullptr;
};

class MenuScreen : public nanogui::Screen
{
public:
    //Widget *activeWindow = nullptr;
    Widget *tools = nullptr;
    std::vector<PlayerConfig> player_configs_;
/*    
    PopupButton *player1;
    ComboBox * p1cb = nullptr;
    PopupButton *player2 = nullptr;
    ComboBox * p2cb = nullptr;
*/    
    Button *bAnalog = nullptr;
    Button *bCdTray = nullptr;
    Button *bNextDisc = nullptr;
    bool is_cdtray_open_ = false;

    std::map<SDL_JoystickID, SDL_Joystick*> joysticks_;

    int imageid_ = 0;
    int imageid_tmp_ = 0;
    int imgw_ = 0;
    int imgh_ = 0;
    NVGpaint imgPaint_ = {};     
    void setBackGroundImage( const std::string & fname );
    void setTmpBackGroundImage( const std::string & fname );
    void setDefalutBackGroundImage();

    nanogui::Window *window;
    nanogui::Window *swindow;
    nanogui::Window *imageWindow;
    
    
    MenuScreen( SDL_Window* pwindow, int rwidth, int rheight, const std::string & fname, const std::string & game  );
    virtual bool keyboardEvent( std::string & keycode , int scancode, int action, int modifiers);
    virtual void draw(NVGcontext *ctx);

	uint32_t reset_ = 0;
	void setResetMenuEventCode( uint32_t type ){ reset_ = type; }

	uint32_t pad_ = 0;
	void setTogglePadModeMenuEventCode( uint32_t type ){ pad_ = type; }

	uint32_t toggile_fps_ = 0;
	void setToggleFpsCode( uint32_t type ){ toggile_fps_ = type; }

	uint32_t toggile_frame_skip_ = 0;
	void setToggleFrameSkip( uint32_t type ){ toggile_frame_skip_ = type; }

	uint32_t update_config_ = 0;
	void setUpdateConfig( uint32_t type ){ update_config_ = type; }

	uint32_t open_tray_ = 0;
	void setOpenTrayMenuEventCode( uint32_t type ){ open_tray_ = type; }

	uint32_t close_tray_ = 0;
	void setCloseTrayMenuEventCode( uint32_t type ){ close_tray_ = type; }

    std::string current_game_path_;
    void setCurrentGamePath( const char * path ){ current_game_path_ = path; }

    uint32_t save_state_ = 0;
    void setSaveStateEventCode( uint32_t code ){ save_state_ = code; }

    uint32_t load_state_ = 0;
    void setLoadStateEventCode( uint32_t code ){ load_state_ = code; }

    void showInputCheckDialog( const std::string & key );

    void showFileSelectDialog( Widget * parent, Widget * toback, const std::string & base_path );

    void setupPlayerPsuhButton( int user_index, PopupButton *player, const std::string & label, ComboBox **cb );

    int onRawInputEvent( InputManager & imp, const std::string & deviceguid, const std::string & type, int id, int value );

    void setCurrentInputDevices( std::map<SDL_JoystickID, SDL_Joystick*> & joysticks );

    std::stack<PreMenuInfo> menu_stack;
    void pushActiveMenu( Widget *active,  Widget * button );
    void popActiveMenu();
    Widget * getActiveMenu();

    void setConfigFile( const std::string & fname ){
        config_file_ = fname;
    }
    void getSelectedGUID( int user_index, std::string & selguid );


    //void showSaveStateDialog( Widget * parent, Widget * toback );
    void setCurrentGameId( const std::string & id ){ cuurent_game_id_ = id; }
    void showSaveStateDialog( Popup *popup );
    void showLoadStateDialog( Popup *popup );
    void showConfigDialog( PopupButton *popup );
    /* "Sound Volume" submenu with two sliders, adjusted with left/right on the
       d-pad. The button pointers are kept so keyboardEvent can recognise them. */
    void showSoundDialog( PopupButton *popup );
    void soundVolStep( nanogui::Button * b, int direction );
    nanogui::Button * snd_cdda_btn_ = nullptr;
    nanogui::Button * snd_sfx_btn_  = nullptr;
    /* Sound core selection (classic/new); takes effect on the next game start. */
    void soundCoreStep( int direction );
    nanogui::Button * snd_core_btn_ = nullptr;
    nanogui::Button * snd_core_new_btn_ = nullptr;   /* second entry "New Sound Core" (snd_core_btn_ = "Old") */
    nanogui::Label * snd_core_hint_ = nullptr;    /* "needs emu restart" line below */
    void soundCoreShow( int selected );
    /* "Cheats" page, built on first open (the list exists only after yabauseinit) */
    void showCheatDialog( PopupButton *popup );
    bool cheat_built_ = false;
    nanogui::Widget * cheat_wrapper_ = nullptr;   /* container holding the buttons, for pushActiveMenu */
    void refreshConfigSwitches();
    /* RetroAchievements login: status dialog + on-screen keyboard (d-pad). */
    void showRetroAchievementsDialog( PopupButton *popup );
    void raStatusUpdate( bool force );
    void raAchOpen( Widget * from );
    void raAchShowPage( int page );
    void raKbOpen();
    void raKbClose();
    void raKbMove( int dr, int dc );
    void raKbPress();
    void raKbRefresh();
    bool raKbKey( const std::string & keycode, int action );
    PopupButton * ra_popup_btn_ = nullptr;
    Popup * ra_popup_ = nullptr;
    Label * ra_status_lbl_ = nullptr;
    Button * ra_login_btn_ = nullptr;
    Button * ra_logout_btn_ = nullptr;
    Button * ra_mode_btn_ = nullptr;           /* Softcore / Hardcore toggle */
    Button * ra_ach_btn_ = nullptr;            /* opens the achievement list */
    Button * ra_ind_btn_ = nullptr;            /* indicator box on/off */
    Button * ra_hcreset_btn_ = nullptr;        /* red Reset beside Mode while hardcore waits */
    Label * ra_hc_lbl_ = nullptr;              /* red note: hardcore needs a reset */
    Button * ra_ach_nav_[3] = { nullptr, nullptr, nullptr };  /* Next / Previous / Close in the list */
    std::vector<RAAchievement> ra_ach_;        /* achievement list window: entries, page, widgets */
    int ra_ach_page_ = 0;
    Label * ra_ach_head_ = nullptr;
    Label * ra_ach_rows_[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    Label * ra_ach_icons_[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };  /* check / lock per row */
    PopupButton * load_state_btn_ = nullptr;   /* disabled in hardcore mode */
    std::string ra_status_text_;
    Window * kb_window_ = nullptr;
    TextBox * kb_display_ = nullptr;
    Button * kb_eye_ = nullptr;
    bool kb_show_pw_ = false;     /* eye toggled: password shown in clear text */
    int kb_maxchars_ = 24;
    struct KbKey { Button * btn; char ch; int special; };   /* special: 0 char, 1 shift, 2 delete, 3 OK, 4 cancel, 5 eye */
    std::vector< std::vector<KbKey> > kb_rows_;
    int kb_r_ = 0;
    int kb_c_ = 0;
    int kb_step_ = 0;            /* 0 = user name, 1 = password */
    bool kb_shift_ = false;
    std::string kb_text_;
    std::string kb_user_;
    // Tint enabled toggle buttons green.
    static void tintToggle( Button * b, bool on );

public:  // events
    int onBackButtonPressed();    
    int onShow();    

protected:
    std::string cuurent_deviceguid_;
    std::string current_key_;
    std::string config_file_;
    std::string cuurent_game_id_;

    // Widget handles of the config dialog buttons/comboboxes, so that
    // refreshConfigSwitches() can update their state from the preference file
    // (showConfigDialog() builds them only once, in the constructor).
    ComboBox *cfg_resolution_cb_ = nullptr;
    ComboBox *cfg_aspect_cb_ = nullptr;
    ComboBox *cfg_rotate_res_cb_ = nullptr;
    Button *cfg_compute_shader_btn_ = nullptr;
    Button *cfg_interlace_half_btn_ = nullptr;
    Button *cfg_rotate_screen_btn_ = nullptr;

};
