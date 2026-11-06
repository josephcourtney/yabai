# DESIGN — Intent-driven window organization

**Status:** Proposed architecture; not implemented.  
**Date:** 2026-11-06  
**Scope:** Interactive organization and its boundary with yabai's authoritative window management.  
**Implementation sequence:** See [PLAN.md](PLAN.md). The native-tab and BSP-resize integration work comes **before** this redesign.

## 1. Executive decision

Separate **specifying a window arrangement** from **applying the arrangement to macOS**.

During an explicit organization gesture (initially the existing modifier-drag), the interactive subsystem captures input, selects targets, calculates a *provisional* layout, and displays temporary overlays. It does **not** move, resize, reassign, raise, or focus real application windows merely because the pointer moved.

On commit (normally mouse release), it submits a **semantic layout intent** to yabai. Yabai validates the intent against current window, Space, display, and layout state; applies the necessary changes; then observes and reconciles the resulting macOS state. On cancel, nothing has to be undone, because the preview was never authoritative.

The architectural boundary is semantic rather than a mandatory process boundary: an interaction frontend may initially run within the daemon while exposing a stable API that permits extracting it into a separate process later.

### Why

In current yabai:

- `src/mouse_handler.c` intercepts modifier mouse-down/up events and posts drag events. The existing implementation can forward drag events even after consuming the initial press.
- `src/event_loop.c::MOUSE_DRAGGED` moves the **actual** window on each drag sample (`scripting_addition_move_window`, falling back to `window_manager_move_window`) or applies actual window/BSP resizing.
- The same event path selects a destination from the cursor, the real window frames, and an insertion-feedback window attached to a BSP node.
- `MOUSE_UP` can then swap, warp, stack, or retile on a different Space, involving further real window and layout changes.

This creates multiple authorities acting on the same physical geometry while the gesture is in progress: pointer input, yabai, and macOS's native window/Space transfer behavior. The reported cross-display flashing and bounce-back are consistent with this coupling, **but the exact causal sequence has not yet been instrumented and proven**. This design removes the feedback path whether or not that particular explanation is complete.

### Goals

1. A modifier-drag expresses an intended operation without changing actual windows before commit.
2. Gestures remain predictable across displays and Spaces, including differing coordinate systems and screen geometries.
3. Preview and commit use **the same layout semantics**, so the preview accurately describes the intended result.
4. Window changes are deterministic, bounded, observable, and recoverable after interruption or partial OS failure.
5. Native macOS tab groups are logical layout items; one selected tab represents a group in a tile.
6. The same intent API supports pointer interaction, future keyboard interfaces, scripting, and tests.
7. The layout planner is deterministic and independently testable without macOS frameworks.
8. Unmodified native macOS dragging, clicks, and application behavior remain intact.
9. Interaction presentation is functional feedback only. It does not impose focus rings, decorative borders, easing, fades, or animations on the core.
10. Existing supported layout, rule, focus, Space, and daemon-IPC behavior remains compatible during migration.

### Non-goals

- Replacing macOS's window server or taking over **unmodified** native window dragging.
- Imposing an application-wide overlay or hiding real windows during ordinary work.
- Promising atomic cross-Space operations; Apple exposes asynchronous operations without general rollback.
- Replacing all existing yabai commands immediately.
- Integrating the jpatrolla focus ring, custom Space-animation subsystem, or rendering engine.
- Combining the architectural rewrite with native-tab or live-resize fork patches; those first receive their own acceptance gates.
- Making overlay artwork, animation cadence, or cursor decoration authoritative state.

## 2. Architectural boundaries

~~~text
External events: AX, WindowServer, Spaces, displays, process lifecycle
                        |
                        v
              Observation / reconciliation
                        |
                        v
                Authoritative state
            (windows, groups, Spaces, BSP)
                        |
       snapshot + revision / capability data
                        |
                        v
Input capture ---> Interaction session ---> Pure intent/layout planner
                        |                       |
                        |                       v
                        +----------------> Proposed arrangement
                                                |
                                                v
                                    Functional overlay presenter
                                    (noninteractive preview)
                                                |
                                     mouse-up / explicit accept
                                                |
                                                v
                                     Transaction coordinator
                                    prepare -> apply -> settle
                                                |
                                                v
                                Existing yabai window/Space executor
                                                |
                                                v
                                       macOS operations
                                                |
                                                +----> observations above
~~~

The **authoritative model** and **transaction executor** are part of the window manager. The **interaction controller** and **overlay presenter** are clients of that model. The **pure planner** is a shared, OS-independent component.

A physically separate overlay process is a possible deployment decision, not a prerequisite for semantic isolation. If split out, it communicates via a local, versioned, access-controlled protocol; only the yabai daemon may mutate authoritative layout or issue macOS window-management operations.

### Module responsibilities

| Module | Owns | Must not own |
| --- | --- | --- |
| Observation adapter | AX/WindowServer/Space/display reports; deduplication; reconciliation | User intent, overlay drawing |
| Authoritative model | Current logical window/tab-group membership, tree, focused item, topology and revisions | Provisional gestures |
| Snapshot adapter | Immutable, normalized, versioned view of relevant model state | OS mutation |
| Pure planner | Intent validation, target placement, prospective trees and frames, required effects | WindowServer calls, AX calls, pointer-event handling |
| Interaction controller | Complete claimed gesture, pointer/display targeting, modifier modes, cancel/accept | Changes to actual app windows |
| Overlay presenter | Draw prospective nodes, insertion/drop zones, proposed frames; remove on cancel/commit | Input hit testing, actual window frames, focus |
| Transaction coordinator | Version checks, serialization, effect plan, deadlines, completion/failure reporting | Overlay styling |
| OS executor | Explicit Space membership, layer, tiling, geometry, ordering and focus operations | Deciding what gesture means |
| Reconciler | Match requested to observed outcomes; converge or report bounded partial failure | Inventing successful results |

The planner and transaction coordinator should be invoked through explicit interfaces rather than directly editing `struct view`/`struct window_node` from event handlers.

## 3. Domain model and identity

### Authoritative layout items

A **layout item** is the unit placed in a BSP leaf, stack slot, or floating set:

- A single independently managed macOS window; or
- A **native macOS tab group** represented by one logical item with multiple member windows, exactly one selected/visible member where macOS exposes such state.

A Zen/Firefox *browser tab* is not a native macOS tab-group member until it becomes a separate macOS window; Zen's delayed AX discovery remains a separate reconciliation responsibility.

Do not equate a transient WindowServer window ID with a persistent layout-item ID. A window reference contains an observed window ID, owning application/process identity, and an observation-generation token. A group is assigned a separate in-memory identity, updated through tab membership/selection events. A group can change its selected WindowServer ID without changing its tile identity.

**Group invariants:**

- Every managed logical item has at most one authoritative placement.
- Only the currently selected member of a native tab group represents its tile; hidden members never gain independent tiles or control focus/Space selection through stale WindowServer locations.
- Group moves, swaps, stacks and floats operate on the group as a unit.
- Tab detach creates a new item at a defined lifecycle transition; tab merge eliminates the redundant item without duplicating a tile.
- Hidden member lifecycle, selected-tab opacity, insertion point, and floating/zoom state remain coherent.
- A group cannot exceed the applicable layout/stack capacity; full-stack behavior must be safe and specified.

### Snapshots and revisions

A `LayoutSnapshot` contains:

- `revision`: monotonically increasing authoritative layout/model revision.
- `topology_epoch`: increments when displays, Space IDs, arrangement, scale or relevant coordinate mappings change.
- Stable *within the observed session* Space IDs and display IDs, with layout type, dimensions, work areas, padding and gaps.
- Logical item IDs, concrete current member window IDs, membership, focus/float/minimized/fullscreen flags, rules and capabilities.
- BSP/stack structure, split orientation, ratios, order, zoom/insertion state and capacity limits.
- Geometry in a **defined global logical-point coordinate system**, with display transforms explicitly available.

Never persist or use historical Space indices/window IDs as safe operation targets without current validation. Session restoration remains a distinct feature with its own matching rules; title-only or app-only matching is not a substitute for current identity.

A snapshot is immutable while planning. Runtime window events continue to update the live model; the session decides whether to refresh, replan, or invalidate its preview.

## 4. Intents, plans and effects

### Semantic intents

Initial supported operations:

- `MoveToSpace(item, destination_space, placement)`
- `InsertRelative(item, target, side, split_orientation)`
- `Swap(item_a, item_b)`
- `Stack(item, target, position)`
- `SetFloating(item, float_state)`
- `AdjustSplit(node, ratio)` or `ResizeLayoutBoundary(boundary, position)`
- `Cancel` / `Accept` as interaction outcomes, *not* macOS window commands.

Placement targets are logical items/nodes/Spaces rather than unverified frames. Target resolution uses the **snapshot's prospective geometry** and the actual pointer coordinate; it must not depend on the moving real window's changing frame or presumed current display.

Future input modes may choose the same intents by keyboard or programmatic API.

### Proposed API (design contract; not existing CLI syntax)

~~~json
{
  "protocol_version": 1,
  "request_id": "gesture-opaque-id",
  "base_revision": 42,
  "topology_epoch": 7,
  "intent": {
    "type": "insert_relative",
    "item_id": "item-A",
    "destination_space_id": 366,
    "target_item_id": "item-B",
    "side": "left",
    "orientation": "horizontal"
  }
}
~~~

IDs above are examples only. Serialized identifiers are process/session-scoped tokens, not permanent file-format guarantees.

The planner returns a `LayoutProposal` containing the prospective trees and projected frames for all affected items, its source revision, destination and capability requirements, a deterministic effect plan, any rejected operation reason, and a stable proposal identity. The frontend can use this to display the **entire outcome**, not merely a target rectangle.

An effect plan records intended membership changes, logical tree changes, geometry and layer changes, ordering and focus changes, and dependencies between them. Planning itself never invokes Accessibility or WindowServer mutations.

### Plan invariants

1. Every affected item appears once in the appropriate prospective tree/floating set.
2. No duplicated native-tab member becomes a second tile.
3. Target nodes and Spaces exist in the snapshot; unsupported layout types or unreachable targets are rejected explicitly.
4. Ratios, dimensions, constraints and stack sizes respect the layout's defined limits.
5. Equivalent snapshot + intent produces an equivalent proposal.
6. Invalid intents yield errors and **no** mutations.
7. Current native-tab and BSP semantics remain preserved whether the intent came from mouse, keyboard or CLI.
8. The displayed proposal is derived from the planner's result rather than a separate approximate layout algorithm.

## 5. Interaction session

The controller has explicit states:

~~~text
IDLE
  -> PENDING (modifier press, window hit, preserve original click)
  -> CLAIMED (movement exceeds drag threshold; own gesture)
  -> PREVIEWING (refresh candidate target/proposal)
  -> COMMITTING (release with valid proposal; session no longer accepts drag)
  -> DONE
or
  -> CANCELLED (escape, invalid input, topology conflict, app termination,
                focus/mission-control interruption, lost tap or timeout)
  -> IDLE
~~~

### Event ownership

At gesture begin, the event tap records a candidate source window/item and pointer origin. Until drag classification it must preserve ordinary click semantics, including mouse-up, double click, keyboard modifiers, and right-click/context-menu behavior. The current delayed click replay is an implementation detail to test carefully; it is not assumed safe for every click sequence.

**Once claimed, the controller must consistently consume the entire intended modifier-drag stream, not just mouse-down/up while forwarding intermediate dragged events to macOS.** The drag does not call `window_manager_move_window`, `scripting_addition_move_window` or native frame/Space setters. The cursor itself may cross displays; its location is not derived from the moved window.

If the event tap times out, loses permission or is disabled, tear down the overlay and abort the gesture. Never synthesize an unpaired button release, leave a stuck modifier state, or silently switch to real-window dragging halfway through.

Unmodified drag is untouched: macOS continues to move ordinary windows natively, with yabai handling observation and post-drag reconciliation independently. Existing interactive BSP resize remains available during migration as an explicitly separate mode.

### Targeting

- Use the cursor's current global position to identify display and **visible destination Space** from the snapshot; do not infer from the source window's position.
- Compute zones over the prospective layout of the hovered destination: edges for insertion, center for swap/stack, background for well-defined empty-Space or no-target behavior.
- Use a dead zone/hysteresis to avoid preview oscillation at narrow boundaries; cursor never moves because a preview moves.
- Provide explicit invalid-target feedback (unsupported Space, fullscreen, inaccessible window, full stack, unavailable OS capability).
- For multi-display topology changes during a gesture, invalidate the proposal and cancel or take a fresh snapshot. First implementation should **cancel** rather than silently retargeting.
- Avoid target selection through the overlay itself; overlays must be noninteractive and excluded from yabai-managed windows and hit-testing.

### Functional overlays

Render, where useful:

- A translucent or outlined **proxy** for the selected logical item near the pointer.
- Proposed tile/stack outlines, affected neighbors' prospective frames and insertion zones on the destination display.
- Distinct valid/invalid/ambiguous target states and a clear cancel affordance.

The original application windows remain stationary. The overlay cannot steal focus, join a managed BSP tree, intercept clicks, create persistent app windows, or trigger window reconciliation as an application to manage. It should respect display bounds, scale factors, work-area insets, clipping, and changing display arrangements.

These overlays express *operation meaning*, not persistent decoration. The preview must remain useful with all animation, transparency and extra effects disabled; accessibility and high-contrast visibility are desirable.

## 6. Commit coordination and OS reality

A transaction is **logically validated**, not claimed to be an atomic macOS operation.

### Phase 1 — Prepare

- Resolve the request's item/group and Space identities against the **current** model.
- Validate `base_revision`, `topology_epoch`, group membership, layout constraints, window liveness, permission/SA capabilities, and target visibility.
- Compute or revalidate the proposal against a coherent current snapshot.
- Serialize conflicting operations on affected logical items and Spaces; no other gesture can mutate these through a second coordinator concurrently.
- Fail closed on stale or ambiguous operations. Initial version rejects all relevant revision conflicts; later versions may safely rebase explicitly.

### Phase 2 — Apply

Use the existing yabai execution primitives through a bounded operation plan, for example: reconcile source membership -> request destination Space membership -> await required confirmation -> update logical placement -> apply layout/frame/layer/order/focus operations -> settle.

The exact order depends on operation and macOS capability. A cross-Space change cannot assume that an asynchronous OS move has completed merely because a request returned. All effect steps have explicit success/failure classification and deadlines.

The coordinator tracks expected OS notifications and correlates observations to the pending operation using IDs, ordering, desired effects and a transaction journal. macOS may emit no transaction token; do **not** blindly suppress unrelated AX/WindowServer events during a transaction. Unexpected observations can invalidate a plan or require reconciliation.

During a commit, the interaction preview is gone or marked noninteractive; the initiating gesture no longer drives geometry.

### Phase 3 — Settle

- Query authoritative observations when the required move/geometry/focus notifications arrive or a deadline expires.
- Verify actual window Space membership, unique tree membership, resulting geometry within tolerance, stacking/layer state and focus as applicable.
- Reconcile authoritative state to *observed macOS reality*. A missing or failed OS effect cannot be reported as success.
- Report `committed`, `rejected`, `failed` or `partial` with reasons and observed state; bound all waits and retries.
- Attempt a safe compensating action only when its preconditions still hold; otherwise preserve actual state and surface a partial-failure result. A general cross-Space rollback is **not promised**.

Animation, if ever offered externally, receives proposed and observed transitions but cannot block, cancel or reorder the authoritative commit.

## 7. Safety and failure handling

**Required invariants:**

- **No preview mutation:** prior to accept, window frames, Space assignments, focus, layers, tree membership and macOS ownership are unchanged *by the preview controller*.
- **Single mutation authority:** only yabai's executor initiates managed window/Space changes.
- **One logical placement:** no managed item/group is duplicated across trees after reconciliation.
- **No stale-frame targeting:** pointer targeting uses cursor and snapshot; not source-window position sampled mid-drag.
- **Cancellation purity:** cancel leaves no preview-originated mutations, no overlay, no captured input state.
- **Bounded operation:** all asynchronous steps complete, time out or return a classified partial failure.
- **Correctness independent of animation:** final intended state does not depend on easing, preview frame rate, opacity or whether animations are available.
- **No blocked input:** no synchronous AX/WindowServer RPC or expensive layout render on the event-tap callback thread.
- **No silent overflow:** stack/node counts and notification-subscription capacity are checked.
- **Platform capability gating:** unsupported SIP/SA/macOS combinations return explicit limits; do not change SIP, Dock or user settings to continue.
- **Provenance:** each live regression records binary commit, signature, current daemon executable, OS build and SA state when relevant.

**Interruption policies:** Escape cancels; source/target termination cancels; sleep/lock or event-tap loss cancels; a display/Space topology epoch change cancels; Mission Control entry cancels (until a deliberate MC interaction design exists). On daemon restart, active preview state is discarded. If the daemon dies mid-commit, restart reconciliation rebuilds from observed state; it must not blindly replay old commands.

The earlier WindowServer invalid-window log bursts are not proof of a specific fault. Operational acceptance uses reproducible observable failures and targeted diagnostics, not error counts alone.

## 8. IPC and process boundary

Implement planner and coordinator interfaces within yabai initially to minimize deployment risk. Define a versioned request/response protocol that is transport independent. A future separate frontend may use a user-owned Unix-domain socket with appropriately restrictive permissions and authentication of the local peer where available.

Suggested interface families:

- `snapshot.read(scope) -> LayoutSnapshot`
- `plan.evaluate(snapshot_revision, intent) -> Proposal | Rejection`
- `commit.submit(proposal_id, expected_revision) -> TransactionId | Rejection`
- `transaction.observe(transaction_id) -> Status / observed effects`
- `interaction.cancel(session_id)` (frontend-only; no layout side effect)

The names are **illustrative contracts**, not current executable flags. Prefer typed result codes and schema versions to parsing human-readable command output.

Snapshots should expose no unnecessary window titles or user data to overlay clients. Apply rate limiting, stale-request rejection and bounded memory/resource usage. Keep the interaction process optional and ensure the daemon remains useful when the frontend exits or crashes.

## 9. Compatibility with existing work

### Native macOS tabs

Complete and qualify the Christian-SC26 tab-group fixes **before** designing a new layout item's implementation. Native groups are the core layout identity. Selected-tab changes may change concrete window IDs without changing the logical layout item or consuming a new slot.

### Live BSP resizing

Retain the current PR #5 live resizing and integrate subsequent Christian-SC26 mouse-up and responsiveness fixes **before** this redesign. Then introduce preview-only `AdjustSplit` using the same pure planner. Live and preview-only implementations should converge on identical final layout outcomes. During migration, existing live-resize behavior remains available and separately tested.

### Window reconciliation and persistence

Keep Zen-only AX reconciliation and existing manual refresh. Session persistence currently uses strict app/title matching and has outstanding restore-acceptance gaps; the design's live in-memory item IDs are **not** a license to persist unstable WindowServer IDs. Explicit future schema migration/identity rules are required.

### Other functionality

Continue supporting focus-follows-mouse and mouse-follows-focus without unsolicited warping during preview. Preserve existing CLI, signal semantics, rule evaluation and Space layout behavior. Do not introduce a mandatory scripting-addition dependency for overlay preview.

## 10. Testing and qualification contract

### Pure, platform-independent tests

- Enumerate BSP tree transforms, split ratios, stack sizes/boundaries, swapping, insertion and removal, multi-Space proposals and no-target outcomes.
- Property-based checks: no duplicate membership, preservation of unaffected nodes, valid regions, deterministic intent results.
- Native-tab group model: group switch, merge/detach, hidden-member stale Space, one tile, floating/zoom/opacity state, limit handling.
- Gesture state machine: click vs drag, all cancel paths, event reorder/coalescing, stale pointer updates, no mutation calls before accept.
- Fake asynchronous executor: delayed/out-of-order/missing events, failed moves, partial results, replan/rejection and cleanup.
- Overlay hit-testing exclusions and coordinate transforms with multiple displays and differing scales.

### macOS acceptance (human-controlled, isolated)

Use a clean build and the existing `just test-local`, `just build`, `just qualify-preflight` and `just qualify-running` gates, followed by a signed foreground daemon with inert startup configuration. Check SA explicitly only for features requiring it. Test only disposable windows/Spaces, never use historical Space/window IDs as current targets. Do not automatically install/reload SA, restart Dock, modify SIP or start/stop the normal installed service.

The decisive cross-display regression:

1. Start with a known tiled window on display A and a valid target on display B.
2. Begin claimed modifier-drag and move the pointer repeatedly across the display boundary and over multiple target zones.
3. While the button is held, verify the real source window's frame/Space membership **never changes due to preview**, and neither does the target's actual layout.
4. Cancel: verify no geometry, Space, focus or membership change attributable to the preview.
5. Repeat and accept: exactly one logical intent is committed; source moves once to the intended destination, settles correctly, and neither bounces back nor creates duplicate managed entries.
6. Test delayed AX notifications, missing window, display disconnect, tab selection during preview, release over no target, Mission Control interruption and failed Space transfer.

Additional scenarios: single display, three displays, differing backing scales, BSP versus stack versus float, native tabs, Zen tab detach, rapid repeated drags, FFM/MFF enabled and disabled, keyboard events, click/double-click, right-click, gesture cancellation, daemon stop and sleep/wake. Measure UI preview latency and resource consumption relative to a recorded baseline; do not claim performance from source inspection alone.

Tests should differentiate **no window movement during preview**, **the intended final placement**, and **error-free cleanup**. A successful CLI response alone is not runtime acceptance.

## 11. Development boundaries

- The standalone feature integrations described in [PLAN.md](PLAN.md) finish before implementation begins.
- Do not bring decoration, focus rings, custom animations or wallpaper effects into the core.
- Avoid opportunistic redesigns of unrelated focus/SA logic while adding the planner.
- Develop in narrow reversible branches with independent acceptance gates; preserve the known-good qualification branch.
- No GitHub Actions; existing local build/signing and isolated macOS tests remain authoritative.
- Document and explicitly test each capability unsupported by the running macOS version, Dock payload or SIP configuration.

## 12. Open design decisions (to validate during prototyping)

1. Exact coordinate transforms and overlay technology for multi-display, mixed-scale desktop arrangements.
2. Which transient OS changes (e.g., focus changes) must invalidate a preview versus permit safe revalidation.
3. Whether transactional snapshots must include all Spaces or only affected Spaces plus topology metadata.
4. Appropriate behavior when the pointer is over an empty Space, fullscreen window, unmanageable app, or a stack at capacity.
5. How to expose preview-only resizing without unexpectedly changing existing live-resize configuration.
6. Whether the frontend remains an in-process module after initial acceptance or becomes a separate executable, and how the IPC protocol is versioned.
7. How to reconcile current layout save/restore with logical tab-group identities and session-stable item IDs.
8. How to serialize simultaneous layout commands, rule events and gestures without introducing a global event-loop stall.

These are explicit engineering decisions to resolve with controlled tests; they do not change the foundational separation of preview from commit.
