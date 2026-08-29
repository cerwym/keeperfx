# KeeperFX Multicore Parallelization Plan

## 1. Context and Motivation

KeeperFX's simulation is currently fully single-threaded. One `update()` call
per game turn (`src/main.cpp:2540`) walks every subsystem — creature/AI
updates, pathfinding, rooms, research, manufacturing, computer players —
sequentially on the main thread. As levels grow larger (more creatures, bigger
maps, more active AI keepers), this single sequential pass becomes the limit
on simulation speed, even though modern desktops and consoles have many idle
cores.

The only concurrency that exists in the engine today is narrowly scoped to
rendering and startup I/O:

- `src/renderer/RenderThreadManager.h/.cpp` overlaps GPU command submission
  (`EndFrame_GL()`) with the next frame's game-thread work, for the OpenGL
  backend only, via a documented mutex/condition-variable handshake
  (`WaitForCompletion()` / `Signal()`).
- `src/bflib_sndlib.cpp:497,520` runs a one-shot `std::thread` to preload sound
  banks during the splash screen, joined before first use.

Nothing in the simulation, AI, or pathfinding core runs concurrently. This
document proposes a phased roadmap for changing that, while respecting two
constraints that are non-negotiable for this codebase:

1. **Deterministic lockstep multiplayer.** Every turn's simulation must
   produce byte-identical results on every machine. This is enforced today by
   per-turn checksums (`src/net_checksums.c`) and by several global, mutable
   RNG seed streams whose *order of consumption* must stay reproducible.
   Careless parallelization — even something as "safe-looking" as adding a
   mutex around a shared RNG call — can silently break multiplayer sync,
   because the *order* threads acquire that mutex is itself
   scheduler-dependent and therefore non-reproducible across machines.
2. **Handheld ports exist** (Vita, 3DS, Switch, Wii U) with limited and,
   critically, **unverified** threading support. `build/cmake/modules/
   Helpers.cmake:60-63` explicitly skips `Threads::Threads`/pthread linkage
   for these platforms ("Homebrew platforms: no special link flags needed"),
   meaning no C++ threading primitive (`std::thread`, `std::mutex`,
   `std::condition_variable`, `std::jthread`) has ever been exercised in this
   tree on vitasdk, libctru, libnx, or WUT. Any plan must design for graceful,
   built-in degradation to a single worker on these platforms from the start,
   not add it as an afterthought.

This document is a **design and roadmap only** — no simulation code is changed
as part of writing it. It is meant to be a reference for scoping and
sequencing future implementation work.

### Explicit non-goals

- This plan does **not** propose parallelizing `process_creature_state()` (the
  per-creature finite-state-machine/AI dispatch itself) at any phase.
  Pathfinding is targeted specifically because it is CPU-hot, free of RNG
  calls, and produces a self-contained per-creature result that's trivial to
  apply deterministically — properties the rest of the FSM does not share (it
  freely reads and mutates other creatures/rooms/dungeons and calls the synced
  RNG streams throughout). Parallelizing the FSM itself is out of scope here.
- Frame pipelining (overlapping simulation of turn N+1 with drawing of turn N)
  is named as a future direction in §4.5 but is not designed in detail — it is
  a structurally larger change (a double-buffered sim/render state split)
  deserving its own follow-on design pass.

## 2. Foundational Job-System Design

### 2.1 Shape of the abstraction

Every candidate workload below has the same shape: walk a bounded collection
(at most ~1024 creatures, a fixed-size texture-animation table, a lighting
grid) once per tick, do independent work per item, then continue. A
**static-split fork-join `parallel_for`** — not a general work-stealing task
graph — is the right primitive: simpler to reason about, simpler to keep
deterministic, and it degrades to "call inline on this thread" trivially when
only one worker is available.

New module, mirroring the existing `src/kfx/` layout (`src/kfx/profiling/`,
`src/kfx/save/`, `src/kfx/engine/`, `src/kfx/config/`):

- **`src/kfx/jobsystem/KfxJobSystem.h/.cpp`** — the real C++20 implementation.
  A persistent pool of `std::jthread` workers parked on a condition variable
  between dispatches (not spawned/joined every tick — at ~20 ticks/second,
  per-tick thread creation would be wasted overhead, especially on
  handhelds). Dispatch is fork-join: split `[0, count)` into `worker_count`
  contiguous chunks and block until all chunks complete — the same
  fan-out/fan-in shape as `RenderThreadManager`'s handshake, generalized from
  one dedicated thread to a pool.
- **`src/kfx/jobsystem/KfxJobSystemC.h`** — a plain C-linkage shim, following
  the exact precedent already established by `src/kfx/profiling/
  KfxProfilingC.h`, which wraps Tracy's C++ API behind `TRACY_ENABLE`-gated
  macros for `.c` callers. The job-system shim does the same for threading:

  ```c
  typedef void (*KfxParallelForFn)(long begin, long end, void *userdata);
  void kfx_job_system_init(int max_workers_hint);
  void kfx_job_system_shutdown(void);
  int  kfx_job_system_worker_count(void);
  void kfx_parallel_for(long count, long min_grain, KfxParallelForFn fn, void *userdata);
  void kfx_job_system_set_forced_sequential(TbBool force);
  ```

  Plain C files such as `thing_list.c` and `ariadne.c` call this directly — no
  lambdas or `std::function` ever cross the C/C++ boundary; the C++ side wraps
  a function pointer + `void*` internally.

- **One degradation choke point.** Inside `kfx_parallel_for`: if
  `kfx_job_system_worker_count() <= 1`, or `count < min_grain`, or
  `kfx_job_system_set_forced_sequential(true)` was set, it calls
  `fn(0, count, userdata)` inline on the caller's thread — control flow
  byte-identical to the pre-parallel code. This single choke point is both the
  handheld fallback mechanism and the on/off switch the validation harness
  (§3) needs.

### 2.2 Worker-count detection (handheld constraint)

- **Desktop (Windows/Linux):** `std::thread::hardware_concurrency()`, clamped
  to a sane ceiling (e.g. 8, to avoid oversubscription pathologies on
  high-core-count machines for grain sizes this small), user-overridable via a
  new `-workers N` flag.
- **Vita/3DS/Switch/Wii U:** do **not** trust `hardware_concurrency()` blindly
  on these embedded libcs. Set a hard-coded, conservative per-platform default
  worker cap via each `build/cmake/modules/Platform*.cmake` (e.g. 1 on 3DS;
  1–2 on Vita/Switch/Wii U, leaving cores for OS/graphics).
- `kfx_job_system_init()` runs once at startup, after platform detection,
  before the first `update()` tick.

### 2.3 CLI/config surface

Follow the existing pattern for threading a new flag through
(`-fps`/`-packetload`/`-dbgpathfind` parsing in `src/main.cpp:3700-3800`, into
`struct StartupParameters` in `src/keeperfx.hpp:108-132`, mirrored into
`game.*` fields as done for `packet_save_enable` in `src/main_game.c:494-495`):

- `-workers N` → `start_params.job_system_workers` → `game.job_system_workers`
  → passed to `kfx_job_system_init()`.
- `-noparallel` → `kfx_job_system_set_forced_sequential(true)` at startup —
  the switch the validation harness (§3) flips between control and treatment
  runs.
- A new bit in `enum DebugFlags` (`src/keeperfx.hpp:87-94`) — e.g.
  `DFlg_LogChecksums` — to extend fuller per-thing checksum logging into
  single-player replay (needed by §3).

### 2.4 Prerequisite spike (blocking, not a nice-to-have)

Before phase (a) below is considered "done" on handhelds, spike-verify that
`std::thread`/`std::mutex`/`std::condition_variable`/`std::jthread` actually
**link and run** — not merely compile — on vitasdk, libctru (3DS), libnx
(Switch), and WUT (Wii U). This has never been exercised in this codebase (the
only prior C++ concurrency, `RenderThreadManager`, is desktop/OpenGL-only). If
any toolchain's C++20 stdlib threading support turns out to be broken or
unavailable, the fallback is a **compile-time** forced `worker_count = 1` for
that platform (which is simply the always-available inline path in
`kfx_parallel_for` — no separate code path to maintain), not blocking the
feature for every other platform.

## 3. Validation and Regression Methodology

KeeperFX already has almost exactly the oracle this plan needs, built for a
different original purpose (single-player replay/demo integrity checking) —
this is a major advantage and should be leveraged rather than rebuilt:

- `-packetsave <file>` / `-packetload <file>` (parsed in
  `src/main.cpp:3750-3768`) record and replay the exact per-turn input packet
  stream deterministically, independent of a live network connection — this
  already works in single-player today.
- `StartupParameters::packet_checksum_verify` defaults **on**
  (`src/keeperfx.hpp:125`, `src/main.cpp:1441`).
- On save, `save_packets()` computes `compute_replay_integrity()`
  (`src/packets_misc.c:192`) — a lightweight checksum over every synchronized
  thing's position/angle/owner — and stores it per turn.
- On replay, `load_packets_for_turn()` (`src/packets_misc.c:343-393`)
  recomputes `compute_replay_integrity()` against current live state and
  compares it to the recorded value (`packets_misc.c:378-392`), automatically
  logging `"PacketSave checksum - Out of sync (GameTurn %u)"` and showing an
  on-screen message the instant of first divergence. **Zero new code needed**
  for the basic pass/fail signal.

### 3.1 Recipe

1. **Record baselines.** With a pre-parallel (or `-noparallel`-forced) build,
   record `-packetsave` replays of representative levels — at minimum one
   creature/AI/pathfinding-heavy skirmish, since that stresses
   `update_things()` and (later) Ariadne hardest.
2. **Replay twice per candidate change:** once with `-packetload <file>
   -workers 1` (control — must exactly reproduce the original recording,
   itself a check that "parallel infra present but disabled" changes
   nothing) and once with `-packetload <file> -workers N` (treatment). Both
   self-report divergence via the existing checksum path.
3. **Wrap in a script**, e.g. `tools/replay_regression.sh`, running both
   invocations for a fixed turn count and failing if the treatment run logs
   any "Out of sync" the control run doesn't.
4. **Deep-diagnosis tier.** `compute_replay_integrity()` only says *that* turn
   X diverged, not *which* thing/field. The richer detail already exists in
   `net_checksums.c`'s `compute_checksums()` / `get_thing_checksum()`
   (`src/net_checksums.c:57-113,161-187`) and `update_turn_checksums()`
   (`net_checksums.c:268-377`), but is currently gated behind
   `network_is_active()` and `game.game_kind == GKind_MultiGame`, making it
   invisible during single-player replay today. The new `DFlg_LogChecksums`
   flag (§2.3) should, when set, populate/log this detail during single-player
   replay regardless of those gates — this is worth landing as part of phase
   (a) infra, since every later phase's "done" criterion depends on being able
   to root-cause a failure quickly, not just detect one.
5. **Coverage caveat.** `compute_replay_integrity()` is deliberately coarse
   (position/angle/owner only) — it will catch anything that perturbs
   movement/ordering (most bugs a scheduling change would introduce) but could
   in principle miss a divergence confined to, say, health or animation-frame
   state. Use the cheap tier for fast local iteration; require the fuller
   `net_checksums.c` tier before merging the higher-risk phases (see §4.4).
6. **Practical infra note.** No headless/dummy-video CLI mode was found in
   `main.cpp`; CI will need `Xvfb` (Linux) or a small `-nowindow`/dummy-SDL
   video driver addition to run these replays unattended. Flag as an infra
   TODO to resolve before phase (a) is exercised in CI, not a design blocker.

Every relevant Tracy zone already placed in the code — `"Sim/ThingList"`
(`src/thing_list.c:985`), `"Sim/Research"`/`"Sim/Manufacturing"`
(`src/game_loop.c:274,291`), `"AI/AllComputerPlayers"`
(`src/player_computer.c:1558`), plus the already-defined but currently unused
`KFX_COLOR_PATHFINDING`/`KFX_COLOR_LIGHTING` palette entries in
`src/kfx/profiling/KfxProfilingC.h:18,21` — gives before/after wall-clock
comparison for free; no new instrumentation is needed for *performance*
validation, only for *correctness* validation (steps above).

## 4. Phased Roadmap

### 4.1 Phase (a) — Job system + validation harness

- **Changes:** `src/kfx/jobsystem/KfxJobSystem.{h,cpp}` + `KfxJobSystemC.h`
  (new); `-workers`/`-noparallel` CLI flags threaded through
  `src/main.cpp`/`src/keeperfx.hpp`/`src/main_game.c`; new
  `DFlg_LogChecksums` flag and its single-player-replay wiring in
  `src/net_checksums.c`/`src/packets_misc.c`; `tools/replay_regression.sh`
  plus 1-2 recorded baseline replays; CMake wiring in the root
  `CMakeLists.txt` and each `build/cmake/modules/Platform*.cmake` (worker
  caps).
- **Risk:** low for the job system itself (nothing calls it yet). Main risk
  is the handheld toolchain uncertainty in §2.4.
- **Done when:** the job system builds and links on all five targets (desktop
  ×2, Vita, 3DS, Switch, Wii U) with worker count correctly capped/degrading
  to 1 on constrained platforms; `kfx_parallel_for` has a standalone
  correctness test (e.g. a sum-reduction sanity check); the replay harness
  reliably catches an intentionally-introduced ordering bug planted in
  existing sequential code — a manufactured negative test proving the oracle
  has teeth before later phases rely on it.
- **Handheld behavior:** this phase *is* the degradation mechanism — every
  later phase inherits it automatically as long as it only ever dispatches
  through `kfx_parallel_for`.

### 4.2 Phase (b) — Low-risk grid-parallelization warm-up

Two steps with different risk profiles, run in order:

**(b1) `update_animating_texture_maps()`** (`src/engine_textures.c:102-121`)
— genuinely trivial. Each loop iteration writes a disjoint output slot
(`dst[TEXTURE_BLOCKS_STAT_COUNT_A + i]`), reading only immutable-during-the-call
state (`game.texture_animation[]`). No RNG, no shared mutable accumulation.
Wrap the inner loop in `kfx_parallel_for` with no algorithmic change. This is
also outside the checksum surface entirely (purely visual, not in
`DesyncChecksums`), so it doubles as a **negative control**: the phase-(a)
harness should show zero checksum divergence before/after, proving the harness
doesn't cry wolf on a purely local, non-simulation change.
*Risk: minimal. Done when: parallelized, Tracy-timed before/after, harness
clean, survives all platforms including `-workers 1`.*

**(b2) `update_light_render_area()` / `light_render_light*()`**
(`src/light_data.c:2131-2263`, `1864-1925`) — a real proof point, **not**
safely parallel per-light. Confirmed at `src/light_data.c:1877-1879`:
`light_render_light_static()` does a read-modify-write max-combine into a
shared grid —
```c
if (lish->stat_light_map[light_map_idx] < lightness)
    lish->stat_light_map[light_map_idx] = lightness;
```
— and light radii routinely overlap the same subtiles (that's the point of
dynamic lighting), so partitioning by light index would be a genuine, silent
data race. The safe decomposition is **by output row-band**: split
`[starty, endy)` into `worker_count` contiguous bands; each worker iterates
*all* lights whose radius touches *its own* band and writes exclusively into
that band's slice of `stat_light_map`/`subtile_lightness`. Requires clipping
`light_render_light*()` to a caller-provided row range.
*Files: `src/light_data.c`. Risk: low-medium — checksum-invisible and
client-local, but correctness now depends on getting band-clipping right, a
useful rehearsal for pathfinding's shared-state hazards even though the hazard
class differs (overlapping writes here vs. scratch-buffer reentrancy there).
Validation: neither (b1) nor (b2) is covered by `DesyncChecksums`, so add a
narrow rolling hash (same `CHECKSUM_ADD` idiom as `net_checksums.c`) over
`stat_light_map[]`, dumped behind `DFlg_LogChecksums`, diffed between
`-workers 1` and `-workers N` runs — a template for any future
checksum-invisible parallel phase. Handheld behavior: degrades via the phase
(a) choke point, no separate low-core path.*

### 4.3 Phase (c) — Ariadne pathfinding reentrancy refactor (standalone, single-threaded)

Pathfinding (`src/ariadne.c` plus `ariadne_regions.c`, `ariadne_tringls.c`,
`ariadne_navitree.c`, `ariadne_naviheap.c`, `ariadne_wallhug.c`,
`ariadne_edge.c`, `ariadne_points.c`, `ariadne_findcache.c`) is the most
expensive per-tick subsystem and is called synchronously inline from
`process_creature_state()`, blocking the tick. **Confirmed by direct
inspection:** it relies on ~30 file-scope `static` mutable scratch buffers
reused across calls, e.g. `src/ariadne.c:64-88` (`wayPoints`, `ap_GPathway`,
`tree_route[TREE_ROUTE_LEN]`, `route_fwd`/`route_bak[ROUTE_LENGTH]`,
`fringe_map`), `ariadne.c:188-190` (`fwd_path`/`bak_path`/`best_path`),
`ariadne_regions.c:38-43` (`Regions[REGIONS_COUNT]`, `RegionQueue[]`),
`ariadne_navitree.c:40-45` (`Tags[]`, `tree_dad[]`, `tree_val[]`), plus
smaller state in `ariadne_naviheap.c`, `ariadne_edge.c`, `ariadne_points.c`,
`ariadne_findcache.c`. **This makes the pathfinder fundamentally
non-reentrant as written.** No RNG calls exist in the module (it is pure
navmesh geometry) — favorable for parallelization once reentrant.

**Scope-bounding finding:** the per-creature route *result* storage
(`struct Ariadne arid` inside `CreatureControl`, `src/creature_control.h:365`)
is already per-thing and safe. Only the solver's *transient workspace* is the
problem — this bounds the refactor to the ~30 buffers listed above across 9
files, not to `CreatureControl` or any `creature_states*.c` call-site
signatures.

**Approach: convert each `static` to `thread_local`, not an explicit context
struct.** This is mechanical, low-diff, and requires zero call-signature
changes across the deeply-nested call graph. On today's single caller (the
main thread), `thread_local` behaves identically to `static` — so the change
is, by construction, a no-op single-threaded, satisfying the requirement that
this phase be validated as behaviorally identical before any parallel
dispatch is added. (A bundled `struct AriadnePathfindingContext*` threaded
through every signature would be more idiomatic but a much larger, more
error-prone diff for no behavioral benefit until phase (d) needs it.)

- **Per-buffer audit** (part of "done"): classify each as (1) pure per-call
  scratch — trivially safe as `thread_local`; (2) cross-call cache/memoization
  (e.g. `find_cache` in `ariadne_findcache.c`, `LastTriangulatedMap`) — safe
  under `thread_local` but changes per-worker cache locality, needing a perf
  re-check, not a correctness concern; (3) anything secretly expected to
  persist as authoritative cross-turn state — the inventory above appears to
  contain none, but each buffer must be verified individually, not assumed.
- **Scope boundary:** navmesh triangulation build itself —
  `init_navigation()`/`update_navigation_triangulation()` (`ariadne.c:328,338`),
  run once per turn when `game.map_changed_for_navigation` is set — stays
  strictly sequential. Only "solve a route through an already-built navmesh"
  is touched by this refactor and by phase (d).
- **Risk:** low-medium. Mechanically simple per buffer, but ~30 buffers across
  9 files is real surface area; a missed one silently reintroduces a race
  that will **not** fail this phase's own single-threaded test — it will only
  surface nondeterministically once phase (d) dispatches concurrently.
  Mitigate with a scripted `grep -n "^static "` sweep across every
  `ariadne*.c` before declaring done, confirming no remaining file-scope
  mutable statics beyond intentionally-immutable lookup tables (e.g.
  `RadiusEdgeFit`, `actual_sizexy_to_nav_block_sizexy_table`).
- **Done when:** every scratch buffer is `thread_local` (or otherwise proven
  call-stack-safe), the module compiles and passes the phase-(a) replay
  harness single-threaded (no `kfx_parallel_for` calls added yet), and the
  audit trail above is documented.
- **Handheld behavior:** `thread_local` costs one TLS slot per thread that
  ever calls into Ariadne — on a 1–2-worker handheld config, 1–2 copies of a
  few hundred KB of scratch (`route_fwd`/`route_bak` alone are ~48KB each at
  `ROUTE_LENGTH≈12000` int32s). Worth a memory-budget footnote for 3DS
  (~128MB usable) but not a blocker, since worker count there is capped to 1
  by phase (a).

### 4.4 Phase (d) — Parallel path-solving (request / compute / apply)

1. **Sequential collection pass.** Walk the creature list
   (`game.thing_lists[TngList_Creatures]`, the same traversal
   `update_things_in_list()` already does at `src/thing_list.c:983-1018`) and
   snapshot which creatures have an outstanding path request this tick into a
   flat array of stable thing indices. Must be sequential and happen *before*
   dispatch, since the live linked list can be mutated by creature
   deaths/creation mid-tick.
2. **Parallel compute pass.** `kfx_parallel_for` over the snapshot array; each
   task calls the now-reentrant (phase-c) route-solving entry points for its
   own creature, reading shared but turn-stable state (the navmesh built
   earlier this turn, `thing->mappos`) and writing its result **only** into
   that creature's own `cctrl->arid` — never another creature's state, never
   the synced RNG streams (confirmed absent from this module).
3. **Sequential apply pass.** Iterate the same snapshot array **in fixed
   ascending thing-index order** to feed results into `creature_states*.c`'s
   state machine on the main thread, as before.
- **Files:** `src/thing_list.c` (snapshot-then-dispatch wrapper),
  `src/ariadne.c`/`src/thing_navigate.c` (entry points callable off-thread),
  the `creature_states*.c` call sites that currently invoke pathfinding
  synchronously inline (each needs converting from "compute now" to "read the
  already-computed result").
- **Risk: the highest in this roadmap short of the phase-(e) explorations.**
  Two hazard classes: (1) anything phase (c)'s audit missed surfaces here,
  likely only under real thread contention — the hardest kind of bug to
  reproduce, so budget ThreadSanitizer runs, not just the replay harness, on
  top of the checksum validation; (2) a subtler *semantic* risk, not a data
  race — converting synchronous-inline solving into request/compute/apply can
  change *when* a result is observed relative to other per-tick mutations
  even with zero races (e.g. a creature's target moving between "request" and
  "apply" in ways the old inline code implicitly serialized against). This
  needs case-by-case review of every converted `creature_states*.c` call
  site, not a mechanical find/replace.
- **Done when:** the phase-(a) harness, using the **full-fidelity**
  `net_checksums.c` tier (pathfinding results are covered by
  `DesyncChecksums` via `cctrl->moveto_pos` and related fields —
  `net_checksums.c:96-98`), shows zero divergence across many turns of a
  pathfinding-heavy replay comparing `-workers 1` vs `-workers N`, on top of a
  clean TSan run.
- **Handheld behavior:** at worker count 1, the collection/compute/apply
  structure still runs — the "parallel" compute pass degrades to
  `kfx_parallel_for`'s inline fallback, i.e. the *same restructured code
  path*, just single-threaded. Unlike phase (b)'s warm-ups, which fall all the
  way back to the original unmodified function, phase (d)'s single-worker path
  still exercises the new snapshot/apply structure — handhelds get real
  coverage of the refactor rather than silently reverting to pre-refactor
  code.

### 4.5 Phase (e) — Further candidates (exploratory, lower confidence)

- **Parallel read-only AI/perception scoring.** On inspection,
  `process_person_moods_and_needs()` (`src/creature_states.c:5515-5556`) is
  **not** separable as-is — it interleaves decision-making with immediate
  state mutation, and its callees (`process_creature_needs_to_eat`,
  `anger_process_creature_anger`, etc.) call the synced RNG streams inline for
  probability checks. Splitting this into a pure-scoring half and a
  mutation/RNG half is a real, separate refactor effort in its own right, not
  a drop-in `kfx_parallel_for` wrap — scope as its own future investigation.
- **Computer-player AI**, `process_computer_players2()`
  (`src/player_computer.c:1556-1583`), loops over `PLAYERS_COUNT` (typically
  ≤6 active AI players) — low parallel value at that N, and each computer
  player's turn plausibly touches shared dungeon/room/combat state affected by
  *other* players' actions (contested claims, combat), so cross-player
  independence is unverified. De-prioritize relative to creature-level work.
- **Frame pipelining**, building on the `RenderThreadManager` precedent —
  overlapping simulation of turn N+1 with drawing of turn N's already
  -snapshotted render state. Structurally the largest change in this roadmap
  (a double-buffered sim/render state split, not just a parallel-for);
  named here as a future direction, not designed further in this document.
- These are intentionally lower-confidence and are not committed milestones —
  they are listed to show where the roadmap could extend, not what comes
  immediately after phase (d).

## 5. Cross-Cutting Determinism Rules

Any parallel code added under this roadmap must follow these rules:

1. **Never call `GAME_RANDOM`/`AI_RANDOM`/`PLAYER_RANDOM`/`SOUND_RANDOM`**
   (`src/game_merge.h:58,64,66,62`) from inside a `kfx_parallel_for` callback,
   ever. These share single mutable seed fields on `game`; even behind a
   mutex, the *order* of concurrent calls depends on OS scheduling, which
   breaks lockstep reproducibility across machines by construction — locking
   doesn't fix this, it only hides the nondeterminism until a different
   machine schedules differently.
2. **`UNSYNC_RANDOM`** (`game_merge.h:60`) is documented as cosmetic/
   non-synced, so value divergence across clients is tolerated — but
   `game.unsync_random_seed` is still a single shared mutable variable, so
   unguarded concurrent writers are a plain data race (a memory-safety bug,
   not merely a determinism one). If a worker ever needs a cosmetic RNG
   stream, give it a private seed rather than sharing this field.
3. **Workers read shared state, write only to a private per-task output.** A
   parallel-for callback may read `game.*`, `things_data[]`/`cctrl_data[]`,
   map/slab arrays freely, but must write only to either (a) the
   caller-provided output slot indexed by the task's own stable index (e.g.
   `cctrl->arid` for "this creature's own route"), or (b) genuinely private
   `thread_local` scratch. Never mutate a `game` struct field or another
   entity's state directly from a worker.
4. **Apply phases iterate in a fixed, thread-assignment-independent order** —
   canonical choice: ascending thing index, matching how
   `compute_things_list_checksum()` (`net_checksums.c:151-166`) already walks
   lists. Never "whichever worker finished first" — completion order depends
   on OS scheduling/timing and would make checksums vary run-to-run on the
   very same machine, defeating both lockstep and the phase-(a) validation
   harness.
5. **Every buffer touched by parallel code must be truly private per
   worker** — `thread_local` (phase c's approach) or an explicit per-worker
   context, never an implicit "the caller handles reentrancy" assumption,
   given how much existing code (Ariadne above all) was written assuming
   exactly one call stack ever touches it.
6. **No new floating-point accumulation in anything feeding checksummed/
   authoritative state.** The engine's fixed-point `Coord3d`/`Coord2d`
   discipline (`src/globals.h:338-378`) is favorable here (no FP
   non-associativity across thread/operation-order changes) — don't regress
   it. Note, pre-existing and orthogonal to this plan: `-ffast-math` is
   already enabled on Vita/3DS/Switch/Wii U and off on desktop; new
   floating-point code in a parallelized path inherits that asymmetry and
   deserves extra scrutiny.
7. **Pre-size all new scratch/output buffers once at init**, never inside the
   per-tick hot path (e.g. size for `CREATURES_COUNT`=1024 up front) —
   allocator jitter matters more on handhelds than desktop.
8. **New Tracy zones inside parallel callbacks are fine** — precedented by
   `RenderThreadManager` already instrumenting its dedicated thread — no need
   to second-guess instrumenting worker code.

## 6. Open Risks and Questions

- **Handheld threading-stack availability is unverified** (§2.4) — the
  single biggest unknown in this plan and the reason phase (a) includes an
  explicit spike rather than assuming success.
- **Tracy overhead on handhelds.** Desktop builds set `TRACY_ENABLE`/
  `TRACY_ON_DEMAND` (`build/cmake/modules/Dependencies.cmake:109-119`);
  confirm whether Tracy is compiled out entirely for handheld builds
  (`Dependencies.cmake:123-128` suggests a different dependency path there).
  If zones remain compiled in, their per-call overhead inside a tight
  `kfx_parallel_for` loop (e.g. phase b1) should be benchmarked on real
  hardware, not assumed negligible.
- **`-ffast-math` desktop/handheld asymmetry** is pre-existing and orthogonal
  to this plan, noted here only as a footnote for anyone reasoning about
  cross-platform-family lockstep in the future.
- **No headless/dummy-video CLI mode exists today** — the replay regression
  harness (§3) needs `Xvfb` or an added dummy-video mode to run unattended in
  CI; resolve before automating phase (a)'s harness.
- **Phase (c)'s audit is only as good as its exhaustiveness** — a missed
  `static` buffer won't fail phase (c)'s own single-threaded validation, only
  phase (d)'s concurrent one, and only nondeterministically. The scripted
  `grep -n "^static "` sweep is a hard exit criterion, not optional.
- **Phase (d)'s subtler risk is semantic, not just data-race** — decoupling
  request from apply can change observation timing relative to other per-tick
  state changes even with a clean TSan run; each converted call site needs
  individual review.
- **`compute_replay_integrity()`'s coverage gap** (position/angle/owner only)
  means the fast harness tier could miss a divergence confined to
  non-positional state; the full `net_checksums.c` tier must be mandatory
  before merging phase (d), not just used as phase (b)/(c)'s spot-check.

## 7. Critical Files Reference

| Area | File(s) |
|---|---|
| C-linkage shim precedent | `src/kfx/profiling/KfxProfilingC.h` |
| Existing threading precedent | `src/renderer/RenderThreadManager.h`, `.cpp` |
| Creature/AI/pathfinding dispatch | `src/thing_list.c` (`update_things_in_list()`, `update_things()`, lines ~983-1145) |
| Pathfinding scratch state to convert | `src/ariadne.c`, `ariadne_regions.c`, `ariadne_navitree.c`, `ariadne_naviheap.c`, `ariadne_edge.c`, `ariadne_points.c`, `ariadne_findcache.c` |
| Determinism/replay validation oracle | `src/net_checksums.c`, `src/packets_misc.c` (`compute_replay_integrity()`, `packet_checksum_verify`, `update_turn_checksums()`) |
| Lighting warm-up target | `src/light_data.c` (`light_render_area()`, `light_render_light_static()`, ~lines 1864-1925, 2131-2263) |
| Texture-animation warm-up target | `src/engine_textures.c` (`update_animating_texture_maps()`, ~lines 102-121) |
| Handheld build/thread-cap configuration | `build/cmake/modules/Helpers.cmake`, `PlatformVita.cmake`, `Platform3DS.cmake`, `PlatformSwitch.cmake`, `PlatformWiiU.cmake` |
