# yabasanshiro-pirry — source code

Source code of **yabasanshiro-pirry**, a heavily optimized build of
**Yaba Sanshiro** (Sega Saturn emulator) for R36S-class handhelds
(RK3326: Cortex-A35 CPU, Mali-G31 GPU).

**Ready-to-use builds, installation guide and the list of tested games:**
👉 **https://github.com/pirrypirrypirry/yabasanshiro-pirry-release**

If you just want to play, you don't need anything from this repository —
grab the binary from the release page above.

## What this is

This tree is based on the `pi4` branch of
[devmiyax/yabause](https://github.com/devmiyax/yabause) (Yaba Sanshiro 2,
`retro_arena` port, commit `f6f41dd6`). On top of it come the changes of
yabasanshiro-pirry: speed work in the SH-2 dynarec and the video code, fixes
for games that froze, crashed or didn't boot, graphics and sound fixes,
multi-disc support via `.m3u`, and the extended in-game menu.

Every fix applies to all games — there are no per-game hacks and no hidden
runtime switches. What you build here is exactly the released emulator.

Current version: **1.6.2**

## Building

The emulator is built as a native **aarch64 Linux** program. The release
binaries are built on **Debian 13 (trixie), arm64** with GCC 14 and CMake 3.31.
The resulting binary needs **glibc 2.38 or newer** on the handheld (dArkOS).

### 1. Install the build dependencies

```bash
sudo apt install build-essential cmake git pkg-config \
    libsdl2-dev libgles-dev libegl-dev libgbm-dev libdrm-dev libudev-dev \
    libasound2-dev libopenal-dev libpng-dev zlib1g-dev
```

### 2. Get the source

```bash
git clone --recursive https://github.com/pirrypirrypirry/yabasanshiro-pirry.git
cd yabasanshiro-pirry
```

`--recursive` is required — it fetches the Eigen submodule used by the menu.
If you already cloned without it: `git submodule update --init --recursive`.

During the build CMake also downloads `libchdr`, `zlib` and `nlohmann/json`
from GitHub and `libpng` from SourceForge, so the build machine needs internet
access.

### 3. Configure

```bash
mkdir build && cd build
cmake ../yabause \
  -DCMAKE_BUILD_TYPE=Release \
  -DYAB_PORTS=retro_arena \
  -DYAB_WANT_DYNAREC_DEVMIYAX=ON \
  -DYAB_WANT_VULKAN=OFF \
  -DUSE_EGL=ON \
  -DCMAKE_C_FLAGS="-O3 -mcpu=cortex-a35 -fno-strict-aliasing -D_GNU_SOURCE -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion" \
  -DCMAKE_CXX_FLAGS="-O3 -mcpu=cortex-a35 -fno-strict-aliasing -D_GNU_SOURCE" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition"
```

Every option matters:

| Option | Why |
|---|---|
| `-DUSE_EGL=ON` | Without it the shaders are built for desktop OpenGL and the emulator fails with *"Cannot initialize shader"* on the handheld |
| `-DYAB_WANT_DYNAREC_DEVMIYAX=ON` | The SH-2 dynarec — most of the speed comes from it |
| `-mcpu=cortex-a35` | Tunes the code for the RK3326 |
| `-Wno-…` flags | Parts of the old C code don't build with GCC 14 otherwise |
| `--allow-multiple-definition` | `nvgCreateGLES3` is defined twice in the bundled nanovg/nanogui code |

### 4. Build

```bash
make -j$(nproc)
strip -o yabasanshiro-pirry src/retro_arena/yabasanshiro
```

The finished emulator is `build/yabasanshiro-pirry`. Install it as
described on the [release page](https://github.com/pirrypirrypirry/yabasanshiro-pirry-release#installation).

### Optional: build exactly like the release (LTO)

The release binary additionally uses link-time optimization. Add `-flto=4 -fno-fat-lto-objects`
to both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`, and pass:

```bash
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-multiple-definition -flto=4 -O3 -mcpu=cortex-a35" \
  -DCMAKE_AR=/usr/bin/gcc-ar -DCMAKE_RANLIB=/usr/bin/gcc-ranlib
```

### Building on an x86 PC (arm64 chroot)

No arm64 machine at hand? An arm64 Debian chroot with QEMU user emulation
works fine — it's how the releases are built. Expect a full build to take
around 15 minutes.

```bash
sudo apt install debootstrap qemu-user-static binfmt-support
sudo debootstrap --arch=arm64 trixie /srv/trixie-arm64 http://deb.debian.org/debian
sudo cp /etc/resolv.conf /srv/trixie-arm64/etc/
sudo chroot /srv/trixie-arm64 /bin/bash
# inside the chroot: steps 1–4 above
```

## License

GPL-2.0-or-later, like Yaba Sanshiro / Yabause — see [LICENSE](LICENSE).
The included third-party components and their licenses are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Yaba Sanshiro is developed by devMiyax and builds on Yabause by the Yabause
team. All credit for the emulator itself goes to them; this repository only
adds the R36S work on top.
