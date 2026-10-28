#include "Preference.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>

#define LOG(...) do { } while (0)

Preference::Preference( const std::string & filename ){

  std::string input_trace_filename;

  int index = filename.find_last_of("/\\");
  if( index != string::npos ){
    input_trace_filename = filename.substr(index+1);
  }else{
    input_trace_filename = filename;
  }

  /* Strip the ROM file extension, so the config is "<game name>.config"
     regardless of the image format (.chd, .iso, .cue, ...). Only the last
     extension is removed, and only if the dot is not the first character:
     "Cotton 2 (English v0.9).chd" becomes "Cotton 2 (English v0.9)".
     Names without a dot are unchanged. */
  {
    const size_t punkt = input_trace_filename.find_last_of('.');
    if (punkt != std::string::npos && punkt > 0)
      input_trace_filename = input_trace_filename.substr(0, punkt);
  }

  std::string home_dir = getenv("HOME");
  home_dir += "/.yabasanshiro/";

  this->filename = home_dir + input_trace_filename + ".config";

  LOG("Preference: opening %s\n", this->filename.c_str() );

  try{
    std::ifstream fin( this->filename );
    this->existed_before = fin.good();
    fin >> j;
    fin.close();
  }catch ( json::exception& e ){
  }
} 

/* The file is written only if something was actually set (menu). */
Preference::~Preference(){
  /* If no config exists for this game, create one with default values.
     Without a config there is no "state path", and save states would
     silently end up in ~/.yabasanshiro. An existing file is never
     overwritten here. */
  if (!this->existed_before) create_defaults();

  if (!this->changed) return;
  write_changed_only();
}

/* Creates a missing per-game config with default values. The values are
   built in; only `state path` is taken from `~/.yabasanshiro/default.config`
   if it is set there. */
void Preference::create_defaults(){
  /* Another Preference instance may have created the file since this one was
     constructed. Never replace it, or that instance's settings would be lost. */
  {
    std::ifstream existing_file( this->filename );
    if (existing_file.good()) {
      this->existed_before = true;
      return;
    }
  }

  /* Defaults that apply everywhere, independent of the device. */
  json tmpl = json::object();
  tmpl["Aspect rate"]              = 0;
  tmpl["Resolution"]               = 3;
  tmpl["Rotate screen"]            = false;
  tmpl["Rotate screen resolution"] = 0;
  tmpl["force 50hz"]               = false;
  tmpl["texcache persist"]         = true;

  /* "state path" has no built-in default, since the directory may not exist
     on another device. It is taken from the global `~/.yabasanshiro/default.config`
     if present there; otherwise it is omitted and the emulator falls back to
     the user directory. Only this key is copied, because default.config also
     holds menu values that do not belong in a per-game config. */
  {
    const char * hd = getenv("HOME");
    if (hd != NULL) {
      std::string gpath = std::string(hd) + "/.yabasanshiro/default.config";
      try{
        std::ifstream gin( gpath );
        if (gin.good()) {
          json global;
          gin >> global;
          /* No global.contains(): the bundled nlohmann::json predates 3.6 and
             does not provide it. find() is available in every version. */
          json::const_iterator it = global.find("state path");
          if (global.is_object() && it != global.end() && it->is_string()) {
            tmpl["state path"] = *it;
          }
        }
        gin.close();
      }catch ( json::exception& e ){ /* broken global config: continue without it */ }
    }
  }

  std::ofstream out( this->filename );
  if (!out) return;
  out << tmpl.dump(2);
  out.close();
  this->existed_before = true;   /* so that write_changed_only() builds on it */
}

/* Writes only the keys that were set via setInt/setBool during this run.
   The file is re-read from disk, the changed keys are merged in and the file
   is written back, so external edits made while running are not overwritten
   by stale values from `j`. */
void Preference::write_changed_only(){
  if (this->changed_keys.empty()) return;

  /* First write of a new config: create it with the defaults (including
     `state path`) and merge the changed keys into that, instead of writing a
     file that only holds the changed keys. */
  if (!this->existed_before) create_defaults();

  json on_disk;
  try{
    std::ifstream fin( this->filename );
    if (fin) fin >> on_disk;
    fin.close();
  }catch ( json::exception& e ){
    on_disk = json::object();
  }
  if (!on_disk.is_object()) on_disk = json::object();

  for (const auto & k : this->changed_keys) {
    on_disk[k] = j[k];
  }

  std::ofstream out(this->filename);
  out << on_disk.dump(2);
  out.close();
}

int Preference::getInt( const std::string & key , int defaultv = 0  ){
  try {
    LOG("Preference: getInt %s:%d\n", key.c_str() ,j[key].get<int>() );
    return j[key].get<int>();
  }catch ( json::type_error & e ){
    /* A missing setting means "use the default"; it is not written back. */
  }

  LOG("Preference: fail to getInt %s and use defalut value %d\n", key.c_str(), defaultv);
  return defaultv;
}

/* Falls back to the global emulator config (default.config) without creating keys. */
int Preference::getIntGlobal( const std::string & key , int defaultv ){
  try {
    json::iterator it = j.find(key);
    if (it != j.end() && it->is_number()) return it->get<int>();
    if (it != j.end() && it->is_boolean()) return it->get<bool>() ? 1 : 0;   /* menu switches store bool */
  } catch ( json::exception & e ) {}
  try {
    const char * hd = getenv("HOME");
    if (hd != NULL) {
      std::ifstream gin( std::string(hd) + "/.yabasanshiro/default.config" );
      if (gin.good()) {
        json global; gin >> global;
        json::iterator it = global.find(key);
        if (it != global.end() && it->is_number()) return it->get<int>();
        if (it != global.end() && it->is_boolean()) return it->get<bool>() ? 1 : 0;
      }
    }
  } catch ( json::exception & e ) {}
  return defaultv;
}

static std::string GlobalPath(){
  const char * hd = getenv("HOME");
  return std::string(hd != NULL ? hd : "") + "/.yabasanshiro/default.config";
}

int Preference::getIntOnlyGlobal( const std::string & key , int defaultv ){
  try {
    std::ifstream gin( GlobalPath() );
    if (gin.good()) {
      json global; gin >> global;
      json::iterator it = global.find(key);
      if (it != global.end() && it->is_number()) return it->get<int>();
      if (it != global.end() && it->is_boolean()) return it->get<bool>() ? 1 : 0;
    }
  } catch ( json::exception & e ) {}
  return defaultv;
}

void Preference::setIntGlobal( const std::string & key , int value ){
  json global = json::object();
  try {
    std::ifstream gin( GlobalPath() );
    if (gin.good()) gin >> global;
  } catch ( json::exception & e ) { global = json::object(); }
  if (!global.is_object()) global = json::object();
  global[key] = value;
  std::ofstream out( GlobalPath() );
  if (!out) return;
  out << global.dump(2);
}

bool Preference::getBool( const std::string & key , bool defaultv = false ){
  try {
    LOG("Preference: getBool %s:%d\n", key.c_str(),j[key].get<bool>() );
    return j[key].get<bool>();
  }catch ( json::type_error & e ){
    /* Default is not written back, see getInt. */
  }

  LOG("Preference: fail to getBool %s and use defalut value %d\n", key.c_str(), defaultv);
  return defaultv;
}

std::string Preference::getString( const std::string & key , const std::string & defaultv ){
  try {
    std::string v = j[key].get<std::string>();
    LOG("Preference: getString %s:%s\n", key.c_str(), v.c_str() );
    return v;
  }catch ( json::type_error & e ){
    /* Intentionally no setString here, see Preference.h. */
  }
  return defaultv;
}

void Preference::setInt( const std::string & key , int value ){
  j[key] = value;
  this->changed = true;
  this->changed_keys.insert(key);
  write_changed_only();
}

void Preference::setBool( const std::string & key , bool value ){
  j[key] = value;
  this->changed = true;
  this->changed_keys.insert(key);
  write_changed_only();
}
