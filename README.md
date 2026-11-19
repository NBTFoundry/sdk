# NBT App SDK

The C++ SDK for applications that run under NBT Manager on BMW NBT Evo
(iDrive 6) head units. It gives an application its side of the manager's
bridge (launch, readiness, heartbeats, status, live settings, signals), the
iDrive controller and gamepad input, an OpenGL ES 2 window on the head unit's
panel, audio output with the car's audio routing, and logging that ends up in
the application's log on the car. The same code builds for the head unit
(QNX 6.5, ARMv7) and for a Linux desktop, where the window is an X11 window.

Not affiliated with or endorsed by BMW AG. BMW, iDrive and NBT are trademarks
of BMW AG.

## Contents

| Library | What it gives an application |
| --- | --- |
| `libnbt-sdk` | `Lifecycle` (bridge), `Settings`, log and crash lines, `VideoEgl`, `AudioPcm` and `AudioRoute`, the controller and gamepad readers, `InputMixer`, `BackGesture` |
| `libnbt-audio-mixer` | `AudioMixer`: 16 sample channels over a music renderer |
| `libnbt-gles2` | A GL1-to-GLES2 shim for porting fixed-function OpenGL code |

The API is described in [docs/api.md](docs/api.md); the bridge contract it
implements is `NBTFoundry/manager: docs/app-bridge.md`.

## Build and test

Linux host (gcc, Python 3, X11, EGL and GLESv2 development files; Xvfb for the
window test):

```sh
make -j"$(nproc)" host               # libraries, unit tests, smoke app
make test                            # unit tests, smoke tests, neutrality check
make test-asan                       # the same under AddressSanitizer/UBSan
make check-static-init               # no static constructors in any object
make check-readers NBT_MGR_DIR=../manager   # bridge readers match the manager's
```

Head unit (QNX 6.5 SDP for ARMv7; `QNX_SCREEN_LIBDIR` holds the head unit's
`libscreen.so`):

```sh
make QNX_ENV=/path/to/qnx-env.sh QNX_SCREEN_LIBDIR=/path/to/screen/lib qnx qnx-check
```

`qnx-check` rejects symbols that QNX 6.5's C++ runtime lacks
(`GLIBCXX_3.4.14` and newer, `__cxa_throw`) and checks that `kdMain` is
exported.

Libraries go to `build/lib/libnbt-{sdk,audio-mixer,gles2}-{host,asan,qnx}.a`.

## Using it from an application

Include `nbt-sdk.mk` from the application's Makefile; it builds the SDK into
the application's build folder and gives the include and link flags:

```make
NBT_SDK_DIR ?= ../sdk
NBT_SDK_BUILD_DIR := $(abspath build)/sdk
include $(NBT_SDK_DIR)/nbt-sdk.mk

CXXFLAGS += $(NBT_SDK_INC)

build/app-host: $(APP_OBJS) $(NBT_SDK_HOST_LIBS)
	$(CXX) $(APP_OBJS) $(NBT_SDK_HOST_LIBS) $(NBT_SDK_HOST_LDLIBS) -o $@

build/app-qnx: $(APP_QNX_OBJS) $(NBT_SDK_QNX_LIBS)
	$(QCC) -Vgcc_ntoarmv7le_gpp -lang-c++ $(APP_QNX_OBJS) $(NBT_SDK_QNX_LIBS) \
		$(NBT_SDK_QNX_LDFLAGS) -L$(QNX_SCREEN_LIBDIR) $(NBT_SDK_QNX_LDLIBS) -o $@
```

[`tests/smoke_app.cpp`](tests/smoke_app.cpp) is a complete small application.

## Rules the code follows

- C++11 and C99, no exceptions, no RTTI, and nothing that runs before `main`
  (`make check-static-init`).
- Bounded buffers; no allocation on the input and settings paths.
- One `Lifecycle` and one open `AudioPcm` per process.
- Nothing specific to one application: `make check-generic` runs with `make test`.
- The four bridge readers are copies of the manager's (see [READERS](READERS)).
