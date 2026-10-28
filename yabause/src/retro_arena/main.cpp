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

#include <stdlib.h>
#include <time.h>
#include <dirent.h>

extern "C" { void ScspSetCddaVolume(int v); void ScspSetSfxVolume(int v); }
extern "C" { extern int g_scsp_dsp_off; }       /* scsp.c */
extern "C" { extern int g_interlace_half_menu; }   /* vidogl.c */
#include <exception>
#include <functional>
#include <string>
#include <algorithm>   /* std::transform */
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if GCC_VERSION < 9
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include <sys/resource.h>
#include <errno.h>
#include <pthread.h>

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>


extern "C" {
#include "../config.h"
#include "yabause.h"
#include "vdp2.h"
#include "scsp.h"
#include "vidsoft.h"
#include "vidogl.h"
#include "peripheral.h"
#include "persdljoy.h"
#include "m68kcore.h"
#include "sh2core.h"
#include "sh2int.h"
#include "cdbase.h"
#include "cs2.h"
#include "cheat.h"
#include "debug.h"
#include "sndal.h"
#include "sndsdl.h"
#include "osdcore.h"
#include "ygl.h"
#include "../threads.h"
#include "libpng16/png.h"
}

#include "InputManager.h"
#include "MenuScreen.h"
extern "C" void SmpcSetSystemLanguage(int id);   /* smpc.c */
#include "Preference.h"
#include "RetroAchievements.h"

#define YUI_LOG printf

static char last_state_filename[256] = "\0";
char s_savepath[256] ="\0";

extern "C" {
static char biospath[256] = "/home/pigaming/RetroPie/BIOS/saturn/bios.bin";
static char cdpath[256] = ""; ///home/pigaming/RetroPie/roms/saturn/nights.cue";
//static char cdpath[256] = "/home/pigaming/RetroPie/roms/saturn/gd.cue";
//static char cdpath[256] = "/home/pigaming/RetroPie/roms/saturn/Virtua Fighter Kids (1996)(Sega)(JP).ccd";
static char buppath[256] = "./back.bin";
static char mpegpath[256] = "\0";
static char cartpath[256] = "\0";
static bool menu_show = false;

#define LOG(...) do { } while (0)

/* a cheat of the loaded .cht is switched on */
static bool cheats_on() {
  int count = 0;
  cheatlist_struct *list = CheatGetList(&count);
  for (int i = 0; list != NULL && i < count; i++)
    if (list[i].enable) return true;
  return false;
}

M68K_struct * M68KCoreList[] = {
  &M68KDummy,
  #ifdef HAVE_C68K
  &M68KC68K,
  #endif
  #ifdef HAVE_Q68
  &M68KQ68,
  #endif
#ifdef HAVE_MUSASHI
  &M68KMusashi,
#endif
  NULL
};

SH2Interface_struct *SH2CoreList[] = {
  &SH2Interpreter,
  &SH2DebugInterpreter,
#ifdef SH2_DYNAREC
  &SH2Dynarec,
#endif
#if DYNAREC_DEVMIYAX
  &SH2Dyn,
  &SH2DynDebug,
#endif
  NULL
};

PerInterface_struct *PERCoreList[] = {
  &PERDummy,
  &PERSDLJoy,
  NULL
};

CDInterface *CDCoreList[] = {
  &DummyCD,
  &ISOCD,
  NULL
};

SoundInterface_struct *SNDCoreList[] = {
  &SNDDummy,
#ifdef HAVE_LIBSDL
  &SNDSDL,
#endif
  NULL
};

VideoInterface_struct *VIDCoreList[] = {
  &VIDDummy,
  &VIDOGL,
  NULL
};


#ifdef YAB_PORT_OSD
#include "nanovg/nanovg_osdcore.h"

OSD_struct *OSDCoreList[] = {
  &OSDNnovg,
  &OSDDummy,
  NULL
};
#endif


static SDL_Window* wnd;
static SDL_GLContext glc;

/* Release the GL context and the window before SDL_Quit(). SDL_Quit() alone
   does not reliably tear down the Mali/EGL context; the next start could then
   come up with a stale graphics state (frozen picture while the emulation
   keeps running). Order: context, window, SDL. */
static void GlCleanShutdown(void)
{
  static int done = 0;
  if (done) return;
  done = 1;
  if (glc) { SDL_GL_MakeCurrent(wnd, nullptr); SDL_GL_DeleteContext(glc); glc = nullptr; }
  if (wnd) { SDL_DestroyWindow(wnd); wnd = nullptr; }
  SDL_Quit();
  /* Every exit path ends here after the backup RAM has been saved. Skip the
     library teardown after main(): the Mali driver can crash unloading while
     another thread still waits inside libEGL. */
  fflush(NULL);
  _exit(0);
}
int g_EnagleFPS = 0; /* FPS overlay; set from the "Show Fps" config key at start, toggled from the menu */
int g_resolution_mode = 0;
int g_rotate_resolution_mode = 0;
int g_keep_aspect_rate = 0;
int g_scsp_sync = 1;
int g_frame_skip = 1;
/* Set to 0 by -nf on the command line, which then overrides the "frame skip" config key. */
static int g_frame_skip_cli = -1;
int g_emulated_bios = 1;
InputManager* inputmng;
MenuScreen * menu;

using std::string;
string g_keymap_filename;

void hideMenuScreen();

//---------------------------------------------------------------------------------------------
NVGcontext * getGlobalNanoVGContext(){
  return menu->nvgContext();
}

void DrawDebugInfo()
{
}

void YuiErrorMsg(const char *string)
{
  LOG("%s",string);
}

void YuiSwapBuffers(void)
{
  SDL_GL_SwapWindow(wnd);
  SetOSDToggle(g_EnagleFPS);
}

int YuiRevokeOGLOnThisThread(){
  LOG("revoke thread\n");
  SDL_GL_MakeCurrent(wnd,nullptr);
  return 0;
}

int YuiUseOGLOnThisThread(){
  LOG("use thread\n");
  SDL_GL_MakeCurrent(wnd,glc);
  return 0;
}

}

int saveScreenshot( const char * filename );

int padmode = 0;

/* Search for a Saturn BIOS image next to the game and in /roms/bios and
   /roms2/bios. Returns 1 and stores the path in goal when one is found. */
static int BiosFind(const char *game_path, char *goal, size_t goal_len)
{
  std::vector<std::string> dir_path;
  if (game_path != NULL && game_path[0] == '/') {
    std::string sp(game_path);
    size_t p1 = sp.find('/', 1);                       /* /roms2/saturn/x.chd -> /roms2 */
    if (p1 != std::string::npos) dir_path.push_back(sp.substr(0, p1) + "/bios");
  }
  dir_path.push_back("/roms/bios");
  dir_path.push_back("/roms2/bios");
  const char *names[] = { "saturn_bios.bin", "sega_101.bin", "mpr-17933.bin", "sega1003.bin", NULL };
  for (size_t o = 0; o < dir_path.size(); o++)
    for (int n = 0; names[n] != NULL; n++) {
      std::string k = dir_path[o] + "/" + names[n];
      FILE *f = fopen(k.c_str(), "rb");
      if (f != NULL) { fclose(f); snprintf(goal, goal_len, "%s", k.c_str()); return 1; }
    }
  for (size_t o = 0; o < dir_path.size(); o++) {
    DIR *d = opendir(dir_path[o].c_str());
    if (d == NULL) continue;
    struct dirent *e;
    for (;;) {
      e = readdir(d);
      if (e == NULL) break;
      std::string nm(e->d_name), kl(nm);
      std::transform(kl.begin(), kl.end(), kl.begin(), ::tolower);
      if (kl.find("saturn") == std::string::npos && kl.find("sega") == std::string::npos && kl.find("mpr-1") == std::string::npos) continue;
      std::string k = dir_path[o] + "/" + nm;
      struct stat sb;
      if (stat(k.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_size == 524288) {
        closedir(d); snprintf(goal, goal_len, "%s", k.c_str()); return 1;
      }
    }
    closedir(d);
  }
  return 0;
}

int yabauseinit()
{
  int res;
  yabauseinit_struct yinit = {};

  Preference pre( cdpath );

  yinit.m68kcoretype = M68KCORE_MUSASHI;
  yinit.percoretype = PERCORE_DUMMY;
#if defined(__PC__)
  yinit.sh2coretype = 0;
#else
  yinit.sh2coretype = 3;
#endif  
  //yinit.vidcoretype = VIDCORE_SOFT;
  yinit.vidcoretype = 1;
  yinit.sndcoretype = SNDCORE_SDL;
  //yinit.sndcoretype = SNDCORE_DUMMY;
  //yinit.cdcoretype = CDCORE_DEFAULT;
  yinit.cdcoretype = CDCORE_ISO;
  yinit.carttype = CART_DRAM32MBIT;

  /* The Saturn clock follows the host wall clock, as on hardware. */
  yinit.clocksync = 0;
  yinit.basetime  = 0;
  yinit.regionid = 0;
  /* A real BIOS is only used when given with -b on the command line. */
  if( g_emulated_bios ){
    yinit.biospath = NULL;
  }else{
    yinit.biospath = biospath;
  }
  yinit.cdpath = cdpath;
  yinit.buppath = buppath;
  yinit.mpegpath = mpegpath;
  yinit.cartpath = cartpath;
  /* NTSC/60 for all games unless the game config sets "force 50hz". */
  yinit.videoformattype = pre.getBool("force 50hz", false)
                            ? VIDEOFORMATTYPE_PAL : VIDEOFORMATTYPE_NTSC;
  /* The "frame skip" config key applies only when -nf was not given. */
  if (g_frame_skip_cli < 0) {
    g_frame_skip = pre.getBool( "frame skip", true ) ? 1 : 0;
  }
  yinit.frameskip = g_frame_skip;
  yinit.usethreads = 0;
  yinit.skip_load = 0;    
  yinit.video_filter_type = 0;
#if defined(__JETSON__)    
  yinit.polygon_generation_mode = GPU_TESSERATION;
#else
  yinit.polygon_generation_mode = PERSPECTIVE_CORRECTION;
#endif
  /* Sound: new SCSP core. Its DSP is enabled by the "sound dsp" key in the
     global default.config (default on); per-game entries are ignored. */
  yinit.use_new_scsp = 1;
  g_scsp_dsp_off = pre.getIntOnlyGlobal("sound dsp", 1) ? 0 : 1;
  /* Half-height interlaced backgrounds, "interlace half" config key (default off). */
  g_interlace_half_menu = pre.getInt("interlace half", 0) ? 1 : 0;
  yinit.resolution_mode = pre.getInt( "Resolution" ,g_resolution_mode);
  yinit.rotate_screen = pre.getBool( "Rotate screen" , false );
  yinit.scsp_sync_count_per_frame = g_scsp_sync;
  yinit.extend_backup = 1;
#if defined(__JETSON__)  
  yinit.scsp_main_mode = 0;
#else
  yinit.scsp_main_mode = 1;
#endif
  yinit.rbg_resolution_mode = pre.getInt( "Rotate screen resolution" ,g_rotate_resolution_mode);
  /* Default true: with false some games (e.g. Panzer Dragoon) show a black screen. */
  yinit.rbg_use_compute_shader = pre.getBool( "Use compute shader" , true);
  // Texture cache persistence across frames ("texcache persist", default on).
  {
    extern int g_texcache_persist;
    g_texcache_persist = pre.getBool("texcache persist", true) ? 1 : 0;
    /* Apply the stored menu volumes at game start. */
    ScspSetCddaVolume(pre.getInt("cd volume", 100));
    ScspSetSfxVolume(pre.getInt("sfx volume", 100));
  }
  /* "state path" config key: optional directory for save states. */
  {
    std::string sp = pre.getString("state path", "");
    if (!sp.empty()) {
      if (sp.back() != '/') sp += "/";
      struct stat sst = {0};
      if (stat(sp.c_str(), &sst) == -1) mkdir(sp.c_str(), 0777);
      if (stat(sp.c_str(), &sst) == 0) {
        strcpy(s_savepath, sp.c_str());
      } else {
        printf("state path %s is not usable, keeping %s\n",
               sp.c_str(), s_savepath);
      }
    }
  }
  {
    /* "mute cdda" config key: mute CD audio, keep sound effects. */
    ScspSetCddaMute(pre.getBool("mute cdda", false) ? 1 : 0);
  }

  res = YabauseInit(&yinit);
  { /* Cheats for the game from ~/.yabasanshiro/cheats/*.cht (libretro format, Action Replay codes).
       Names are compared without bracket tags and punctuation ("Burning Rangers (Europe).chd" ~
       "Burning Rangers (USA).cht"); the same region is preferred, files in the RetroArch save format
       ("(RetroArch)") come last. Entries picked in the menu are stored in the game config as
       "cheat <N>"; otherwise an entry is on only if the file says enable = true. */
    auto key = [](std::string n) {
      std::string::size_type p = n.find_last_of('/');
      if (p != std::string::npos) n = n.substr(p + 1);
      p = n.find_first_of("([");
      if (p != std::string::npos) n = n.substr(0, p);
      p = n.find_last_of('.');
      if (p != std::string::npos && n.size() - p <= 4) n = n.substr(0, p);
      std::string k;
      for (char c : n) if (isalnum((unsigned char)c)) k += (char)tolower((unsigned char)c);
      return k;
    };
    std::string game(cdpath), dir = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.yabasanshiro/cheats/";
    std::string gk = key(game), best;
    int bestscore = -100;
    const char *regions[] = { "Europe", "USA", "Japan" };
    if (DIR *d = opendir(dir.c_str())) {
      while (struct dirent *e = readdir(d)) {
        std::string f(e->d_name);
        if (f.size() < 5 || f.compare(f.size() - 4, 4, ".cht") != 0 || key(f) != gk) continue;
        int s = 0;
        for (const char *r : regions)
          if (game.find(r) != std::string::npos && f.find(r) != std::string::npos) s += 2;
        if (f.find("(RetroArch)") != std::string::npos) s -= 3;
        if (s > bestscore) { bestscore = s; best = f; }
      }
      closedir(d);
    }
    if (!best.empty()) {
      int count = 0, last = -2;
      CheatLoadCht((dir + best).c_str());
      cheatlist_struct *list = CheatGetList(&count);
      for (int i = 0; list != NULL && i < count; i++) {
        int g = list[i].group;
        if (g < 0 || g == last) continue;
        last = g;
        CheatEnableGroup(g, pre.getBool("cheat " + std::to_string(g), list[i].enable != 0) ? 1 : 0);
      }
    }
  }
  {
    /* FPS overlay: "Show Fps" (fallback "show fps") from the game config or
       default.config, off otherwise. */
    int c_fa = pre.getIntGlobal("Show Fps", -1);
    if (c_fa < 0) c_fa = pre.getIntGlobal("show fps", 0);
    g_EnagleFPS = (c_fa != 0);
    /* System language from the game config or default.config, English otherwise. */
    int sp = pre.getIntGlobal("system language", 0);
    if (sp < 0 || sp > 5) sp = 0;
    SmpcSetSystemLanguage(sp);
  }
  if( res == -1) {
    return -1;
  }

  /* Per-line CPU sync: only requested here, vdp2.cpp switches it on once the
     game draws, because enabling it during boot breaks some titles. Must come
     after YabauseInit(), which resets the deciline mode. */
  {
    extern int g_perline_requested;
    g_perline_requested = 1;
    YabauseSetDecilineMode(1);
  }

  padmode = inputmng->getCurrentPadMode( 0 );
#if !defined(__PC__)  
  OSDInit(0);
  OSDChangeCore(OSDCORE_NANOVG);
#endif
  LogStart();
  LogChangeOutput(DEBUG_CALLBACK, NULL);
  return 0;
}

int main(int argc, char** argv)
{
  /* Let SDL load the Mali EGL/GLES libraries. libEGL.so.1 is the Mesa build on
     this platform; mixing it with Mali GLES makes the graphics init fail
     without an error message. overwrite=0 keeps an externally set value.
     Must come before SDL_Init(). */
  setenv("SDL_VIDEO_EGL_DRIVER", "libEGL.so",    0);
  setenv("SDL_VIDEO_GL_DRIVER",  "libGLESv2.so", 0);

  inputmng = InputManager::getInstance();
  
  //printf("\033[2J");

  // Inisialize home directory
  std::string home_dir = getenv("HOME");
  home_dir += "/.yabasanshiro/";
  struct stat st = {0};
  if (stat(home_dir.c_str(), &st) == -1) {
    mkdir(home_dir.c_str(), 0700);
  }  
  std::string bckup_dir = home_dir + "backup.bin";
  strcpy( buppath, bckup_dir.c_str() );
  strcpy( s_savepath, home_dir.c_str() );
  g_keymap_filename = home_dir + "keymapv2.json";

  std::string current_exec_name = argv[0]; // Name of the current exec program
  std::vector<std::string> all_args;
  if (argc > 1) {
    all_args.assign(argv + 1, argv + argc);
    if( all_args[0] == "-h" || all_args[0] == "--h" ){
      printf("Usage:\n");
      printf("  -b STRING  --bios STRING                 bios file\n");
      printf("  -i STRING  --iso STRING                  iso/cue file\n");
      printf("  -r NUMBER  --resolution_mode NUMBER      0 .. Native, 1 .. 4x, 2 .. 2x, 3 .. Original\n");
      printf("  -rr NUMBER  --rotate_resolution_mode NUMBER      0 .. Original, 1 .. 2x, 2 .. 720p, 3 .. 1080p, 4 .. Native\n");
      printf("  -a         --keep_aspect_rate\n");
      printf("  -s NUMBER  --scps_sync_per_frame NUMBER\n");
      printf("  -nf         --no_frame_skip              disable frame skip\n");    
      printf("  -v         --version\n");    
      exit(0);
    }
  }



  for( int i=0; i<all_args.size(); i++ ){
    string x = all_args[i];
		if(( x == "-b" || x == "--bios") && (i+1<all_args.size() ) ) {
      g_emulated_bios = 0;
      strncpy(biospath, all_args[i+1].c_str(), 256);
    }
		else if(( x == "-i" || x == "--iso") && (i+1<all_args.size() ) ) {
      strncpy(cdpath, all_args[i+1].c_str(), 256);
    }
		else if(( x == "-r" || x == "--resolution_mode") && (i+1<all_args.size() ) ) {
      g_resolution_mode = std::stoi( all_args[i+1] );
    }
		else if(( x == "-rr" || x == "--rotate_resolution_mode") && (i+1<all_args.size() ) ) {
      g_rotate_resolution_mode = std::stoi( all_args[i+1] );
    }
		else if(( x == "-a" || x == "--keep_aspect_rate") ) {
      g_keep_aspect_rate = 1;
    }
		else if(( x == "-s" || x == "--g_scsp_sync")&& (i+1<all_args.size() ) ) {
      g_scsp_sync = std::stoi( all_args[i+1] );
    }
		else if(( x == "-nf" || x == "--no_frame_skip") ) {
      g_frame_skip = 0;
      g_frame_skip_cli = 0;   /* command line wins over the config */
    }
		else if(( x == "-v" || x == "--version") ) {
      printf("YabaSanshiro version %s(%s)\n",YAB_VERSION, GIT_SHA1 );
      return 0;
    }
	}
  /* -b given but the file is missing: search for a BIOS image instead. */
  if (!g_emulated_bios) {
    FILE *bt_ = fopen(biospath, "rb");
    if (bt_ != NULL) fclose(bt_);
    else {
      char found_[256];
      if (BiosFind(cdpath, found_, sizeof(found_))) {
        printf("BIOS %s not found, using %s\n", biospath, found_);
        snprintf(biospath, sizeof(biospath), "%s", found_);
      } else {
        printf("BIOS %s not found and no other BIOS image available, using HLE BIOS\n", biospath);
        g_emulated_bios = 1;
      }
    }
  }

  
  /* .m3u playlist (multi-disc games): load the first disc. "Next Disc" in
     the menu switches to the following ones. */
  {
    std::string path_str(cdpath);
    std::string lower_name = path_str;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    if (lower_name.size() > 4 && lower_name.compare(lower_name.size() - 4, 4, ".m3u") == 0) {
      FILE *fm = fopen(cdpath, "r");
      if (fm != NULL) {
        char line_buf[512];
        std::string first_name;
        for (;;) {
          if (fgets(line_buf, sizeof(line_buf), fm) == NULL) break;
          std::string z(line_buf);
          if (z.size() >= 3 && (unsigned char)z[0] == 0xEF && (unsigned char)z[1] == 0xBB && (unsigned char)z[2] == 0xBF) z = z.substr(3);
          size_t e0 = z.find_last_not_of(" \t\r\n");
          if (e0 == std::string::npos) continue;
          z = z.substr(0, e0 + 1);
          size_t a0 = z.find_first_not_of(" \t");
          z = z.substr(a0);
          if (z[0] == '#') continue;
          first_name = z;
          break;
        }
        fclose(fm);
        if (!first_name.empty()) {
          std::string goal = first_name;
          if (goal[0] != '/') {
            size_t sl = path_str.find_last_of('/');
            goal = (sl == std::string::npos ? std::string("") : path_str.substr(0, sl + 1)) + first_name;
          }
          strncpy(cdpath, goal.c_str(), sizeof(cdpath) - 1);
          cdpath[sizeof(cdpath) - 1] = 0;
        } else {
          fprintf(stderr, "%s contains no disc\n", cdpath);
        }
      } else {
        fprintf(stderr, "cannot read %s\n", cdpath);
      }
    }
  }

  // Init Preference
  Preference * p = new Preference(cdpath);
  p->getInt("Resolution",g_resolution_mode);
  p->getInt("Rotate screen resolution",g_rotate_resolution_mode);
  p->getInt("Aspect rate",g_keep_aspect_rate);
  p->getBool("Rotate screen",false);
  /* Same default as in yabauseinit(). */
  p->getBool( "Use compute shader" , true);
  delete p;

  /* RetroAchievements login (background, no output) */
  RA_Init();


  if( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0 ) {
    printf("Fail to init SDL Bye! (%s)", SDL_GetError() );
    return -1;
  }

  SDL_DisplayMode dsp;
  if( SDL_GetCurrentDisplayMode(0,&dsp) != 0 ){
    printf("Fail to SDL_GetCurrentDisplayMode Bye! (%s)", SDL_GetError() );
    return -1;
  }

  SDL_ShowCursor(SDL_DISABLE);
  //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  //SDL_GL_SetSwapInterval(0);
  //SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  //SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  //SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  //SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	//SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	//SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

		SDL_GL_SetAttribute(SDL_GL_RED_SIZE,           8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,         8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,          8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,        24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,       1);
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

#if defined(__PC__)
  int width = 1280;
  int height = 720;
  wnd = SDL_CreateWindow("Yaba Snashiro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN );
#else
  int width = dsp.w;
  int height = dsp.h;
  wnd = SDL_CreateWindow("Yaba Snashiro", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
  if(wnd == nullptr ) {
    printf("Fail to SDL_CreateWindow Bye! (%s)", SDL_GetError() );
    return -1;
  }
#endif
  dsp.refresh_rate = 60;
  SDL_SetWindowDisplayMode(wnd,&dsp);
  SDL_GetWindowSize(wnd,&width,&height);
  SDL_SetWindowInputFocus(wnd);
  glc = SDL_GL_CreateContext(wnd);
  if(glc == nullptr ) {
    printf("Fail to SDL_GL_CreateContext Bye! (%s)", SDL_GetError() );
    return -1;
  }


  inputmng->init(g_keymap_filename);
  menu = new MenuScreen(wnd,width,height, g_keymap_filename, cdpath);
  menu->setConfigFile(g_keymap_filename);  
  menu->setCurrentGamePath(cdpath);

  if( yabauseinit() == -1 ) {
      printf("Fail to yabauseinit Bye! (%s)", SDL_GetError() );
      return -1;
  }

  /* Identify the disc for RetroAchievements (cdpath is final here). */
  RA_LoadGame();
  if (cheats_on()) RA_CheatsUsed();

  p = new Preference(cdpath);
  VIDCore->Resize(0,0,width,height,1,p->getInt("Aspect rate",0));
  delete p;
  
  SDL_GL_MakeCurrent(wnd,nullptr);
  /* The thread that runs YabauseExec() is not pinned: pinning it to core 2 costs frame rate in titles whose RBG0
     rotation thread runs on a neighbouring core, and the scheduler avoids the collision by itself. */
  YabThreadSetAffinityRaw(0xFu);
  Uint32 evToggleMenu = SDL_RegisterEvents(1);
  inputmng->setToggleMenuEventCode(evToggleMenu);

  Uint32  evResetMenu = SDL_RegisterEvents(1);
  menu->setResetMenuEventCode(evResetMenu);

  Uint32  evPadMenu = SDL_RegisterEvents(1);
  menu->setTogglePadModeMenuEventCode(evPadMenu);

  Uint32  evToggleFps = SDL_RegisterEvents(1);
  menu->setToggleFpsCode(evToggleFps);

  Uint32  evToggleFrameSkip = SDL_RegisterEvents(1);
  menu->setToggleFrameSkip(evToggleFrameSkip);

  Uint32  evUpdateConfig = SDL_RegisterEvents(1);
  menu->setUpdateConfig(evUpdateConfig);

  Uint32  evOpenTray = SDL_RegisterEvents(1);
  menu->setOpenTrayMenuEventCode(evOpenTray);

  Uint32  evCloseTray = SDL_RegisterEvents(1);
  menu->setCloseTrayMenuEventCode(evCloseTray);

  Uint32  evSaveState = SDL_RegisterEvents(1);
  menu->setSaveStateEventCode(evSaveState);

  Uint32  evLoadState = SDL_RegisterEvents(1);
  menu->setLoadStateEventCode(evLoadState);

  std::string tmpfilename = home_dir + "tmp.png";

  //struct sched_param thread_param;
  //thread_param.sched_priority = 15; //sched_get_priority_max(SCHED_FIFO);
  //if ( pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_param) < -1 ) {
  //  LOG("sched_setscheduler");
  //}
  //setpriority( PRIO_PROCESS, 0, -8);
  int frame_cont = 0;
  int event_count = 0;
  while(true) {
    SDL_Event e;
    event_count = 0;
    while(SDL_PollEvent(&e)) {
      event_count++;
      if(e.type == SDL_QUIT){
        /* Only clear and swap when the main thread owns the GL context (menu open).
           Otherwise the render thread holds it and touching EGL from here can crash on exit. */
        if (menu_show) {
          glClearColor(0.0,0.0,0.0,1.0);
          glClear(GL_COLOR_BUFFER_BIT);
          SDL_GL_SwapWindow(wnd);
        }
        RA_Shutdown();
        YabauseDeInit();
        GlCleanShutdown();
        return 0;
      }
      else if( e.type == evUpdateConfig ){
          inputmng->updateConfig();
      }
      else if(e.type == evToggleMenu){
        if( menu_show ){
            Preference * p = new Preference(cdpath);
            VIDCore->Resize(0,0,width,height,1,p->getInt("Aspect rate",0));
            delete p;
          hideMenuScreen();           
        }else{
          menu_show = true;
          ScspMuteAudio(1);

          // Refresh the menu switches from the config of the running game,
          // so the menu shows (and writes back) its current settings.
          if (menu) menu->refreshConfigSwitches();

          VdpRevoke();

          char pngname_base[256];
          snprintf(pngname_base,256,"%s/%s_", s_savepath, cdip->itemnum);
          menu->setCurrentGameId(pngname_base);

          inputmng->setMenuLayer(menu);
          SDL_GL_MakeCurrent(wnd,glc);
          saveScreenshot(tmpfilename.c_str());
          glUseProgram(0);
          glGetError();
          glBindBuffer(GL_ARRAY_BUFFER, 0);
          glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
          glDisableVertexAttribArray(0);
          glDisableVertexAttribArray(1);
          glDisableVertexAttribArray(2);
          glDisable(GL_DEPTH_TEST);
          glDisable(GL_SCISSOR_TEST);
          glDisable(GL_STENCIL_TEST);
          glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   

           SDL_ShowCursor(SDL_ENABLE);
          menu->setBackGroundImage( tmpfilename );
        }
      }

      else if(e.type == evResetMenu){
        YabauseReset();
        RA_OnReset(cheats_on());
        hideMenuScreen(); 
      }

      else if(e.type == evPadMenu ){
        if( padmode == 0 ){
          padmode = 1;
        }else{
          padmode = 0;
        }
        hideMenuScreen();         
      }

      else if(e.type == evToggleFps ){
        if( g_EnagleFPS == 0 ){
          g_EnagleFPS = 1;
        }else{
          g_EnagleFPS = 0;
        }
        hideMenuScreen();         
      }

      else if(e.type == evToggleFrameSkip ){
        if( g_frame_skip == 0 ){
          g_frame_skip = 1;
          EnableAutoFrameSkip();
        }else{
          g_frame_skip = 0;
          DisableAutoFrameSkip();
        }
        hideMenuScreen();         
      }
      else if(e.type == evOpenTray ){
        menu->setCurrentGamePath(cdpath);
        Cs2ForceOpenTray();
        if( !g_emulated_bios ) {
          hideMenuScreen();            
        }
      }
      else if(e.type == evCloseTray ){
        if( e.user.data1 != nullptr ){
          strcpy( cdpath, (const char*)e.user.data1 );
          free(e.user.data1);
        }
        Cs2ForceCloseTray(CDCORE_ISO, cdpath );
        RA_OnDiscChanged();
        hideMenuScreen();     
      }

      else if(e.type == evSaveState ){

        int ret;
        time_t t = time(NULL);
        YUI_LOG("MSG_SAVE_STATE");

        snprintf(last_state_filename, 256, "%s/%s_%d.yss", s_savepath, cdip->itemnum, e.user.code);
        ret = YabSaveState(last_state_filename);
        if( ret == 0 ){
          char pngname[256];
          snprintf(pngname,256,"%s/%s_%d.png", s_savepath, cdip->itemnum, e.user.code);
          fs::copy(tmpfilename, pngname, fs::copy_options::overwrite_existing );
        }
        hideMenuScreen();
      }

      else if(e.type == evLoadState ){
        int rtn;
        YUI_LOG("MSG_LOAD_STATE");

        if (RA_HardcoreActive()) {
          RA_ShowMessage("Load State disabled in Hardcore mode");
        } else {
          // Find latest filename
          sprintf(last_state_filename, "%s/%s_%d.yss", s_savepath, cdip->itemnum, e.user.code);
          rtn = YabLoadState(last_state_filename);
          switch(rtn){
            case 0:
            YUI_LOG("Load State: OK");
            break;
          case -1:
            YUI_LOG("Load State: File Not Found");
            break;
          case -2:
            YUI_LOG("Load State: Bad format");
             break;
          case -3:
            YUI_LOG("Load State: Bad format deep inside");
            break;                    
          default:
            YUI_LOG("Load State: Fail unkown");
            break;                    
          }
        }
        hideMenuScreen();
      }

      inputmng->parseEvent(e);
      if( menu_show ){
        menu->onEvent( e );
      }
    }
    inputmng->handleJoyEvents();

    if( menu_show ){

      RA_Idle();
      if( event_count > 0 ){
        glClearColor(0.0f, 0.0f, 0.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        menu->drawAll();
        SDL_GL_SwapWindow(wnd);
      }else{
        usleep( 16*1000 );
      }
    }else{
      //printf("Frmae = %d \n", 0, 0, frame_cont);
      //frame_cont++;
      YabauseExec(); // exec one frame
      RA_DoFrame();
    }
  }
  RA_Shutdown();
  YabauseDeInit();
  GlCleanShutdown();
  return 0;
}

void hideMenuScreen(){
  /* A second call without an open menu (e.g. Next Disc with a real BIOS
     triggers both evOpenTray and evCloseTray) would draw without a GL
     context and crash. */
  if (!menu_show) return;
  SDL_ShowCursor(SDL_DISABLE);
  menu_show = false;
  inputmng->setMenuLayer(nullptr);
  glClearColor(0.0f, 0.0f, 0.0f, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(wnd);          
  glClear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(wnd);          
  SDL_GL_MakeCurrent(wnd,nullptr);
  VdpResume();
  ScspUnMuteAudio(1); 
}

int saveScreenshot( const char * filename ){
    
    int width;
    int height;
    unsigned char * buf = NULL;
    unsigned char * bufRGB = NULL;
    png_bytep * row_pointers = NULL;
    int quality = 100; // best
    FILE * outfile = NULL;
    int row_stride;
    int glerror;
    int u,v;
    int pmode;
    png_byte color_type;
    png_byte bit_depth; 
    png_structp png_ptr;
    png_infop info_ptr;
    int number_of_passes;
    int rtn = -1;
  
    SDL_GetWindowSize( wnd, &width, &height);
    buf = (unsigned char *)malloc(width*height*4);
    if( buf == NULL ) {
        YUI_LOG("not enough memory\n");
        goto FINISH;
    }

    glReadBuffer(GL_BACK);
    pmode = GL_RGBA;
    glGetError();
    glReadPixels(0, 0, width, height, pmode, GL_UNSIGNED_BYTE, buf);
    if( (glerror = glGetError()) != GL_NO_ERROR ){
        YUI_LOG("glReadPixels %04X\n",glerror);
         goto FINISH;
    }
	
	for( u = 3; u <width*height*4; u+=4 ){
		buf[u]=0xFF;
	}
    row_pointers = (png_byte**)malloc(sizeof(png_bytep) * height);
    for (v=0; v<height; v++)
        row_pointers[v] = (png_byte*)&buf[ (height-1-v) * width * 4];

    // save as png
    if ((outfile = fopen(filename, "wb")) == NULL) {
        YUI_LOG("can't open %s\n", filename);
        goto FINISH;
    }

    /* initialize stuff */
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr){
        YUI_LOG("[write_png_file] png_create_write_struct failed");
        goto FINISH;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr){
        YUI_LOG("[write_png_file] png_create_info_struct failed");
        goto FINISH;
    }

    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during init_io");
        goto FINISH;
    }
    /* write header */
    png_init_io(png_ptr, outfile);
    
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during writing header");
        goto FINISH;
    }
    bit_depth = 8;
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
    png_set_IHDR(png_ptr, info_ptr, width, height,
        bit_depth, color_type, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    //png_set_gAMA(png_ptr, info_ptr, 1.0);
    {
        png_text text[3];
        int txt_fields = 0;
        char desc[256];
        
        time_t      gmt;
        png_time    mod_time;
        
        time(&gmt);
        png_convert_from_time_t(&mod_time, gmt);
        png_set_tIME(png_ptr, info_ptr, &mod_time);
    
        text[txt_fields].key = "Title";
        text[txt_fields].text = Cs2GetCurrentGmaecode();
        text[txt_fields].compression = PNG_TEXT_COMPRESSION_NONE;
        txt_fields++;

        sprintf( desc, "Yaba Sanshiro Version %s\n VENDER: %s\n RENDERER: %s\n VERSION %s\n",YAB_VERSION,glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION));
        text[txt_fields].key ="Description";
        text[txt_fields].text=desc;
        text[txt_fields].compression = PNG_TEXT_COMPRESSION_NONE;
        txt_fields++;
        
        png_set_text(png_ptr, info_ptr, text,txt_fields);
    }       
    png_write_info(png_ptr, info_ptr);


    /* write bytes */
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during writing bytes");
        goto FINISH;
    }
    png_write_image(png_ptr, row_pointers);

    /* end write */
    if (setjmp(png_jmpbuf(png_ptr))){
        YUI_LOG("[write_png_file] Error during end of write");
        goto FINISH;
    }
    
    png_write_end(png_ptr, NULL);
    rtn = 0;
FINISH: 
    if(outfile) fclose(outfile);
    if(buf) free(buf);
    if(bufRGB) free(bufRGB);
    if(row_pointers) free(row_pointers);
    return rtn;
}

extern "C" {

  int YabauseThread_IsUseBios() {
    //if( s_biospath == NULL){
    //    return 1;
    //}
    return 0;

  }

  const char * YabauseThread_getBackupPath() {
    //return s_buppath;
    return NULL;
  }

  void YabauseThread_setUseBios(int use) {


  }

  char tmpbakcup[256];
  void YabauseThread_setBackupPath( const char * buf) {
      //strcpy(tmpbakcup,buf);
      //s_buppath = tmpbakcup;
  }

  void YabauseThread_resetPlaymode() {
      //if( s_playrecord_path != NULL ){
      //    free(s_playrecord_path);
      //    s_playrecord_path = NULL;
      //}
      //s_buppath = GetMemoryPath();
  }

  void YabauseThread_coldBoot() {
    //YabauseDeInit();
    //YabauseInit();
    //YabauseReset();
  }
}
