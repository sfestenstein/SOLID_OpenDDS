# SOLID_OpenDDS — OpenDDS facade + radar demo

A reusable OpenDDS facade library (`pub_sub_open_dds`) plus a notional
radar demo that exercises it. The facade hides every OpenDDS API surface
behind a small, OpenDDS-free C++11 contract so application code never
includes `dds/...` or `ace/...`. A CMake codegen helper emits the per-IDL
glue from one line per type. Fully containerized in a VS Code dev
container so you never have to build ACE/TAO/OpenDDS by hand.

## What's inside

```
.
├── .devcontainer/        # Dockerfile + devcontainer.json (objectcomputing/opendds base image)
├── CMakeLists.txt        # Top-level build; pulls in the subdirectories below
├── config/
│   └── rtps.ini          # RTPS peer-to-peer discovery (no DCPSInfoRepo needed)
├── pub_sub_open_dds/     # Facade library + CMake codegen helper for per-IDL glue
├── RadarSystem/          # Notional radar demo (SensorApp + WorkstationApp,
│                         #   own IDL + own CMakeLists), domain 43
├── tests/                # Smoke test + unit tests; runs on both OpenDDS and
│                         #   an in-memory runtime so QoS-sensitive tests are fast
└── docs/HANDOFF.md       # Engineering notes (gotchas, conventions, design rationale)
```

## Prerequisites (host)

- Docker Desktop / Docker Engine
- VS Code with the **Dev Containers** extension

## Open the dev container

1. Open this folder in VS Code.
2. Run **Dev Containers: Reopen in Container** from the command palette.
3. First build pulls `objectcomputing/opendds:latest` (large, but cached).

The image already has OpenDDS installed at `/opt/OpenDDS` with `DDS_ROOT`,
`ACE_ROOT`, `TAO_ROOT` and the IDL compilers on `PATH` (sourced via
`setenv.sh` in `/etc/bash.bashrc`).

## Build & test

Inside the container:

```bash
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Seven tests run:

| Test | Runtime | Purpose |
| ---- | ------- | ------- |
| `smoke_roundtrip_opendds`   | RTPS, domain 42 | End-to-end pub/sub round-trip over the real transport. |
| `smoke_roundtrip_inmemory`  | in-memory bus   | Same `.cpp`, in-memory fake — milliseconds, no DDS env. |
| `in_memory_roundtrip_test`  | in-memory bus   | Durability replay + `handle_keepalive_` regression + write-before-activate. |
| `lifecycle_test`            | no runtime      | `Service` state machine + missing-adapter error message. |
| `qos_profile_test`          | no runtime      | Built-in QoS profile field values. |
| `topic_config_test`         | no runtime      | INI parser corner cases (`TopicConfig::load_from_string`). |
| `xml_qos_test`              | OpenDDS XML loader | Locks in the fix for uninitialised fields in XML-resolved QoS. |

## Run the radar demo

Two terminals, from `build/RadarSystem/`:

```bash
./SensorApp      -DCPSConfigFile rtps.ini
./WorkstationApp -DCPSConfigFile rtps.ini
```

The workstation prints incoming heartbeats, radar tracks, and
command-status replies; the sensor prints inbound commands and
component-status requests. Ctrl-C either side for a clean shutdown.

The radar runs on DDS domain 43 (separate from the smoke test's domain
42) so they don't collide if you leave the smoke test running.

### Per-topic QoS

Each radar app loads a small INI file from its working directory that
maps topic names to a named QoS profile in the facade
([sensor_topics.ini](RadarSystem/config/sensor_topics.ini),
[workstation_topics.ini](RadarSystem/config/workstation_topics.ini)). The
built-in profiles live in
[qos.h](pub_sub_open_dds/include/pub_sub_open_dds/qos.h):

| Profile              | Reliability  | Durability        | History          | Extras                                                          |
| -------------------- | ------------ | ----------------- | ---------------- | --------------------------------------------------------------- |
| `best_effort`        | BEST_EFFORT  | VOLATILE          | KEEP_LAST 1      |                                                                 |
| `reliable`           | RELIABLE     | VOLATILE          | KEEP_LAST 10     |                                                                 |
| `reliable_transient` | RELIABLE     | TRANSIENT_LOCAL   | KEEP_LAST 10     |                                                                 |
| `event_bus`          | RELIABLE     | VOLATILE          | KEEP_ALL         |                                                                 |
| `latched`            | RELIABLE     | TRANSIENT_LOCAL   | KEEP_LAST 1      |                                                                 |
| `streaming`          | BEST_EFFORT  | VOLATILE          | KEEP_LAST 1      |                                                                 |
| `persistent`         | RELIABLE     | TRANSIENT_LOCAL   | KEEP_ALL         | Service-local PERSISTENT stand-in (no on-disk store yet)        |
| `heartbeat`          | BEST_EFFORT  | VOLATILE          | KEEP_LAST 1      | DEADLINE = 3 s — reader fires `on_requested_deadline_missed`    |
| `critical`           | RELIABLE     | VOLATILE          | KEEP_ALL         | MANUAL_BY_TOPIC liveliness, 5 s lease; bounded queue (1000)     |

The radar demo binds:

| Topic                        | Profile                                | Why                                                              |
| ---------------------------- | -------------------------------------- | ---------------------------------------------------------------- |
| `Command` / `CommandStatus`  | `reliable`                             | Operator action and ack must arrive.                             |
| `ComponentStatus`            | `xml:ComponentStatusDurable`           | Late workstation immediately sees the current state (XML).       |
| `RadarTrack`                 | `xml:RadarTrackStreaming`              | High-rate live data; latest matters (XML).                       |
| `ComponentStatusRequest`     | `best_effort`                          | Polled, response carries the value.                              |
| `SystemAlarm`                | `latched`                              | Each `alarm_id` is latched; alarms stay visible until acked.     |
| `RawIQSample`                | `streaming`                            | High-rate stream; drops under load are expected.                 |
| `TrackingCue`                | `reliable`                             | Infrequent and must arrive.                                      |
| `OperatorAuditLog`           | `event_bus`                            | Never drop a session-active log entry.                           |
| `OperatorChat`               | `reliable`                             | Per-operator history is preserved by KEEP_LAST 10.               |

To see durability in action, start `SensorApp` first, wait a few seconds,
then start `WorkstationApp` — the workstation immediately gets a backlog
of `ComponentStatus` heartbeats and the latest `SystemAlarm`, but no
replayed `RadarTrack` or `RawIQSample` data.

### DDS-XML QoS profiles

For more elaborate or operator-tweakable QoS, the facade also accepts an
**OMG DDS-XML** profile file
([radar_qos.xml](RadarSystem/config/radar_qos.xml)). Reference a profile
from the topic config with `xml:<profile_name>`, e.g.

```ini
RadarTrack       = xml:RadarTrackStreaming
ComponentStatus  = xml:ComponentStatusDurable
```

Schema is `/opt/OpenDDS/docs/schema/dds_qos.xsd`; profile names are
restricted to `[a-zA-Z0-9 ]+` (no underscores). The bundled
`radar_qos.xml` defines `RadarTrackStreaming` and `ComponentStatusDurable`,
intentionally mirroring the built-in `streaming` and `reliable_transient`
profiles so flipping the binding type does not change runtime behaviour.

## How user code consumes the facade

The pattern is: **one IDL file per topic struct** + **one `cmake_helper(TYPES …)`
call per consumer target**. The helper emits both the user-facing wrapper
header (`<TypeName>PubSub.h`) and a private adapter `.cpp` that's the
only TU that includes the OpenDDS-generated `*TypeSupportImpl.h`.

```cmake
# In your CMakeLists.txt:
add_library(myapp_idl)
OPENDDS_TARGET_SOURCES(myapp_idl PRIVATE Greeting.idl
                       OPENDDS_IDL_OPTIONS -Lc++11)
target_link_libraries(myapp_idl PUBLIC OpenDDS::Dcps)

add_executable(myapp main.cpp)
pub_sub_open_dds_generate_bindings(
    TARGET     myapp
    IDL_TARGET myapp_idl
    TYPES      MyMod::Greeting
)
```

```cpp
// In main.cpp — no DDS/ACE/TAO includes anywhere:
#include "pub_sub_open_dds_generated/GreetingPubSub.h"  // generated wrapper
#include "pub_sub_open_dds/service.h"

int main(int argc, char* argv[]) {
  using namespace pub_sub_open_dds;
  Service svc;                                       // default: OpenDDS runtime
  ServiceConfig cfg;
  cfg.domain_id = 42;
  for (int i = 1; i < argc; ++i)                     // e.g. -DCPSConfigFile rtps.ini
    cfg.runtime_args.emplace_back(argv[i]);
  auto topics = TopicConfig::load_from_string("greetings = reliable\n");
  svc.pre_activate(cfg, std::move(topics));
  svc.subscribe<MyMod::Greeting>(
      "greetings", [](const MyMod::Greeting& g) { /* ... */ });
  svc.post_activate();

  MyMod::Greeting g;
  g.text("hello");
  if (svc.publish("greetings", g) != WriteResult::Ok) { /* handle */ }

  svc.deactivate();
}
```

The repository's own tests also exercise the same facade shape against an
in-memory runtime, but that runtime seam lives in the library's private
source tree rather than the installed public headers.

## How it works (the 30-second tour)

1. **IDL → C++**. `OPENDDS_TARGET_SOURCES` runs `opendds_idl` and `tao_idl`
   against each `.idl`, producing the `*TypeSupportImpl` sources that the
   facade's adapter `.cpp` links against.
2. **Codegen**. `pub_sub_open_dds_generate_bindings(...)` emits one
   wrapper header + one adapter `.cpp` per IDL type. The adapter is the
   only TU outside the facade that touches OpenDDS.
3. **Discovery**. `rtps.ini` selects RTPS discovery so participants find
   each other over UDP multicast — no separate `DCPSInfoRepo` process
   required.
4. **Pub/Sub**. `Service::pre_activate` initialises the runtime and loads
  topic policy; `subscribe<T>` stages readers until `post_activate()` and
  `publish(topic, sample)` lazily creates writers on first use. The
  service dispatches through a `void*`-based runtime seam back into the
  OpenDDS-typed adapter.
5. **Testability**. Inside this repository, `Service` is exercised against
  both the real OpenDDS runtime and a private in-memory runtime that
  models RELIABLE/BEST_EFFORT, KEEP_LAST history depth, and
  TRANSIENT_LOCAL late-join replay.

## Troubleshooting

- **`find_package(OpenDDS)` fails**: confirm `/opt/OpenDDS` exists in the
  container and that `source /opt/OpenDDS/setenv.sh` is in your shell
  init. The `CMAKE_PREFIX_PATH` in [CMakeLists.txt](CMakeLists.txt)
  already covers the default install location.
- **`no TypeAdapter registered for '…'` at runtime**: a
  `publish<T>` / `subscribe<T>` call used a type whose generated adapter
  `.cpp` was not linked into the binary.
  Add the type to your `pub_sub_open_dds_generate_bindings(... TYPES …)`
  call.
- **`fatal error: <TypeName>C.h: No such file or directory`** while
  building a generated adapter: your IDL file name doesn't match the
  generated header naming. The codegen helper expects
  `<TypeName>.idl → <TypeName>C.h`. Either rename the IDL, or pass
  `HEADER_PREFIX` to `pub_sub_open_dds_generate_bindings` if you've put
  the generated headers under a subdirectory.
- **No samples received**: Docker bridge networks sometimes block UDP
  multicast. The radar demo runs both apps inside the same container by
  default, so this should "just work". For multi-container experiments,
  run them on the same Docker network (e.g. `docker compose` with the
  same `network`) or use `--network host` on Linux hosts.
- **Stale generated files**: delete `build/` and reconfigure.
- **VS Code says "the container doesn't support VS Code Server" / GLIBC
  error**: the official `objectcomputing/opendds` image is based on an
  older Ubuntu whose glibc predates VS Code Server's minimum (currently
  glibc 2.28+). The Dockerfile here works around that by using a
  two-stage build: the OpenDDS tree is *copied* out of the official
  image into an Ubuntu 22.04 base (glibc 2.35), which VS Code Server
  supports. If you ever swap the base image, make sure it ships glibc
  2.28 or newer.
