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

/*  RetroAchievements on top of rcheevos rc_client, see RetroAchievements.h */
#include <algorithm>
#include "RetroAchievements.h"
#include "../config.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "rcheevos/include/rc_client.h"
#include "rcheevos/include/rc_api_request.h"
#include "rcheevos/src/rhash/md5.h"

#include "cs2.h"
#include "cdbase.h"
#include "memory.h"

/* curl constants (no curl headers), values from include/curl/curl.h:
   CURLOPT(name, type, number) = type + number, LONG=0, OBJECTPOINT=10000,
   FUNCTIONPOINT=20000. */
#define RA_CURLOPT_URL             10002    /* STRINGPOINT 2 */
#define RA_CURLOPT_POSTFIELDS      10015    /* OBJECTPOINT 15 */
#define RA_CURLOPT_USERAGENT       10018    /* STRINGPOINT 18 */
#define RA_CURLOPT_HTTPHEADER      10023    /* SLISTPOINT 23 */
#define RA_CURLOPT_WRITEFUNCTION   20011    /* FUNCTIONPOINT 11 */
#define RA_CURLOPT_WRITEDATA       10001    /* CBPOINT 1 */
#define RA_CURLOPT_ACCEPT_ENCODING 10102    /* STRINGPOINT 102 */
#define RA_CURLOPT_TIMEOUT         13       /* LONG 13 */
#define RA_CURLOPT_CONNECTTIMEOUT  78       /* LONG 78 */
#define RA_CURLOPT_FOLLOWLOCATION  52       /* LONG 52 */
#define RA_CURLOPT_NOSIGNAL        99       /* LONG 99 */
#define RA_CURLINFO_RESPONSE_CODE  0x200002 /* CURLINFO_LONG + 2 */
#define RA_CURL_GLOBAL_DEFAULT     3        /* CURL_GLOBAL_ALL = SSL(1)|WIN32(2) */
#define RA_CURLE_OK                0

#define RA_OVERLAY_MS 4000

namespace {

typedef int   (*fn_global_init)(long);
typedef void* (*fn_easy_init)(void);
typedef int   (*fn_easy_setopt)(void*, int, ...);
typedef int   (*fn_easy_perform)(void*);
typedef void  (*fn_easy_cleanup)(void*);
typedef int   (*fn_easy_getinfo)(void*, int, ...);
typedef const char* (*fn_easy_strerror)(int);
typedef void* (*fn_slist_append)(void*, const char*);
typedef void  (*fn_slist_free_all)(void*);

struct CurlApi {
  void* lib;
  fn_global_init global_init;
  fn_easy_init init;
  fn_easy_setopt setopt;
  fn_easy_perform perform;
  fn_easy_cleanup cleanup;
  fn_easy_getinfo getinfo;
  fn_easy_strerror strerror_;
  fn_slist_append slist_append;      /* optional */
  fn_slist_free_all slist_free_all;  /* optional */
  CurlApi() : lib(NULL), global_init(NULL), init(NULL), setopt(NULL),
              perform(NULL), cleanup(NULL), getinfo(NULL), strerror_(NULL),
              slist_append(NULL), slist_free_all(NULL) {}
};

struct Config {
  std::string username, password, token;
  int hardcore;
  int indicators;   /* box with trackers/challenges/progress, top right */
  Config() : hardcore(0), indicators(1) {}
};

/* One request for the worker thread. */
struct Request {
  std::string url, post, content_type;
  bool is_post;
  rc_client_server_callback_t callback;
  void* callback_data;
  Request() : is_post(false), callback(NULL), callback_data(NULL) {}
};

/* One finished request, delivered on the main thread. */
struct Response {
  rc_client_server_callback_t callback;
  void* callback_data;
  std::string body;
  int status;
  Response() : callback(NULL), callback_data(NULL), status(0) {}
};

/* Context of a login request. */
struct LoginCtx {
  bool write_cfg;   /* password login: store user name and token */
  LoginCtx() : write_cfg(false) {}
};

std::mutex g_mutex;          /* protects status, config and overlay */
RAStatus g_status;
Config g_cfg;
std::string g_overlay;
long long g_overlay_until = 0;            /* CLOCK_MONOTONIC ms */
std::atomic<bool> g_overlay_active(false);
/* lasting indicators (g_mutex): leaderboard trackers, challenge and progress indicators */
std::map<uint32_t, std::string> g_trackers;    /* tracker id -> current value */
std::map<uint32_t, std::string> g_challenges;  /* achievement id -> title */
std::string g_progress;                         /* "<title>: <measured progress>" */
long long g_progress_until = 0;

rc_client_t* g_client = NULL;             /* main thread only */
std::string g_hash;                       /* hash of the inserted disc */
bool g_loading = false;                   /* load game request running */
std::atomic<bool> g_logged_in(false);
std::atomic<bool> g_game_loaded(false);
std::atomic<bool> g_hc_pending(false);    /* hardcore selected, waits for reset */
std::atomic<bool> g_cheats(false);        /* a cheat was on since the last reset */
std::string g_user_agent;

const char * const CHEATS_MSG = "Cheats activated -> RetroAchievements disabled until reset";

CurlApi g_curl;
bool g_curl_tried = false;   /* only touched by the main thread, before the worker starts */

/* worker thread and queues */
std::thread g_worker;
bool g_worker_started = false;
std::mutex g_req_mutex;
std::condition_variable g_req_cv;
std::deque<Request> g_req_queue;
bool g_stop = false;
std::mutex g_resp_mutex;
std::deque<Response> g_resp_queue;
std::atomic<int> g_resp_pending(0);

std::string cfg_path() {
  const char* h = getenv("HOME");
  std::string home = (h && h[0]) ? h : "/home/ark";
  return home + "/.yabasanshiro/retroachievements.cfg";
}

std::string trim(const std::string & s) {
  size_t a = 0, b = s.size();
  for (; a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'); a++) {}
  for (; b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n'); b--) {}
  return s.substr(a, b - a);
}

Config read_config() {
  Config c;
  std::ifstream in(cfg_path().c_str());
  std::string line;
  while (std::getline(in, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    if (k == "username") c.username = v;
    else if (k == "password") c.password = v;
    else if (k == "token") c.token = v;
    else if (k == "hardcore") c.hardcore = (v == "1") ? 1 : 0;
    else if (k == "indicators") c.indicators = (v == "0") ? 0 : 1;
  }
  return c;
}

/* Write the file via a temporary file with mode 0600, then rename. */
bool write_config(const Config & c) {
  std::string path = cfg_path();
  size_t sl = path.find_last_of('/');
  if (sl != std::string::npos) mkdir(path.substr(0, sl).c_str(), 0755);
  std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  fchmod(fd, 0600);
  std::string s;
  if (!c.username.empty()) s += "username=" + c.username + "\n";
  if (!c.password.empty()) s += "password=" + c.password + "\n";
  if (!c.token.empty()) s += "token=" + c.token + "\n";
  s += std::string("hardcore=") + (c.hardcore ? "1" : "0") + "\n";
  s += std::string("indicators=") + (c.indicators ? "1" : "0") + "\n";
  size_t off = 0;
  bool ok = true;
  while (off < s.size()) {
    ssize_t n = write(fd, s.data() + off, s.size() - off);
    if (n <= 0) { ok = false; break; }
    off += (size_t)n;
  }
  close(fd);
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  chmod(path.c_str(), 0600);
  return true;
}

long long now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void set_overlay(const std::string & text) {
  std::lock_guard<std::mutex> l(g_mutex);
  g_overlay = text;
  g_overlay_until = now_ms() + RA_OVERLAY_MS;
  g_overlay_active = true;
}

void clear_indicators() {
  std::lock_guard<std::mutex> l(g_mutex);
  g_trackers.clear();
  g_challenges.clear();
  g_progress.clear();
}

bool load_curl() {
  if (g_curl.lib) return true;
  if (g_curl_tried) return false;
  g_curl_tried = true;
  void* lib = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return false;
  CurlApi a;
  a.lib = lib;
  a.global_init = (fn_global_init)dlsym(lib, "curl_global_init");
  a.init = (fn_easy_init)dlsym(lib, "curl_easy_init");
  a.setopt = (fn_easy_setopt)dlsym(lib, "curl_easy_setopt");
  a.perform = (fn_easy_perform)dlsym(lib, "curl_easy_perform");
  a.cleanup = (fn_easy_cleanup)dlsym(lib, "curl_easy_cleanup");
  a.getinfo = (fn_easy_getinfo)dlsym(lib, "curl_easy_getinfo");
  a.strerror_ = (fn_easy_strerror)dlsym(lib, "curl_easy_strerror");
  a.slist_append = (fn_slist_append)dlsym(lib, "curl_slist_append");
  a.slist_free_all = (fn_slist_free_all)dlsym(lib, "curl_slist_free_all");
  if (!a.global_init || !a.init || !a.setopt || !a.perform || !a.cleanup ||
      !a.getinfo || !a.strerror_) {
    dlclose(lib);
    return false;
  }
  a.global_init(RA_CURL_GLOBAL_DEFAULT);
  g_curl = a;
  return true;
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
  std::string* s = (std::string*)ud;
  size_t n = size * nmemb;
  if (s->size() + n > (16u << 20)) return 0;  /* cap at 16 MB */
  s->append(ptr, n);
  return n;
}

/* Worker thread only: run one request. Returns the HTTP status, or the
   rc_client retryable client error code if there was no HTTP answer. */
int http_perform(const Request & rq, std::string & out) {
  void* h = g_curl.init();
  if (!h) {
    out = "curl init failed";
    return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
  }
  void* hdr = NULL;
  g_curl.setopt(h, RA_CURLOPT_URL, rq.url.c_str());
  if (rq.is_post) {
    g_curl.setopt(h, RA_CURLOPT_POSTFIELDS, rq.post.c_str());
    if (!rq.content_type.empty() && rq.content_type != "application/x-www-form-urlencoded" &&
        g_curl.slist_append && g_curl.slist_free_all) {
      hdr = g_curl.slist_append(NULL, ("Content-Type: " + rq.content_type).c_str());
      if (hdr) g_curl.setopt(h, RA_CURLOPT_HTTPHEADER, hdr);
    }
  }
  g_curl.setopt(h, RA_CURLOPT_USERAGENT, g_user_agent.c_str());
  g_curl.setopt(h, RA_CURLOPT_ACCEPT_ENCODING, "");
  g_curl.setopt(h, RA_CURLOPT_WRITEFUNCTION, &write_cb);
  g_curl.setopt(h, RA_CURLOPT_WRITEDATA, &out);
  g_curl.setopt(h, RA_CURLOPT_TIMEOUT, (long)20);
  g_curl.setopt(h, RA_CURLOPT_CONNECTTIMEOUT, (long)10);
  g_curl.setopt(h, RA_CURLOPT_FOLLOWLOCATION, (long)1);
  g_curl.setopt(h, RA_CURLOPT_NOSIGNAL, (long)1);
  int rc = g_curl.perform(h);
  long code = 0;
  int status;
  if (rc == RA_CURLE_OK) {
    g_curl.getinfo(h, RA_CURLINFO_RESPONSE_CODE, &code);
    status = (int)code;
  } else {
    out = g_curl.strerror_(rc);
    status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
  }
  g_curl.cleanup(h);
  if (hdr) g_curl.slist_free_all(hdr);
  return status;
}

void worker_main() {
  for (;;) {
    Request rq;
    {
      std::unique_lock<std::mutex> l(g_req_mutex);
      g_req_cv.wait(l, [] { return g_stop || !g_req_queue.empty(); });
      if (g_stop) return;
      rq = g_req_queue.front();
      g_req_queue.pop_front();
    }
    Response rs;
    rs.callback = rq.callback;
    rs.callback_data = rq.callback_data;
    rs.status = http_perform(rq, rs.body);
    if (!rq.post.empty()) rq.post.assign(rq.post.size(), ' ');  /* may hold a password */
    {
      std::lock_guard<std::mutex> l(g_resp_mutex);
      g_resp_queue.push_back(rs);
      g_resp_pending++;
    }
  }
}

/* rc_client server call: hand the request to the worker. */
void server_call(const rc_api_request_t* request, rc_client_server_callback_t callback,
                 void* callback_data, rc_client_t*) {
  Request rq;
  rq.url = request->url ? request->url : "";
  rq.is_post = (request->post_data != NULL);
  if (request->post_data) rq.post = request->post_data;
  if (request->content_type) rq.content_type = request->content_type;
  rq.callback = callback;
  rq.callback_data = callback_data;
  {
    std::lock_guard<std::mutex> l(g_req_mutex);
    g_req_queue.push_back(rq);
  }
  g_req_cv.notify_one();
}

/* Main thread: deliver the finished requests to rc_client. */
void drain_responses() {
  if (g_resp_pending.load() == 0) return;
  std::deque<Response> q;
  {
    std::lock_guard<std::mutex> l(g_resp_mutex);
    q.swap(g_resp_queue);
    g_resp_pending = 0;
  }
  for (size_t i = 0; i < q.size(); i++) {
    rc_api_server_response_t r;
    r.body = q[i].body.c_str();
    r.body_length = q[i].body.size();
    r.http_status_code = q[i].status;
    if (q[i].callback) q[i].callback(&r, q[i].callback_data);
  }
}

uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t*) {
  /* RetroAchievements Saturn map: $000000 Work RAM Low, $100000 Work RAM High,
     1 MB each, raw host byte order like the reference cores. */
  uint32_t i = 0;
  if (!LowWram || !HighWram) return 0;
  for (; i < num_bytes; i++, address++) {
    if (address < 0x100000) buffer[i] = LowWram[address];
    else if (address < 0x200000) buffer[i] = HighWram[address - 0x100000];
    else break;
  }
  return i;
}

/* Refresh the cached game part of the status (main thread). */
void update_game_status() {
  bool loaded = g_client && rc_client_is_game_loaded(g_client);
  if (loaded) {
    const rc_client_game_t* gi = rc_client_get_game_info(g_client);
    rc_client_user_game_summary_t sum;
    rc_client_get_user_game_summary(g_client, &sum);
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.game_loaded = true;
    g_status.game_title = (gi && gi->title) ? gi->title : "";
    g_status.ach_unlocked = sum.num_unlocked_achievements;
    g_status.ach_total = sum.num_core_achievements;
  } else {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.game_loaded = false;
    g_status.game_title.clear();
    g_status.ach_unlocked = g_status.ach_total = 0;
  }
  g_game_loaded = loaded;
}

void event_handler(const rc_client_event_t* ev, rc_client_t*) {
  switch (ev->type) {
  case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    if (ev->achievement) {
      char b[256];
      snprintf(b, sizeof(b), "Achievement unlocked: %s (%u)",
               ev->achievement->title ? ev->achievement->title : "?",
               (unsigned)ev->achievement->points);
      set_overlay(b);
      update_game_status();
    }
    break;
  case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
  case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
    if (ev->leaderboard) {
      char b[256];
      snprintf(b, sizeof(b), "Leaderboard attempt %s: %s",
               ev->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED ? "started" : "failed",
               ev->leaderboard->title ? ev->leaderboard->title : "?");
      set_overlay(b);
    }
    break;
  case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
    if (ev->leaderboard) {
      char b[256];
      snprintf(b, sizeof(b), "Leaderboard submitted: %s %s",
               ev->leaderboard->title ? ev->leaderboard->title : "?",
               ev->leaderboard->tracker_value ? ev->leaderboard->tracker_value : "");
      set_overlay(b);
    }
    break;
  case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
  case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
    if (ev->leaderboard_tracker) {
      std::lock_guard<std::mutex> l(g_mutex);
      g_trackers[ev->leaderboard_tracker->id] = ev->leaderboard_tracker->display;
    }
    break;
  case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
    if (ev->leaderboard_tracker) {
      std::lock_guard<std::mutex> l(g_mutex);
      g_trackers.erase(ev->leaderboard_tracker->id);
    }
    break;
  case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
    if (ev->achievement) {
      std::lock_guard<std::mutex> l(g_mutex);
      g_challenges[ev->achievement->id] = ev->achievement->title ? ev->achievement->title : "?";
    }
    break;
  case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
    if (ev->achievement) {
      std::lock_guard<std::mutex> l(g_mutex);
      g_challenges.erase(ev->achievement->id);
    }
    break;
  case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
  case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
    if (ev->achievement) {
      std::lock_guard<std::mutex> l(g_mutex);
      g_progress = std::string(ev->achievement->title ? ev->achievement->title : "?") + ": " +
                   ev->achievement->measured_progress;
      g_progress_until = now_ms() + RA_OVERLAY_MS;
    }
    break;
  case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE: {
    std::lock_guard<std::mutex> l(g_mutex);
    g_progress.clear();
    break;
  }
  case RC_CLIENT_EVENT_GAME_COMPLETED: {
    RAStatus st;
    RA_GetStatus(st);
    std::string m = std::string(rc_client_get_hardcore_enabled(g_client) ? "Mastered: " : "Completed: ") + st.game_title;
    set_overlay(m);
    break;
  }
  case RC_CLIENT_EVENT_SERVER_ERROR:
    if (ev->server_error) {
      char b[256];
      snprintf(b, sizeof(b), "RetroAchievements error: %s",
               ev->server_error->error_message ? ev->server_error->error_message : "?");
      set_overlay(b);
    }
    break;
  case RC_CLIENT_EVENT_DISCONNECTED:
    set_overlay("RetroAchievements: connection lost, unlocks pending");
    break;
  case RC_CLIENT_EVENT_RECONNECTED:
    set_overlay("RetroAchievements: connection restored");
    break;
  default:
    /* RESET: raised when hardcore is enabled with a game loaded; that only happens in RA_OnReset,
       which runs after the emulator reset. SCOREBOARD/SUBSET_COMPLETED: not shown. */
    break;
  }
}

void load_game_cb(int result, const char* error_message, rc_client_t*, void*) {
  g_loading = false;
  update_game_status();
  if (result != RC_OK) {
    return;
  }
  RAStatus st;
  RA_GetStatus(st);
  char b[256];
  /* the mode must be visible when the game starts */
  if (g_cheats.load())
    snprintf(b, sizeof(b), "%s: cheats activated -> RetroAchievements disabled", st.game_title.c_str());
  else
    snprintf(b, sizeof(b), "%s: %u/%u achievements - %s mode", st.game_title.c_str(), st.ach_unlocked, st.ach_total,
             rc_client_get_hardcore_enabled(g_client) ? "Hardcore" : "Casual");
  set_overlay(b);
}

void start_game_load() {
  if (!g_client || g_hash.empty() || g_loading || rc_client_is_game_loaded(g_client)) return;
  g_loading = true;
  clear_indicators();
  rc_client_begin_load_game(g_client, g_hash.c_str(), load_game_cb, NULL);
}

void login_cb(int result, const char* error_message, rc_client_t* client, void* userdata) {
  LoginCtx* ctx = (LoginCtx*)userdata;
  bool write_cfg = ctx && ctx->write_cfg;
  delete ctx;
  if (result != RC_OK) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.state = RA_ERROR;
    g_status.error = error_message ? error_message : "Login failed";
    g_status.name.clear();
    g_status.score = 0;
    g_logged_in = false;
    return;
  }
  const rc_client_user_t* u = rc_client_get_user_info(client);
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.state = RA_LOGGED_IN;
    g_status.error.clear();
    if (u) {
      g_status.name = u->display_name ? u->display_name : (u->username ? u->username : "");
      g_status.score = u->score ? u->score : u->score_softcore;
      if (write_cfg && u->username && u->token) {
        /* keep the token, drop the password */
        g_cfg.username = u->username;
        g_cfg.token = u->token;
        g_cfg.password.clear();
        write_config(g_cfg);
      }
    }
    g_logged_in = true;
  }
  start_game_load();
}

void begin_password_login(const std::string & user, const std::string & pass) {
  LoginCtx* ctx = new LoginCtx();
  ctx->write_cfg = true;
  rc_client_begin_login_with_password(g_client, user.c_str(), pass.c_str(), login_cb, ctx);
}

} // namespace

void RA_Init() {
  Config c = read_config();
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_cfg = c;
    g_status = RAStatus();
  }
  if (g_client) return;
  g_client = rc_client_create(read_memory, server_call);
  if (!g_client) return;
  rc_client_set_event_handler(g_client, event_handler);
  rc_client_set_hardcore_enabled(g_client, c.hardcore);

  g_user_agent = std::string("YabaSanshiroPirry/") + YAB_VERSION + " (Linux)";
  char clause[128];
  clause[0] = 0;
  if (rc_client_get_user_agent_clause(g_client, clause, sizeof(clause)) > 0 && clause[0])
    g_user_agent += std::string(" ") + clause;

  bool have_cred = !c.username.empty() && (!c.token.empty() || !c.password.empty());
  if (!have_cred) return;
  if (!load_curl()) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.state = RA_NOT_AVAILABLE;
    return;
  }
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.state = RA_LOGGING_IN;
  }
  g_worker = std::thread(worker_main);
  g_worker_started = true;
  if (!c.password.empty()) {
    /* a hand-made file: the token is stored and the password dropped after success */
    begin_password_login(c.username, c.password);
  } else {
    rc_client_begin_login_with_token(g_client, c.username.c_str(), c.token.c_str(), login_cb, new LoginCtx());
  }
}

void RA_LoginWithPassword(const std::string & user, const std::string & pass) {
  if (!g_client || user.empty() || pass.empty()) return;
  if (!load_curl()) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_status.state = RA_NOT_AVAILABLE;
    return;
  }
  if (!g_worker_started) {
    g_worker = std::thread(worker_main);
    g_worker_started = true;
  }
  {
    std::lock_guard<std::mutex> l(g_mutex);
    if (g_status.state == RA_LOGGING_IN) {
      g_status.error = "Login already running";
      g_status.state = RA_ERROR;
      return;
    }
    g_status.state = RA_LOGGING_IN;
    g_status.error.clear();
  }
  begin_password_login(user, pass);
}

void RA_Logout() {
  if (g_client) rc_client_logout(g_client);
  g_loading = false;
  g_logged_in = false;
  g_game_loaded = false;
  g_hc_pending = false;
  std::lock_guard<std::mutex> l(g_mutex);
  g_cfg.token.clear();
  g_cfg.password.clear();   /* username and hardcore stay */
  write_config(g_cfg);
  g_status = RAStatus();
  g_overlay_active = false;
}

void RA_GetStatus(RAStatus & st) {
  bool eff = RA_HardcoreActive();
  std::lock_guard<std::mutex> l(g_mutex);
  st = g_status;
  st.hardcore = g_cfg.hardcore != 0;
  st.indicators = g_cfg.indicators != 0;
  st.hardcore_active = eff;
  st.cheats = g_cheats.load();
  if (st.state == RA_NOT_AVAILABLE) st.error = "libcurl not available";
}

/* RetroAchievements Saturn hash: MD5 of the first 512 bytes of sector 0 of the first track. */
static bool disc_hash(std::string & out) {
  if (!Cs2Area || !Cs2Area->cdi || !Cs2Area->cdi->ReadSectorFAD) return false;
  static unsigned char sec[2448];
  memset(sec, 0, sizeof(sec));
  if (Cs2Area->cdi->ReadSectorFAD(150, sec) <= 0) return false;
  /* Mode 1: user data starts after 12 bytes sync + 4 bytes header */
  const unsigned char* ud = sec + 16;
  if (memcmp(ud, "SEGA SEGASATURN ", 16) != 0) return false;

  md5_state_t ms;
  md5_byte_t dg[16];
  md5_init(&ms);
  md5_append(&ms, ud, 512);
  md5_finish(&ms, dg);
  char hex[33];
  for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02x", dg[i]);
  out = hex;
  return true;
}

void RA_LoadGame() {
  if (!g_client) return;
  std::string hex;
  if (!disc_hash(hex)) return;

  if (g_hash != hex && rc_client_is_game_loaded(g_client)) {
    rc_client_unload_game(g_client);
    update_game_status();
  }
  g_hash = hex;
  if (g_logged_in.load()) start_game_load();   /* else the login callback does it */
}

void change_media_cb(int, const char*, rc_client_t*, void*) {
  update_game_status();
}

/* After a disc swap: tell the server about the new disc of the running game instead of unloading it.
   An unknown disc disables hardcore (rcheevos does that itself). */
void RA_OnDiscChanged() {
  if (!g_client) return;
  if (!rc_client_is_game_loaded(g_client)) { RA_LoadGame(); return; }
  std::string hex;
  if (!disc_hash(hex) || hex == g_hash) return;
  g_hash = hex;
  rc_client_begin_change_media(g_client, hex.c_str(), change_media_cb, NULL);
}

bool RA_GetAchievements(std::vector<RAAchievement> & out) {
  out.clear();
  if (!g_client || !rc_client_is_game_loaded(g_client)) return false;
  rc_client_achievement_list_t* list = rc_client_create_achievement_list(g_client,
      RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (!list) return false;
  for (uint32_t b = 0; b < list->num_buckets; b++) {
    const rc_client_achievement_bucket_t & bk = list->buckets[b];
    for (uint32_t i = 0; i < bk.num_achievements; i++) {
      const rc_client_achievement_t* a = bk.achievements[i];
      RAAchievement r;
      r.title = a->title ? a->title : "";
      r.description = a->description ? a->description : "";
      r.points = a->points;
      r.unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
      r.progress = a->measured_progress;
      out.push_back(r);
    }
  }
  rc_client_destroy_achievement_list(list);
  /* rcheevos lists the unlocked bucket last (rc_client.c subset_bucket_order): show unlocked ones first */
  std::stable_partition(out.begin(), out.end(), [](const RAAchievement & r){ return r.unlocked; });
  return true;
}

void RA_DoFrame() {
  if (!g_client) return;
  drain_responses();
  if (!g_game_loaded.load()) return;
  /* with cheats nothing is evaluated, so nothing can unlock; idle keeps the session alive */
  if (g_cheats.load()) rc_client_idle(g_client);
  else rc_client_do_frame(g_client);
}

void RA_Idle() {
  if (!g_client) return;
  drain_responses();
  rc_client_idle(g_client);
}

void RA_OnReset(bool cheats_on) {
  /* the reset ends a cheat session only if all cheats are off now */
  g_cheats = cheats_on;
  if (!g_client) return;
  bool to_hardcore = g_hc_pending.exchange(false);
  if (to_hardcore) rc_client_set_hardcore_enabled(g_client, 1);
  if (g_game_loaded.load()) rc_client_reset(g_client);
  /* the mode must be visible after the switch as well, not only at game start */
  if (cheats_on && g_game_loaded.load()) set_overlay(CHEATS_MSG);
  else if (to_hardcore && g_game_loaded.load()) set_overlay("RetroAchievements: Hardcore mode");
  update_game_status();
}

void RA_CheatsUsed() {
  if (g_cheats.exchange(true)) return;
  if (g_game_loaded.load()) set_overlay(CHEATS_MSG);
}

bool RA_HardcorePending() {
  return g_hc_pending.load();
}

bool RA_HardcoreActive() {
  return g_client && g_logged_in.load() && g_game_loaded.load() &&
         rc_client_get_hardcore_enabled(g_client) != 0;
}

void RA_SetHardcore(bool on) {
  {
    std::lock_guard<std::mutex> l(g_mutex);
    g_cfg.hardcore = on ? 1 : 0;
    write_config(g_cfg);
  }
  if (!g_client) return;
  if (!on) {
    g_hc_pending = false;
    rc_client_set_hardcore_enabled(g_client, 0);
  } else if (g_game_loaded.load()) {
    g_hc_pending = true;     /* applied at the next reset */
  } else {
    rc_client_set_hardcore_enabled(g_client, 1);
  }
}

void RA_SetIndicators(bool on) {
  std::lock_guard<std::mutex> l(g_mutex);
  g_cfg.indicators = on ? 1 : 0;
  write_config(g_cfg);
}

void RA_ShowMessage(const char * text) {
  if (text && text[0]) set_overlay(text);
}

void RA_Shutdown() {
  {
    std::lock_guard<std::mutex> l(g_req_mutex);
    g_stop = true;
  }
  g_req_cv.notify_all();
  if (g_worker.joinable()) g_worker.join();   /* a running request has timeouts, ends in <= 30 s */
  g_worker_started = false;
  if (g_client) {
    rc_client_destroy(g_client);
    g_client = NULL;
  }
  g_game_loaded = false;
  g_logged_in = false;
  g_overlay_active = false;
  clear_indicators();
  std::lock_guard<std::mutex> l(g_resp_mutex);
  g_resp_queue.clear();
  g_resp_pending = 0;
}

extern "C" int RA_GetOverlayText(char *out, int maxlen) {
  if (!g_overlay_active.load() || !out || maxlen <= 0) return 0;
  std::lock_guard<std::mutex> l(g_mutex);
  if (now_ms() >= g_overlay_until) {
    g_overlay_active = false;
    return 0;
  }
  strncpy(out, g_overlay.c_str(), (size_t)maxlen - 1);
  out[maxlen - 1] = 0;
  return 1;
}

extern "C" int RA_GetIndicatorText(char *out, int maxlen) {
  if (!out || maxlen <= 0) return 0;
  std::lock_guard<std::mutex> l(g_mutex);
  if (!g_cfg.indicators) return 0;   /* switched off in the RetroAchievements menu */
  std::string s;
  for (const auto & tr : g_trackers) s += tr.second + "\n";
  for (const auto & ch : g_challenges) s += "Challenge: " + ch.second + "\n";
  if (!g_progress.empty()) {
    if (now_ms() < g_progress_until) s += g_progress + "\n";
    else g_progress.clear();
  }
  if (s.empty()) return 0;
  s.pop_back();
  strncpy(out, s.c_str(), (size_t)maxlen - 1);
  out[maxlen - 1] = 0;
  return 1;
}
