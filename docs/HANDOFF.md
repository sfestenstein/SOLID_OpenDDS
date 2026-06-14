# pub_sub_open_dds — engineering handoff

Current-state notes for the `pub_sub_open_dds` package and the radar demo.
This file is intentionally architecture-first: it explains what is public,
what is private, what invariants matter, and which OpenDDS/CMake gotchas
are still relevant.

For usage examples, see [README.md](../README.md).

---

## 1. Build / run cheat sheet

- Configure: `cmake -S . -B build -GNinja`
- Build: `cmake --build build`
- Test: `ctest --test-dir build --output-on-failure`
- Manual radar demo, from `build/RadarSystem/`:
	- `./SensorApp      -DCPSConfigFile rtps.ini`
	- `./WorkstationApp -DCPSConfigFile rtps.ini`

The dev container already has `/opt/OpenDDS/setenv.sh` sourced for
interactive shells.

---

## 2. Public package boundary

The installed public headers under `pub_sub_open_dds/include/pub_sub_open_dds/`
are intentionally small:

- `fwd.h`
	- `WriteResult`
	- `LifecycleState`
	- forward declarations for façade-level types
- `service_config.h`
	- `ServiceConfig`
- `qos.h`
	- `QosProfile`, `WriterQos`, `ReaderQos`, built-in profiles
- `qos_profile.h`
	- compatibility shim that includes `qos.h`
- `topic_config.h`
	- `TopicConfig`
- `service.h`
	- `Service`

Anything else is private implementation detail.

Notably, these headers are no longer public:

- `publisher.h`
- `subscriber.h`
- `runtime.h`
- `detail/*`
- `error.h`

That matches the current user model: application code knows about `Service`,
topic names, IDL structs, `TopicConfig`, and façade QoS values. It does not
manage publisher/subscriber handles directly and does not depend on runtime
seams.

---

## 3. Current layout

```text
pub_sub_open_dds/
├── CMakeLists.txt
├── cmake/
│   ├── pub_sub_open_dds_codegen.cmake
│   └── templates/
│       ├── PubSub.h.in
│       └── PubSub_adapter.cpp.in
├── include/pub_sub_open_dds/
│   ├── fwd.h
│   ├── qos.h
│   ├── qos_profile.h
│   ├── service.h
│   ├── service_config.h
│   └── topic_config.h
└── src/
		├── service.cpp
		├── qos_profile.cpp
		├── topic_config.cpp
		├── registry.cpp
		├── runtime.h                        # private runtime seam
		├── runtime/
		│   ├── in_memory_runtime.cpp        # private test/runtime fake
		│   └── opendds_runtime.cpp          # real OpenDDS runtime impl
		└── pub_sub_open_dds/detail/
				├── data_adapter.h
				└── opendds_bindings.h
```

Radar demo code in `RadarSystem/src/` includes only:

- generated `pub_sub_open_dds_generated/*PubSub.h`
- public façade headers such as `service.h` and `topic_config.h`

No hand-written app code includes `dds/...`, `ace/...`, or the package's
private runtime/detail headers.

---

## 4. User-facing programming model

The current lifecycle is:

1. Build or load a `TopicConfig`
2. `Service::pre_activate(cfg, topic_config)`
3. `Service::subscribe<T>(topic_name, callback)` zero or more times
4. `Service::post_activate()`
5. `Service::publish(topic_name, sample)` as needed
6. `Service::deactivate()`

Important behavior:

- `subscribe<T>` is valid after `pre_activate()`.
	- Before `post_activate()`, subscriptions are staged.
	- `post_activate()` performs the hidden activation step and realizes them.
- `publish()` requires the service to be post-activated.
- Topic declarations come from `TopicConfig` when one is supplied.
- Topic/type mismatches are rejected the first time a conflicting
	`publish<T>` / `subscribe<T>` is attempted.

This is intentionally service-centric. There is no public `activate()`,
`register_publisher<T>()`, or `register_subscriber<T>()` anymore.

---

## 5. Generated code contract

`pub_sub_open_dds_generate_bindings(...)` emits two artifacts per IDL type:

- `<TypeName>PubSub.h`
	- includes only `<TypeName>C.h` plus `pub_sub_open_dds/service.h`
	- exposes the IDL struct definition alongside the façade API
- `<TypeName>PubSub_adapter.cpp`
	- includes `*TypeSupportImpl.h`
	- implements and registers the private `TypeAdapter`

This is the key isolation boundary:

- user-authored code sees the IDL struct and the façade
- generated adapter `.cpp` files are the only consumer TUs that touch
	OpenDDS type-support headers

The helper expects one generated header family per logical type name.
In practice, this repo uses one IDL file per message type.

---

## 6. Runtime / test seam

`Service` still depends internally on `IRuntime`, but that seam is private
to the repository and not part of the installed include tree.

Implications:

- External consumers are expected to use the default OpenDDS-backed
	`Service` constructor.
- Repository tests can still exercise `Service` against the private
	in-memory runtime because test targets add `pub_sub_open_dds/src` to
	their include path.
- `runtime.h` staying private is intentional: it supports repository
	testing and implementation isolation, not the normal application API.

The in-memory runtime models enough semantics to keep the tests meaningful:

- synchronous delivery on write
- topic/type consistency checks
- KEEP_LAST history
- durable late-join replay
- RELIABLE/BEST_EFFORT recorded at the façade level

---

## 7. QoS model

Public QoS stays façade-owned:

- `QosProfile` expresses the common knobs users care about
- `WriterQos` / `ReaderQos` wrap that profile
- built-ins live in `pub_sub_open_dds::qos`

`TopicConfig` is the runtime policy entrypoint:

- `load_from_file()` / `load_from_string()` parse the tiny INI mapping
- `use_xml_qos_file()` enables `xml:<profile>` references
- `writer_qos_for()` / `reader_qos_for()` resolve built-in or XML-backed
	profile names

The XML path stays PIMPL-backed so `topic_config.h` has no OpenDDS or Xerces
includes.

---

## 8. OpenDDS / CMake gotchas that still matter

1. `OpenDDS::QOS_XML_XSC_Handler` is optional.
	 - When the target is unavailable, XML-backed QoS loading is compiled out
		 and the façade throws a runtime error if the feature is requested.

2. DDS-XML profile names in the schema cannot contain underscores.
	 - Use names like `RadarTrackStreaming`, not `RadarTrack_Streaming`.

3. XML-resolved QoS must start from OpenDDS defaults.
	 - The loader only overwrites fields mentioned in XML.
	 - The implementation pre-seeds the QoS structs with participant defaults
		 before applying XML overrides.

4. IDL targets should use `OPENDDS_TARGET_SOURCES(... PRIVATE ...)`.
	 - Export generated headers separately; do not propagate generated `.idl`
		 sources across CMake directory boundaries.

5. Keep IDL files in the directory whose `CMakeLists.txt` invokes
	 `OPENDDS_TARGET_SOURCES`.
	 - That avoids duplicate or mismatched generated include paths.

6. The codebase is pinned to C++11-compatible language/library features.
	 - Avoid `std::optional`, `std::filesystem`, `std::make_unique`, and other
		 C++14/17 conveniences in public or shared code.

---

## 9. Critical invariants

1. `post_activate()` is the only public transition to a fully active service.
	 - Internal activation happens there when the service is still in the
		 `PreActivated` state.

2. Reader bindings must outlive in-flight callbacks.
	 - `Service` stores active reader bindings in `subscriber_bindings_`.
	 - `deactivate()` shuts the runtime down before clearing those bindings.

3. Writer bindings are cached by topic after first publish.
	 - Reusing them preserves topic/type consistency and avoids rebuilding the
		 writer path on every sample.

4. Generated adapters must be linked into the consuming target.
	 - If they are missing, `publish<T>` / `subscribe<T>` fails with the
		 "no TypeAdapter registered" runtime error.

---

## 10. Verification shortcuts

Useful checks while iterating:

```bash
# Build the main library/apps/tests slice used during this refactor.
cmake --build build --target pub_sub_open_dds SensorApp WorkstationApp \
	lifecycle_test in_memory_roundtrip_test smoke_roundtrip

# Run the focused validation slice.
ctest --test-dir build --output-on-failure \
	-R 'lifecycle_test|in_memory_roundtrip_test|smoke_roundtrip_inmemory'

# Confirm the public include tree contains only the intended headers.
find pub_sub_open_dds/include/pub_sub_open_dds -maxdepth 1 -type f | sort
```

---

## 11. Deferred work

- Decide whether the private runtime seam should stay repository-only or be
	promoted to a supported external testing API in a future iteration.
- Broaden docs/examples around `TopicConfig` as the canonical declaration
	source for topics.
- If external packaging matters later, add an install/export story for the
	trimmed public header set.
