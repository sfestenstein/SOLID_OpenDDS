# pub_sub_open_dds — engineering notes

Single-file dump of what's in the repo today, the conventions we converged
on, every OpenDDS / IDL / CMake gotcha we hit (with the fix), and a brief
"how this got here" sketch. The intent is that anyone picking up the
project later — or a fresh AI chat — can read this one file and skip the
iteration archaeology.

> This file is **not** a how-to / README; for that see [README.md](../README.md).
> It is a notes file: terse, dense, and assumes you've already glanced at
> the code.

---

## 1. Build / run cheat sheet

- Always `source /opt/OpenDDS/setenv.sh` first (the dev-container does this
  in `/etc/bash.bashrc`, so interactive shells are fine; non-interactive /
  CI shells need it explicitly).
- Configure: `cmake -S . -B build -GNinja`
- Build:     `cmake --build build`
- Test:      `ctest --test-dir build --output-on-failure`
  (six tests: smoke_roundtrip × {opendds, inmemory}, in_memory_roundtrip_test,
  lifecycle_test, qos_profile_test, topic_config_test.)
- Manual radar demo, from `build/RadarSystem/`:
  - `./SensorApp      -DCPSConfigFile rtps.ini`
  - `./WorkstationApp -DCPSConfigFile rtps.ini`

---

## 2. Current layout (post SOLID-redesign pass)

```
pub_sub_open_dds/
├── CMakeLists.txt
├── cmake/
│   ├── pub_sub_open_dds_codegen.cmake         # pub_sub_open_dds_generate_bindings()
│   └── templates/
│       ├── PubSub.h.in                         # user-facing wrapper template
│       └── PubSub_adapter.cpp.in               # private OpenDDS adapter template
├── include/pub_sub_open_dds/
│   ├── fwd.h                  # forward decls + WriteResult, LifecycleState
│   ├── error.h                # Error : std::runtime_error
│   ├── service_config.h       # ServiceConfig (façade-typed: int / vector<string>)
│   ├── qos.h                  # QosProfile, WriterQos, ReaderQos, built-in profiles
│   ├── runtime.h              # IRuntime + make_opendds_runtime / make_in_memory_runtime
│   ├── service.h              # Service (depends on IRuntime; template-thin glue)
│   ├── publisher.h            # Publisher<T> (thin handle; no DDS in header)
│   ├── subscriber.h           # Subscriber<T> (thin handle; no DDS in header)
│   ├── topic_config.h         # TopicConfig (PIMPL'd XML loader)
│   └── detail/                # PRIVATE — user TUs must not include these
│       ├── typed_binding.h         # TypedWriterBinding / TypedReaderBinding bases
│       ├── data_adapter.h          # TypeAdapter interface
│       ├── registry.h              # process-wide type-adapter map
│       └── opendds_bindings.h      # DDS-aware bindings + listener template
└── src/
    ├── service.cpp                       # lifecycle state machine
    ├── qos_profile.cpp                   # built-in profile factories + name lookup
    ├── topic_config.cpp                  # INI + DDS-XML loader (PIMPL'd)
    ├── registry.cpp                      # type-adapter registry
    └── runtime/
        ├── opendds_runtime.cpp           # the ONE TU that owns OpenDDS plumbing
        └── in_memory_runtime.cpp         # test fake (fidelity B: durability + RELIABLE/BE)

RadarSystem/                              # sole demo; no hand-written DdsTraits
├── CMakeLists.txt                        # calls pub_sub_open_dds_generate_bindings()
├── config/
├── idl/                                  # per-message .idl files + own CMakeLists.txt
└── src/{SensorApp,WorkstationApp}.cpp    # zero OpenDDS includes; #include "...PubSub.h"

tests/
├── CMakeLists.txt
├── idl/{Ping.idl,CMakeLists.txt}         # one-struct test-local IDL
├── smoke_roundtrip.cpp                   # parameterised: opendds | inmemory
└── unit/
    ├── lifecycle_test.cpp
    ├── qos_profile_test.cpp
    ├── topic_config_test.cpp
    └── in_memory_roundtrip_test.cpp
```

`pub_sub_open_dds` is a static library. Its **public headers contain zero
`#include <dds/*>` / `#include <ace/*>`**. The OpenDDS link dependency is
re-exported PUBLIC because each consumer's generated adapter `.cpp`s (see
the codegen helper) call into `libDcps`/`libRtps` at link time.

---

## 3. SOLID redesign (this iteration)

Three new seams replaced the previous direct OpenDDS leakage:

1. **Opaque `WriterQos` / `ReaderQos`** wrap `QosProfile`. The runtime
   adapter is the only place that translates them to `DDS::DataWriterQos`
   / `DataReaderQos`. The previous `QosApplier` with its embedded
   `std::function<void(DDS::DataWriterQos&)>` is gone.

2. **`IRuntime` interface.** `Service` holds `std::shared_ptr<IRuntime>`
   and never touches OpenDDS directly. Two impls:
   - `OpenDddsRuntime` — the real transport.
   - `InMemoryRuntime` — process-local topic bus the tests use. Models
     RELIABLE/BEST_EFFORT (recorded), KEEP_LAST history depth, durable
     late-join replay (TRANSIENT_LOCAL).
   Selected via `make_opendds_runtime()` / `make_in_memory_runtime()`;
   `Service::Service()` defaults to the OpenDDS runtime.

3. **Per-type binding via CMake codegen.** The user no longer writes
   `DdsTraits<T>`. A new CMake helper:
   ```cmake
   pub_sub_open_dds_generate_bindings(
       TARGET     SensorApp
       IDL_TARGET radar_idl
       TYPES      RadarSystem::ComponentStatus  RadarSystem::RadarTrack  ...
   )
   ```
   emits two files per IDL type into the consumer's build dir:
   - `<TypeName>PubSub.h` — user-facing wrapper (includes only
     `<TypeName>C.h`, *not* `*TypeSupportImpl.h`; provides `using
     <TypeName>Publisher = pub_sub_open_dds::Publisher<Ns::TypeName>;` +
     same for Subscriber).
   - `<TypeName>PubSub_adapter.cpp` — the only TU outside the facade
     that includes `*TypeSupportImpl.h`. Specialises `TypeAdapter` and
     registers itself via a `namespace { const Registrar reg; }` static
     initialiser.

Plus three smaller leak fixes:
- `Publisher<T>::write` returns `WriteResult` (enum class), not
  `DDS::ReturnCode_t`.
- `ServiceConfig` is `{int domain_id; vector<string> runtime_args;
  optional<filesystem::path> config_file;}` — no `DDS::DomainId_t`, no
  `ACE_TCHAR**`. The runtime adapter builds the C-style `argv` for
  `TheParticipantFactoryWithArgs` internally.
- `Publisher<T>::wait_for_subscribers` body and the listener that calls
  back into `Subscriber<T>` moved out of the public headers into
  `detail/opendds_bindings.h` (DDS-aware, private) and the generated
  adapter `.cpp`.

### What's still allowed in user TUs (in-scope vs out-of-scope)

In-scope and achieved: zero OpenDDS API in user-authored code. RadarSystem
apps have no `DDS::*`, `OpenDDS::*`, `ACE_*`, or `TAO_*` references in
their `.cpp` files (a grep of the post-refactor tree confirms — see §9).

Out-of-scope by design: zero OpenDDS *preprocessor symbols*. The
generated `<TypeName>PubSub.h` `#include`s `<TypeName>C.h` (the IDL-derived
C-mapping header) so the IDL struct definition is available in user code.
`<TypeName>C.h` itself transitively pulls `dds/Versioned_Namespace.h` and
a couple of OpenDDS macros. Hiding those would mean either PIMPL'ing user
data (defeats the purpose) or regenerating a stripped IDL header (loses
bounded sequences and IDL constructs).

### Testability won

Six tests now run; previously there was just one smoke. The new shape:

- `smoke_roundtrip_opendds`   — full RTPS round-trip (the historic test).
- `smoke_roundtrip_inmemory`  — same `.cpp`, in-memory runtime; ~20 ms.
- `in_memory_roundtrip_test`  — drops the Subscriber<T> shared_ptr (HANDOFF §7
                                 regression), durability replay, write-before-activate.
- `lifecycle_test`            — Service state machine + null-runtime + missing-type-adapter
                                 error message (no OpenDDS, no IDL).
- `qos_profile_test`          — built-in profile field values (no runtime).
- `topic_config_test`         — INI parser via `TopicConfig::load_from_string`
                                 (comments, whitespace, fallbacks, malformed lines).- `xml_qos_test`              — DDS-XML loader: profiles resolve, raw payload is
                                 populated with canonical defaults for unmentioned
                                 fields, built-in profile path does NOT attach raw
                                 (so it still hits participant defaults).
Running `smoke_roundtrip` against both runtimes is the validation that the
in-memory fake matches the real runtime's observable semantics for the
tested QoS path.

---

## 4. QoS facility (unchanged from prior iteration)

- `qos.h` exposes `QosProfile { name, reliable, durable, keep_all,
  history_depth, deadline_ms, liveliness_lease_ms, liveliness_manual,
  max_samples, max_instances }` + `WriterQos` / `ReaderQos` wrappers.
- Built-in named profiles in `pub_sub_open_dds::qos`: `best_effort`,
  `reliable`, `reliable_transient`, `event_bus`, `latched`, `streaming`,
  `persistent`, `heartbeat`, `critical`. `find_builtin_profile(name)` is
  case-insensitive.
- `TopicConfig::load_from_file(ini)` + optional `use_xml_qos_file(xml)`
  is the single runtime QoS entry point. `writer_qos_for(topic, default)`
  / `reader_qos_for(...)` resolve built-in names directly, dispatch
  `xml:<profile>` references through the loader, and fall back to
  `default` with a `std::cerr` warning on any miss.
- `Service::register_publisher<T>` / `register_subscriber<T>` accept a
  `WriterQos` / `ReaderQos` defaulting to `make_writer_qos(qos::reliable())`
  / ditto reader.
- The XML loader is held PIMPL-style inside `TopicConfig` (`struct Impl;`)
  so `topic_config.h` carries no OpenDDS includes.

---

## 5. OpenDDS XML QoS gotchas

1. The CMake target name imported by `find_package(OpenDDS)` is
   `OpenDDS::QOS_XML_XSC_Handler` — **not**
   `OpenDDS::OpenDDS_QOS_XML_XSC_Handler`. The helper macro strips the
   `OpenDDS_` prefix when it builds the imported target name.
2. The schema `/opt/OpenDDS/docs/schema/dds_qos.xsd` restricts
   `qos_profile name=` to `[a-zA-Z0-9 ]+` — **no underscores**. Use
   `RadarTrackStreaming`, not `RadarTrack_Streaming`. Validation failure
   surfaces as a vanilla `rc=1` from `QOS_XML_Loader::init`; running
   `xmllint --noout --schema dds_qos.xsd <file>.xml` gives the precise
   reason.
3. OpenDDS resolves the schema via the `DDS_ROOT` env var
   (`$DDS_ROOT/docs/schema/`), set by `/opt/OpenDDS/setenv.sh`. The
   dev-container sources this in `/etc/bash.bashrc`, but non-interactive
   processes (e.g. CI) need to source it explicitly or the XML loader will
   silently fail.
4. **`QOS_XML_Loader::get_datawriter_qos` / `get_datareader_qos` only
   overwrite the fields the XML mentions.** Fields the XML omits keep
   whatever the in/out `DDS::DataWriterQos` struct held before the call.
   A default-constructed `DDS::DataWriterQos` leaves several substructures
   (deadline, liveliness, resource_limits) uninitialised at the C++ level
   because the CORBA-generated structs don't perform real DDS-default
   initialisation. Forwarding that to OpenDDS produces wildly
   incompatible QoS (e.g. a 600-billion-millisecond deadline) and
   silently breaks reader/writer matching — the radar demo's `xml:`-bound
   topics didn't flow in the first cut of the refactor for exactly this
   reason. **Fix**: pre-seed the in/out struct with
   `TheServiceParticipant->initial_DataWriterQos()` (resp. reader) before
   calling the loader. Locked in by `xml_qos_test`.
5. **Don't round-trip XML QoS through `QosProfile`.** The QosProfile
   only models the headline dimensions; the XML may set anything (e.g.
   partition, lifespan, latency_budget). `TopicConfig::writer_qos_for` /
   `reader_qos_for` therefore attach the fully-resolved
   `DDS::DataWriterQos` / `DataReaderQos` as an opaque payload on the
   returned `WriterQos` / `ReaderQos` (via `attach_raw` /
   `raw()`). The generated OpenDDS adapter uses the raw payload verbatim
   when present, falls back to the `QosProfile` translation otherwise.
   `InMemoryRuntime` ignores the raw payload and uses the `QosProfile`.

---

## 6. OpenDDS IDL CMake gotchas (learned the hard way)

1. **Don't list IDL files from a parent CMakeLists when they live in a
   subdir.** `OPENDDS_TARGET_SOURCES` auto-adds
   `-I${CMAKE_CURRENT_SOURCE_DIR}` to opendds_idl. If your IDL files sit
   in a subdir and you also explicitly add `-I${SRC}/sub`, opendds_idl
   resolves cross-IDL `#include`s through BOTH paths and emits duplicate
   `#include`s in the generated headers (e.g. both `#include "CommonC.h"`
   AND `#include "sub/CommonC.h"`), and one of them won't exist.
   **Fix**: give the IDL files their OWN `CMakeLists.txt` in their dir so
   `CMAKE_CURRENT_SOURCE_DIR` *is* that dir; sibling `#include "Common.idl"`
   then resolves through the single auto-added path. `RadarSystem/idl/`
   and `tests/idl/` both follow this pattern.
2. **List IDL files PRIVATE, not PUBLIC, in `OPENDDS_TARGET_SOURCES`**
   when the IDL target lives in a different CMake directory from its
   consumers. PUBLIC propagates the generated `.idl` source files into
   every consumer's source list, but their `GENERATED` property was set
   in the IDL dir scope and isn't visible to the consumer's dir →
   "Cannot find source file". The public include path that exports the
   generated `.h` files is set separately by the helper and stays
   exported either way.
3. **`sequence` is a reserved IDL keyword.** A `@topic` struct field
   named `sequence` makes opendds_idl emit a bare "syntax error". Use any
   other identifier (RadarTrack uses `seq_no`).
4. **Fixed-size arrays via `typedef T name[N]` are rejected by
   opendds_idl** when used as struct fields under the C++11 mapping (also
   surfaces as "Illegal syntax or missing identifier following member
   type"). Use `typedef sequence<T, N> name;` instead — generates a
   bounded `std::vector<T>` under the C++11 mapping.
5. **Codegen helper expects one `<TypeName>.idl` per type.** The IDL
   file name drives the generated `<TypeName>C.h` and
   `<TypeName>TypeSupportImpl.h` filenames the helper `#include`s. The
   test IDL is therefore `tests/idl/Ping.idl` (not `Smoke.idl`), even
   though the type inside is `Smoke::Ping`. RadarSystem already follows
   one-IDL-per-message-type for the same reason. If you have to deviate,
   the helper accepts an `HEADER_PREFIX` escape hatch.

---

## 7. Critical facade invariant — don't break this

`Service` **MUST** keep registered `Publisher<T>` / `Subscriber<T>` handles
alive for its own lifetime. The OpenDDS listener (instantiated inside the
generated adapter `.cpp`) holds a `std::weak_ptr<OpenDddsReaderBinding>`
back into the binding. The binding is also referenced by the user-returned
`Subscriber<T>`, but `Service::handle_keepalive_` (a
`vector<shared_ptr<void>>`) holds the only strong reference if the user
discards their `shared_ptr<Subscriber<T>>`. `Service::deactivate()` clears
the keepalive **after** `runtime_->shutdown()` has drained the listener
callbacks (which `delete_contained_entities()` does). Don't reorder those
two lines.

The in-memory runtime exercises the same invariant via
`in_memory_roundtrip_test`'s "drop the Subscriber<T> shared_ptr" case.

---

## 8. Conventions verified

- IDL uses the **C++11 mapping**:
  `OPENDDS_TARGET_SOURCES(... OPENDDS_IDL_OPTIONS -Lc++11)`. Generated
  members are `std::string` / `int32_t`; accessors are `m.from()` /
  `m.from("val")`, not `m.from = ...`.
- CMake options: `PUBSUB_OPEN_DDS_BUILD_TESTS` (default ON).
- Facade lifecycle: `pre_activate → activate → post_activate → deactivate`.
  Out-of-order calls throw `pub_sub_open_dds::Error`.
- Hot paths return `WriteResult` (façade enum), not `DDS::ReturnCode_t`.
- `Service` is non-copyable and non-movable; the destructor calls
  `deactivate()`.
- Tests are framework-less — each is its own `int main` with an `EXPECT`
  macro that prints the failing line and exits non-zero so CTest catches
  it. Add a real framework only when the lifecycle / fixture noise
  outgrows that.

---

## 9. Verification one-liners

```bash
# Public-header purity (must return zero hits other than the documented
# explanatory comments in fwd.h and the SensorApp.cpp comment that
# mentions ACE_TCHAR**):
grep -rEn 'DDS::|OpenDDS::|ACE_|TAO_' RadarSystem/src/ tests/ \
     --include='*.cpp' --include='*.h'

# No DDS/ACE includes in public headers (excluding the documented private
# detail/opendds_bindings.h):
grep -rEn '#include[ ]+[<"](dds|ace)/' \
     pub_sub_open_dds/include/pub_sub_open_dds/ --include='*.h' \
   | grep -v '/detail/'
```

---

## 10. Deferred work

- Multi-handler / lambda-chain registry per topic.
- Full QoS configuration API (some knobs still hardcoded inside the
  built-in profiles).
- True PERSISTENT durability (the in-memory runtime models late-join
  replay; OpenDDS still uses TRANSIENT_LOCAL stand-in for `persistent`).
- Real test framework (GoogleTest or Catch2) — current framework-less
  pattern works at 4 unit tests, will start hurting around 10.
- CI workflow, Doxygen, multi-domain support per `Service`.
- `pub_sub_open_dds` installable as a package so external projects can
  `find_package(pub_sub_open_dds)` instead of `add_subdirectory`'ing it.
