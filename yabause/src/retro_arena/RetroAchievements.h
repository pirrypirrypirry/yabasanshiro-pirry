/*  Copyright 2026 pirrypirrypirry

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

/*  RetroAchievements: login, game identification and achievement unlocking
    (rcheevos rc_client).

    libcurl is loaded at run time with dlopen(), so there is no build-time
    dependency. If it is missing the status is RA_NOT_AVAILABLE and the
    emulator runs normally.

    All functions except RA_GetOverlayText() must be called from the main
    (emulation) thread. RA_GetOverlayText() is called from the render thread.
*/
#ifndef RETROACHIEVEMENTS_H
#define RETROACHIEVEMENTS_H

#include <string>
#include <vector>

enum RAState {
  RA_NOT_CONFIGURED = 0,  /* no credentials */
  RA_LOGGING_IN,          /* request running in the background */
  RA_LOGGED_IN,
  RA_ERROR,
  RA_NOT_AVAILABLE        /* libcurl could not be loaded */
};

struct RAStatus {
  RAState state;
  std::string name;       /* display name, valid if RA_LOGGED_IN */
  unsigned int score;     /* hardcore score (softcore score if that is 0) */
  std::string error;      /* valid if RA_ERROR / RA_NOT_AVAILABLE */
  bool game_loaded;       /* the disc is known to RetroAchievements */
  std::string game_title; /* valid if game_loaded */
  unsigned int ach_unlocked;  /* valid if game_loaded */
  unsigned int ach_total;     /* valid if game_loaded */
  bool hardcore;          /* hardcore selected in the config */
  bool hardcore_active;   /* hardcore is in force for the running game */
  bool indicators;        /* indicator box (trackers, challenges, progress) shown */
  bool cheats;            /* a cheat was on since the last reset: nothing is unlocked */
  RAStatus() : state(RA_NOT_CONFIGURED), score(0), game_loaded(false),
               ach_unlocked(0), ach_total(0), hardcore(false),
               hardcore_active(false), indicators(true), cheats(false) {}
};

/* Read ~/.yabasanshiro/retroachievements.cfg; with a token or a password
   start a login in the background. Call once at start. */
void RA_Init();

/* Start a login with user name and password (background). The token is
   stored in the cfg file on success, the password is never stored. */
void RA_LoginWithPassword(const std::string & user, const std::string & pass);

/* Remove token and password from the cfg file and reset the status. */
void RA_Logout();

/* Thread-safe snapshot of the status. */
void RA_GetStatus(RAStatus & st);

/* Identify the inserted disc (Saturn header + hash of the first sector) and
   load the achievement set once logged in. Call once after yabauseinit(). */
void RA_LoadGame();
/* one entry of the achievement list shown in the menu */
struct RAAchievement {
  std::string title;
  std::string description;
  unsigned int points;
  bool unlocked;
  std::string progress;   /* measured progress, e.g. "3/10", empty if none */
};
/* core achievements of the loaded game, unlocked first; false if no game is loaded */
bool RA_GetAchievements(std::vector<RAAchievement> & out);
/* after a disc swap in a multi-disc game */
void RA_OnDiscChanged();

/* Call after every emulated field (YabauseExec): serves server answers and
   evaluates the achievements. */
void RA_DoFrame();

/* Call while the menu is open instead of RA_DoFrame(). */
void RA_Idle();

/* Call when the emulator is reset; cheats_on = a cheat is still switched on. */
void RA_OnReset(bool cheats_on);

/* Call when a cheat is switched on (also at game start). Until the next reset
   with all cheats off no achievement or leaderboard is evaluated, in casual
   and in hardcore mode. */
void RA_CheatsUsed();

/* Hardcore: logged in, game loaded and hardcore in force. */
bool RA_HardcoreActive();
/* hardcore was selected while a game runs and waits for the reset */
bool RA_HardcorePending();

/* Select hardcore or softcore (stored in the cfg). Hardcore to softcore is
   immediate, softcore to hardcore only takes effect after the game restarts
   (next reset). */
void RA_SetHardcore(bool on);
/* show or hide the indicator box; stored in the config, leaderboard/achievement messages stay */
void RA_SetIndicators(bool on);

/* Show a short message in the on-screen overlay for 4 seconds. */
void RA_ShowMessage(const char * text);

/* Wait for a running request and release resources. */
void RA_Shutdown();

/* Render thread: copies the active overlay message, returns 1 if there is one. */
extern "C" int RA_GetOverlayText(char *out, int maxlen);
/* lasting indicators (leaderboard trackers, challenges, progress), one per line; 0 if none. Render thread. */
extern "C" int RA_GetIndicatorText(char *out, int maxlen);

#endif
