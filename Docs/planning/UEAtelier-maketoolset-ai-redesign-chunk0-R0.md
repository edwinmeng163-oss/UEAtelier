# Make Tool Set AI-Redesign — Chunk 0 R0: `call_tool` approval-gated real-write

_Status: **DRAFT — awaiting director sign-off.** PM-authored (design only; nothing dispatched to Codex). Produced from a code-grounded design + 5-lens adversarial red-team, then PM-calibrated (see "PM calibration note" in §1)._

_Date: 2026-07-01. Target branch: **TBD — see Open Decision #1** (recommended: a main-derived v0.34 line; NOT `experiment/v0.33-ue58-validation`)._

---

## ⭐ RESOLVED MODEL (2026-07-02, director) — GOVERNS this doc; supersedes the runtime-grant mechanism below

The director resolved the core safety-model fork. **This block is authoritative** where it conflicts with sections drafted under the earlier per-run human-approval model (notably §4.2 grant, §5.1 provenance, §5.3 depth-block-glue). Those sections are **superseded**; read "grant / approval / in-editor provenance" there as "**vetted marker + vetted-execution scope**."

**Gate = authoring time, not runtime.** A **non-scaffold** (AI-authored composite/glue) toolset that passes the **AI check** — **both** an **AI safety-review pass** *and* a **build + smoke of the generated tool** — plus the director's **one-time proposal approval**, earns **standing authority** to bypass the `call_tool` wall at runtime. Real writes are permitted for **ALL** dangerous buckets, **including raw `dangerous_no_dryrun` scene/exec** (`spawn_*`, `batch_*`, `editor_set_map_game_mode`, `execute_python`, `execute_console_command`). **No per-run human confirm.**

**Mechanism (replaces §4.2's per-run `FGuid` grant):** a persisted **vetted trust marker** on the `user.<toolId>` toolset (in its user-registry manifest), recording the AI-review verdict, the smoke result, the approver/timestamp, and an **integrity hash of the generated `main.py`**. At runtime, executing a vetted toolset pushes a **vetted-execution scope**; `GatherFacts` sets a new classifier fact `bInVettedToolsetContext=true` **only** when the running toolset's live `main.py` hash matches its marker; the pure classifier then returns a new decision **`AllowVettedReal`** for dangerous tools inside `if (bDangerous)`. (The per-run `GrantId`/provenance thread-local from §4.2 is **not** built.)

**PM-mandated structural invariants (NON-NEGOTIABLE — these make standing authority safe rather than a blank check):**
1. **No-direct-mutation validator STILL runs** at authoring (director did not list it as an "AI check," but it is a *structural* gate, not a judgment gate). Without it a vetted tool could `import` raw mutation APIs and skip `call_tool` → zero policy, zero audit. It is what keeps every vetted write inside the audited `call_tool` path.
2. **Vetted-marker integrity:** the marker is bound to a SHA-256 of the generated `main.py`. Any edit to the tool's code → hash mismatch → `bInVettedToolsetContext=false` → the wall re-applies. Tampering cannot ride a stale marker.
3. **Every real write audited, fail-closed** at the `call_tool` chokepoint (§4.4) — the primary runtime accountability now that there is no per-write human.
4. **The four HARD denies are unchanged and un-overridable** (`not_visible`, `user_tool_forbidden`, `call_tool_depth_exceeded`, `workflow_run_forbidden`). A vetted toolset still cannot call another user tool, recurse via `call_tool` beyond depth 0, or invoke `workflow_run`. `AllowVettedReal` is reachable **only** inside the `bDangerous` block, below those guards.
5. **Delegate-through-`call_tool`** for every side-effect (enforced by #1).

**§5.3 note:** the earlier "increment `FScopedCallToolDepth` around the composite executor to block AI glue" is **dropped** — in this model the vetted toolset's own first-level `call_tool` steps (at `Depth==0`) are *supposed* to run for real; only deeper nesting (`Depth>=1`) stays denied. The glue-interleave concern is accepted as covered by the authoring-time AI check + audit, not by a runtime binding.

**Residual risks the director explicitly accepted:** (a) the AI safety-review is a fallible judge (prompt-injectable / can be wrong) — mitigated but not eliminated by the structural validator + build/smoke backstop; (b) runtime **glue** can compute args that differ from what the AI check saw, so "checked once" ≠ "each write vetted"; (c) raw scene/exec writes remain **irreversible** (no rollback) until a later pre-write-snapshot chunk — accepted for now.

**Decisions now settled:** Open Decision **#3** = approval-eligible for ALL buckets (all dangerous tools). Open Decisions **#4/#5** (per-step granularity / grant TTL) are **moot** (no per-run grants). Open Decision **#1** (target branch) and **#2** (wire hardening) and **#6** (official-`:8000` delegation validator) remain open.

---

## 0. Where this sits

This is **Chunk 0** of the larger Make Tool Set AI-redesign (AI-authored, human-approved reusable tools). Chunk 0 is the *foundation*: it evolves the v0.27 `call_tool` force-dry-run safety wall into a narrow, human-gated **approval real-write path**, so that a reviewed composite can execute its dangerous steps for real. Chunks 1–4 (structured-proposal injection, review UI, Python generation, run-time approval) all depend on this seam and are sketched in §6.

Locked director decisions carried in: (1) approval model = **AI proposes → human confirms in-editor → generate/execute**; (2) the AI may add lightweight glue logic. Three structural gates are preserved throughout: every side-effect delegates through `call_tool` (policy), the no-direct-mutation validator, and the human approval gate.

## 1. Context & the v0.27 wall

Make Tool Set (Task Atlas composite authoring) is blocked by the v0.27 `call_tool` safety wall. Every side-effecting step an AI-authored composite issues routes through `builtins.call_tool` → `UUnrealMcpCallToolLibrary::CallTool` ([`UnrealMcpCallToolLibrary.cpp:140`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolLibrary.cpp:140)), which calls `ClassifyCallToolTarget_Pure` ([`UnrealMcpCallToolPolicy.cpp:24`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolPolicy.cpp:24)) and then, per the decision, either injects `dryRun:true` or hard-denies before dispatch.

`ClassifyCallToolTarget_Pure` sorts targets into two *dangerous* buckets (a tool is dangerous when `RiskLevel >= High` OR `bRequiresLock/Write/Restart/ExternalProcess/Build`):

- **ForceDryRun** — `bDangerous && bDryRunSupport`. The classifier forces `dryRun:true` (injected at [`UnrealMcpCallToolLibrary.cpp:160-163`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolLibrary.cpp:160)). The write is *previewable but never real* through `call_tool`.
- **`dangerous_no_dryrun`** — `bDangerous && !bDryRunSupport` (scene/write tools: `spawn_actor`, `batch_configure_static_mesh_actors`, `editor_set_map_game_mode`, `execute_python`, `execute_console_command`). These are **hard-DENIED**, not force-dry-run'd, precisely *because* they are neither previewable nor reversible — there is no `dryRun` semantics to fall back to, so the only safe classifier verdict today is Deny.

> **The memory understated this.** The scene/write tools that block Make Tool Set are in the **Deny** bucket (`dryRunSupport=false`), **not** the ForceDryRun bucket. So an honest Chunk 0 cannot just "skip the dry-run injection" — it must convert *both* buckets, and the harder bucket has no rollback artifact today (see §5.5 / Open Decision #3).

Downstream consequences:

- Task Atlas classification ([`UnrealMcpTaskAtlasService.cpp` `ClassifyTask`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpTaskAtlasService.cpp)) runs `ClassifyCallToolTarget_Pure` per step and marks a row **Blocked** iff any step Denies for a reason `!= not_visible`.
- The Make Tool Set button is hard-greyed on `Row.Eligibility != Blocked`, and the confirm handler hard-wires `bForceWriteEvenIfBlocked=false` ([`STaskAtlasWindow.cpp`](Plugins/UnrealMcp/Source/UnrealMcp/Private/STaskAtlasWindow.cpp)).

**Why direct-chat AI writes succeed but composed tools cannot — the trust boundary.** The in-editor AI, calling a tool *directly* over the `:8765` MCP wire, does real writes today: the wire executes `ExecuteTool` with the client's args, and write handlers default `dryRun=false`. The `call_tool` re-entrancy library is a *different* path — the one an AI-authored composite (a `user.<toolId>` UserRegistry tool) uses to invoke steps *programmatically*, with no per-write human in the loop. v0.27 deliberately clamped **that** path shut for dangerous tools. The director's model reopens it *narrowly*: **AI proposes → a human confirms the exact writes in-editor → the composite executes those writes for real.** Chunk 0 opens that narrow, human-gated door through the `call_tool` chokepoint without weakening any structural guard.

> ### PM calibration note (correction to the raw red-team output)
> The adversarial red-team (correctly) observed that the **external `:8765`/`:8000` wire ingress bypasses the classifier entirely**: `HandleToolsCall` ([`UnrealMcpProtocol.cpp:394`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpProtocol.cpp:394)) calls `ExecuteTool` **directly** ([`:413`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpProtocol.cpp:413)) — no classify, no dry-run injection — and `ExecuteToolFromEditorUI` is literally `return ExecuteTool(...)` ([`UnrealMcpToolDispatcher.cpp:957-960`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolDispatcher.cpp:957)), i.e. the **same executor** the `call_tool` library uses. From this the red-team concluded Chunk 0 *must* route the external ingress through the classifier, or "approval is the only way `dangerous_no_dryrun` writes" is false.
>
> **PM verdict: that conclusion is rejected as a Chunk 0 requirement.** It conflates *intended* behavior with a hole. The `:8765` server is the product's whole point — it exists so the local trusted AI client (Claude/Codex in the IDE) can drive Unreal. Its access control is **localhost binding** ([`UnrealMcpProtocol.cpp:155/158/181` — `127.0.0.1` only](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpProtocol.cpp:155)) + `ValidateOrigin` + an **optional** bearer (`ValidateAuthorization` returns true when `AuthToken` is empty, [`:531`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpProtocol.cpp:531)). The security boundary is *the local machine*, the standard MCP-local-server model. Real writes over that wire are **by design**; force-dry-run'ing them through the classifier would break the core product.
>
> What actually matters for Chunk 0 is narrower and *is* fully addressed: because the wire and the in-editor path share one executor, the approval grant **must not** rely on thread/executor identity to tell "the human clicked" from "a wire client called." It relies instead on two things a wire client can never supply — a **server-minted `FGuid` grant handed in-process** and an **in-editor-provenance flag set only inside the Slate mint→execute critical section** (§4.2, §5.1). Those close the "external client rides the human's grant" vector *without* re-gating the wire. The wire's optional-bearer / origin-absent-passes posture is a **separate, pre-existing** hardening topic (Open Decision #2), explicitly **out of Chunk 0 scope**.

## 2. Goal & non-goals

**Goal.** Convert both dangerous buckets from "always dry-run / always deny" into "real write **only** when a valid, in-editor-minted, single-use approval grant is presented and structurally bound to (a) this exact resolved-handler + canonical args, (b) this run, (c) an in-editor call-provenance, and (d) an ordered step index" — while keeping the four structural HARD denies permanently un-overridable at **both** enforcement sites (the pure classifier **and** the composite-write gate). Keep `ClassifyCallToolTarget_Pure` a pure function; keep the policy matrix testable; ship audit + rollback at the write chokepoint so every approved real write is logged and (for the buckets Chunk 0 actually enables) reversible.

**Non-goals.**

- **No re-gating of the external `:8765`/`:8000` wire.** Real writes by the local trusted client are intended; Chunk 0 changes only the in-editor `call_tool` re-entrancy path. (See §1 PM calibration note.)
- No client-facing `approvalToken`/`grantId` argument on any tool schema or on the wire. Approval provenance is an **in-process handshake**, never a client-supplied bearer value.
- No relaxation or reordering of the four structural HARD denies (`not_visible`, `user_tool_forbidden`, `call_tool_depth_exceeded`, `workflow_run_forbidden`). Approval can never reach or override them, at either enforcement site.
- No change to Task Atlas eligibility **classification** semantics — Blocked rows stay Blocked-labeled; only the *write* path gains a human-gated escape.
- No auto-approval, no "remember this approval," no unattended real writes. Every real write requires a fresh in-editor human click bound to that exact step.
- No whole-composite single-click approval in Chunk 0 (per-step grants only); no approval UI on any non-editor surface.
- No merge onto `experiment/v0.33-ue58-validation`; no coupling to official-MCP work.
- **The `dangerous_no_dryrun` scene/exec bucket does NOT reach `AllowApprovedReal` in Chunk 0** unless a rollback artifact exists for the tool. Chunk 0 enables approval only for tools already reversible/previewable (`bDryRunSupport==true`) or carrying a backup manifest. Raw `spawn_actor`/`editor_set_map_game_mode`/`batch_*`/`execute_python`/`execute_console_command` stay Deny even with a valid grant until a later chunk adds a pre-write snapshot. This makes the reversibility guarantee *true* rather than assumed (§5.5, Open Decision #3).

## 3. Threat model & trust boundaries

**Trust domains.**

1. **External MCP clients** — the `:8765` native HTTP dispatcher (`HandleToolsCall`) and the optional UE5.8 `:8000` official server. These are **untrusted for authorizing an *approved* real-write**: they can never mint or present a grant. (They *can*, by product design, issue ordinary real writes subject to the wire's localhost+origin+optional-bearer posture — that is not what Chunk 0 governs.) Verified: HTTP `tools/call` runs on `AsyncTask(ENamedThreads::GameThread)` ([`UnrealMcpProtocol.cpp:236`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpProtocol.cpp:236)) — the **same** GameThread that owns the in-editor grant registry. There is *no* thread barrier between a wire call and a live grant, so provenance separation must be **explicit**, not incidental.
2. **In-editor human** operating Task Atlas / chat Slate widgets — the **only** trusted approver of a real write via `AllowApprovedReal`. Such a write is authorized only by a caller executing inside the editor mint→execute critical section, on a call stack originating from a physical Slate confirm click.

**What may trigger an `AllowApprovedReal` write under Chunk 0:** only the trusted composite executor's own replay of a reviewed, ordered step list, where each step matches a live grant bound to `(GrantId, RunId, StepIndex, resolved-handler+args hash)` **and** the current execution carries in-editor UI provenance. Grant-match alone is never sufficient.

**Attack surface closed (each mapped to a §5 clause):**

- **Cross-channel grant replay / content-hash bearer grant** — a grant keyed on `SHA-256(tool+args)` is a value a wire client can independently *reconstruct* (composite step lists are AI-authored and often logged) and replay within the TTL to consume the human's grant. → §5.1, §5.2.
- **Shared-executor confused deputy** — wire calls and in-editor calls share one executor on one thread; provenance can't be inferred. → §5.1.
- **Depth-0 composite re-entrancy** — the composite runs via `ExecuteToolFromEditorUI`, which never bumps `FScopedCallToolDepth` (bumped only inside `CallTool`, *after* `Facts.Depth` is read), so first-level `call_tool` steps classify at `Depth==0` and the depth guard never fires; AI glue between mint and consume controls count/order/interleave of hash-matching calls. → §5.3.
- **Structural HARD-deny bypass via `bForceWriteEvenIfBlocked`** — `MakeComposite` gates only on the coarse `Eligibility==Blocked && !bForceWriteEvenIfBlocked` and is **reason-blind**; force=true can materialize a composite containing a `user_tool_forbidden`/`workflow_run` step past a structural deny. → §5.4.
- **Audit/rollback gap** — the `tool_call` ActivityLog is bound to ingress, not to the write; the `CallTool`/`MakeComposite`/Slate paths emit nothing; nested composite sub-writes are invisible; `dangerous_no_dryrun` tools have no backup. → §5.5.
- **TOCTOU / clock rollback** — args mutated between approve and execute; wall-clock TTL extended by a backward clock jump; abandoned grants stay live. → §5.2.

## 4. Design: the approval-gated real-write path

### 4.1 Decision-class table

| Facts | No grant | Valid grant + in-editor provenance + reversible/backed-up |
|---|---|---|
| `bDangerous && bDryRunSupport` (ForceDryRun bucket) | ForceDryRun (`dryRun:true`, current) | **AllowApprovedReal** (real write, no `dryRun` inject) |
| `bDangerous && !bDryRunSupport` **and** `bHasBackupManifest` | Deny (`dangerous_no_dryrun`) | **AllowApprovedReal** |
| `bDangerous && !bDryRunSupport` **and** `!bHasBackupManifest` (raw scene/exec) | Deny (`dangerous_no_dryrun`) | **Deny (still)** — not approval-eligible in Chunk 0 (§5.5, non-goal) |
| `not_visible` (`bVisible==false`) | Hard-deny | **Hard-deny (unchanged)** — checked FIRST, above the approval branch |
| `user_tool_forbidden` (`SourceKind==UserRegistry`) | Hard-deny | **Hard-deny (unchanged)** |
| `call_tool_depth_exceeded` (`Depth>=1`) | Hard-deny | **Hard-deny (unchanged)** |
| `workflow_run_forbidden` (`unreal.workflow_run` via call_tool) | Hard-deny | **Hard-deny (unchanged)** |
| Safe tool (`!bDangerous`) | Allow | Allow (approval fact never consulted) |

The four HARD denies are evaluated **before** the `if (bDangerous)` block (confirmed [`UnrealMcpCallToolPolicy.cpp:26-44`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolPolicy.cpp:26)), so approval facts can never reach them. `AllowApprovedReal` is reachable **only inside** the `bDangerous` block, and only when the tool is reversible/backed-up.

### 4.2 Approval grant: representation, binding, lifetime, minting

**Not a content-hash bearer secret.** A grant keyed on `SHA-256(tool+args)` and looked up by recomputing that hash from the actual call is a value the wire client fully controls and can reconstruct. The primary key is therefore a **server-chosen unguessable `FGuid GrantId`**, handed *directly* from the in-editor mint site into the execute call — never recomputed from client args, never accepted over the wire.

**Record** (an extension of the existing thread-safe approval registry, not a parallel store):

```
FApprovalGrant {
  FGuid    GrantId;            // server-chosen, unguessable, never client-supplied — PRIMARY key
  FGuid    RunId;              // per-MakeComposite run (§5.3)
  int32    StepIndex;          // ordered step position within the run (§5.3)
  FString  BoundArgsHash;      // SHA-256 of ResolvedHandlerName + '\n' + total-injective-canonical(args)
                               //   — SECONDARY integrity check, NOT the lookup key
  FString  ApprovingSessionId; // the assistant-run / editor session that approved (single-flow, not process-global)
  double   ExpiresAtMonotonic; // FPlatformTime-based, NOT wall clock (§5.2)
  FString  Approver;
}
```

**Binding.** The grant is bound to four independent facts, **all** of which must hold at execute time:
1. **GrantId match** — the executor presents the `FGuid` it received at mint; wire clients cannot supply it (§5.1).
2. **In-editor provenance** — a GameThread thread-local set only within the editor mint→execute critical section confirms the call stack originated from the Slate confirm handler; `false` for any `HandleToolsCall`-originated stack (§5.1).
3. **`(RunId, StepIndex)` in order** — the executor is replaying exactly this step of exactly this run, in sequence (§5.3).
4. **`BoundArgsHash` equality** — recomputed over the **resolved handler name** (post-handler-resolution, not the display `toolName`) and a **total, injective** canonicalization that preserves *every* field reaching `ExecuteTool`, evaluated on the exact post-decision args object **before** any `dryRun` mutation. Any field the canonicalizer cannot represent → reject (fail-closed), never silently drop (§5.2).

**Minting.** The **only** two mint sites are (1) the Task Atlas "Make Tool Set" Slate confirm handler and (2) the assistant-run approval resolve path (`ResolveAssistantApproval` on `Approved`) — both driven by a physical human click inside the editor process. No HTTP handler, no `CallTool` argument, no wire field mints or references a grant. The mint site reserves the grant and hands `GrantId` in-process to the executor; consumption is reserved at mint-handoff, not at first content match, so there is no consume-race a wire caller can win (§5.1).

**Lifetime.** Short TTL (propose **120s**, strictly under the existing 300s approval-wait ceiling) **and single-use** — erased atomically on first successful `(GrantId, provenance, RunId/StepIndex, hash)` match. TTL uses a **monotonic clock** (`FPlatformTime`) and an **active timer sweep**, so a backward wall-clock jump cannot extend the window and a minted-but-never-consumed grant is swept and surfaced in audit rather than lingering live (§5.2).

### 4.3 Pure-function evolution

Keep `ClassifyCallToolTarget_Pure` pure — no I/O, no registry access inside the function. Add input facts + one output enumerator; the *caller* (`CallTool`) does all the impure grant lookup and sets the facts.

- **`ECallToolDecision`** ([`UnrealMcpCallToolPolicy.h:14`](Plugins/UnrealMcp/Source/UnrealMcp/Public/UnrealMcpCallToolPolicy.h:14)): append `AllowApprovedReal` (append-only, mirrors the `EAiProviderKind` append-only discipline).
- **`FCallToolTargetFacts`** ([`:28`](Plugins/UnrealMcp/Source/UnrealMcp/Public/UnrealMcpCallToolPolicy.h:28)): add
  - `bool bHasBoundApproval = false;` — set true by `CallTool` **only** when GrantId + in-editor provenance + RunId/StepIndex-in-order + BoundArgsHash *all* matched. A composite fact, never "hash matched" alone.
  - `bool bHasBackupManifest = false;` — set true when the tool is reversible via an existing per-tool backup convention (§5.5).
- **Body** ([`UnrealMcpCallToolPolicy.cpp:24`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolPolicy.cpp:24)): leave the four HARD-DENY guards (`:26-44`) exactly as-is and above everything. Inside `if (bDangerous)` **only**, insert the approval short-circuit FIRST:
  ```cpp
  if (F.bHasBoundApproval && (F.bDryRunSupport || F.bHasBackupManifest))
  {
      Result.Decision = ECallToolDecision::AllowApprovedReal;
      Result.Reason   = TEXT("approved_real");
      return Result;
  }
  ```
  then fall through to the existing ForceDryRun (`if bDryRunSupport`) / `dangerous_no_dryrun` (else) logic unchanged. The safe-tool `Allow` tail is untouched.

Net: both dangerous buckets gain a single approval short-circuit, but the `!bDryRunSupport && !bHasBackupManifest` case still falls through to Deny even with a grant — enforcing §5.5 inside the pure function itself. With `bHasBoundApproval=false` everywhere, the current baseline decision matrix is byte-identical (regression-safe — see §8 for how the baseline is captured rather than hardcoded).

**`CallTool` caller changes** ([`UnrealMcpCallToolLibrary.cpp`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolLibrary.cpp)): after `GatherFacts`, resolve the handler name, compute `BoundArgsHash`, look up the grant **by GrantId** (received in-process from the mint site), verify provenance + RunId/StepIndex-in-order + hash, set `Facts.bHasBoundApproval` accordingly, set `Facts.bHasBackupManifest`, then classify. On `AllowApprovedReal`: skip the `dryRun` inject (`:160-163`), **write audit + snapshot backup first (fail-closed, §5.5)**, consume/erase the grant, then dispatch. `MakeMetaPayload` emits `policyDecision="AllowApprovedReal"`, `forcedDryRun=false`.

### 4.4 Audit & rollback

Audit and rollback move to the **write chokepoint** (`CallTool`), not the ingress, because the ingress `tool_call` event wraps only the single top-level dispatch and misses nested composite sub-writes entirely (§5.5).

For every `AllowApprovedReal` decision, `CallTool` — **before** calling `ExecuteToolFromEditorUI`, fail-closed:

1. Emits a **`tool_call`** ActivityLog event reusing the existing payload builder + capture-metadata attach, additionally recording: `policyDecision="AllowApprovedReal"`, `approvedReal=true`, `approver`, `approvalTimestampUtc`, `boundArgsHash`, `grantId`, `runId`, `stepIndex`, and a `nested=true`/depth tag for dedupe against any ingress event.
2. For a reversible/backed-up tool, snapshots the **backup manifest** under the existing per-tool backup convention.
3. If the audit append (or a required snapshot) fails, **aborts the write** — the real dispatch never runs. This closes the "write happened but log didn't" window.

A distinct **`approval_granted`** ActivityLog event is emitted at **mint** time (mirroring the existing `ApprovalRequired` emission) recording `grantId`, `runId`, `stepIndex`, `toolName`, `boundArgsHash`, `approver`, `expiresAtUtc`. The log thus always contains the full pair (`approval_granted` → `tool_call`) with matching `grantId`, and a grant minted-but-never-consumed (swept per §5.2) is visible. To avoid double-logging non-composite top-level HTTP/AssistantRun calls, suppress the ingress event when `Decision==AllowApprovedReal`, or dedupe on the `nested`/depth tag.

**Rollback guarantee.** Chunk 0 only permits `AllowApprovedReal` for tools with `bDryRunSupport==true` (already previewable/reversible) OR `bHasBackupManifest==true`. Tools with neither stay Deny (§5.5). So every approved real write in Chunk 0 has a resolvable rollback artifact; the reversibility guarantee is enforced, not assumed.

### 4.5 Task Atlas eligibility interaction

Chunk 0 does **not** re-classify eligibility. `ClassifyTask` still runs `ClassifyCallToolTarget_Pure` with `bHasBoundApproval=false` (no approval exists at preview time), so a composite whose steps hit the dangerous buckets **still** classifies **Blocked** and the Eligibility field still reads Blocked. That is intentional: eligibility describes the *un-approved* posture.

What changes is the *write* path, not the *label*:

- The Make Tool Set button stays hard-gated on `Row.Eligibility != Blocked` for the default/un-approved state — greyout + blocked-reason tooltip unchanged. No silent auto-unblock, no eligibility mutation.
- Chunk 0 adds an explicit **"Review & Approve Writes"** affordance on a Blocked row that surfaces the exact resolved (tool, args) list the human is authorizing. On confirm it mints **per-step** grants (bound to `RunId`, `StepIndex`, resolved-handler hash) and drives `MakeComposite`.
- The affordance is offered **only** for rows whose Blocked reason is an **approvable** bucket. Rows with a structural-deny step (`user_tool_forbidden`, `workflow_run_forbidden`, etc.) get **no** approve affordance (§5.4). Rows with a raw `dangerous_no_dryrun` (no backup) step also get no approve affordance in Chunk 0 (§5.5).
- On confirm, the UI may set `bForceWriteEvenIfBlocked=true` (previously hard-wired `false`) — but only through the reason-aware gate in §5.4, and only backed by live grants.

## 5. Safety clauses

### 5.1 Replay & confused-deputy — provenance + GrantId, not content hash *(closes: cross-channel replay, shared-executor confused deputy, bearer-grant reconstruction)*

- The grant's **primary key is a server-chosen unguessable `FGuid GrantId`**, handed in-process from the mint site to the executor. `CallTool`/`ExecuteTool` look up **by GrantId**, never by recomputing a hash from client args. `BoundArgsHash` is a *secondary* integrity check only.
- `bHasBoundApproval` requires **both** (a) a matching grant **and** (b) `provenance == in-editor-UI` — a GameThread thread-local set only inside the editor mint→execute critical section and structurally `false` on any `HandleToolsCall`-originated stack. Grant-match alone is never sufficient. **This is what closes the shared-executor problem** ([`ExecuteToolFromEditorUI` == `ExecuteTool`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolDispatcher.cpp:957)) without re-gating the wire.
- The grant is bound to the **approving session/assistant-run id** (single-flow), not a process-global bucket, so it is not visible to an unrelated ingress.
- The consume/reservation happens at **mint-handoff**, not at first content-hash match, eliminating the consume-race a wire caller could otherwise win or front-run.
- **Test (Chunk 0.2/0.4):** dangerous tool + matching-content grant + **wire-ingress provenance** → still Deny/ForceDryRun (a client with the exact right args cannot ride a live grant).
- **Out of Chunk 0 scope (Open Decision #2):** the wire's own posture (localhost + `ValidateOrigin` [absent-Origin passes] + *optional* bearer) is the pre-existing MCP-local-server trust model. Hardening it (e.g. requiring `AuthToken` by default) is a **separate** pass; Chunk 0 neither depends on nor changes it. This spec explicitly does **not** route the wire through the classifier (doing so would break the intended local-client write path).

### 5.2 TOCTOU, canonicalization & clock-rollback *(closes: args-mutation between approve and execute, hash-at-wrong-layer, wall-clock extension, abandoned grants)*

- `BoundArgsHash` is computed over the **resolved handler name**, not the display `toolName`, so it binds at the executor layer, not the schema layer.
- Canonicalization is **total and injective** over the exact args object reaching `ExecuteTool`, evaluated **before** the `dryRun` mutation at [`UnrealMcpCallToolLibrary.cpp:162`](Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpCallToolLibrary.cpp:162). Any field the canonicalizer cannot faithfully represent → **reject** (fail-closed), never drop — a malicious extra field cannot survive to the handler while hashing to the approved value.
- TTL uses a **monotonic clock** (`FPlatformTime`), not wall-clock, so a backward clock jump cannot extend the window.
- An **active timer sweep** expires grants, so minted-but-never-consumed grants do not linger consumable and *do* fire the audit "abandoned grant" visibility.
- Single-use: erased atomically on first full match.

### 5.3 Composite re-entrancy / depth-0 escalation *(closes: AI-glue interleave, depth guard no-op on first-level steps)*

- The composite executor is entered under an **incremented `FScopedCallToolDepth`**, so any `call_tool` issued from AI-authored `main.py` glue runs at `Depth>=1` and is hard-denied by `call_tool_depth_exceeded`. The `AllowApprovedReal` path is reachable **only** from the trusted executor's own `Depth==0` replay of the reviewed, ordered step list — never from AI glue.
- Each grant is bound to a per-run server-chosen **`RunId`** (minted at `MakeComposite` entry) plus a monotonic **`StepIndex`**. `CallTool` accepts a grant only when the current call is the executor-driven replay of that exact `(RunId, StepIndex)` **in order** — not a free-floating hash match. This defeats the glue's ability to reorder/interleave/re-fire steps: "reviewed step 3" is distinguishable from "AI glue re-firing step 1's args."

### 5.4 Structural HARD-deny bypass at the composite-write gate *(closes: reason-blind bForceWriteEvenIfBlocked)*

- `MakeComposite` is made **reason-aware**. Before honoring `bForceWriteEvenIfBlocked`, it re-scans the eligibility step list and **refuses** force (returns Blocked with a distinct `structural_deny_not_approvable` error) if **any** step's `DenyReason ∈ {not_visible, user_tool_forbidden, call_tool_depth_exceeded, workflow_run_forbidden}`. Force may only lift steps whose `DenyReason` is an approvable bucket.
- A helper `IsApprovableDenyReason(DenyReason)` whitelists **only** the approvable dangerous buckets (and, per §5.5, only reversible/backed-up ones). Asserted in a unit-test matrix mirroring the pure-function tests.
- **Mint-site refusal:** the Slate confirm handler and the assistant-run approval resolve refuse to mint a grant for any step classified as a structural deny — no approve affordance is surfaced for those rows, so the human is never asked to authorize a phantom/re-entrant/workflow tool.
- Each minted grant is bound to the `(RunId, StepIndex, resolved-handler+args hash)` of an **approvable** step only, so even if force is passed, no grant can match a structural-deny step. The four structural denies stay un-overridable at **both** enforcement sites.

### 5.5 Audit & rollback gap *(closes: unlogged approved writes, invisible nested sub-writes, irreversible scene/exec writes)*

- The `tool_call` ActivityLog event is emitted **inside `CallTool`** for every `AllowApprovedReal` decision (§4.4), guaranteeing an entry regardless of ingress and capturing nested composite sub-writes the outer ingress event misses.
- Audit write (and required backup snapshot) is a **hard, fail-closed precondition** of the real execute — write the events/snapshot before dispatch; if the append fails, abort.
- **The `dangerous_no_dryrun` scene/exec bucket does not reach `AllowApprovedReal` in Chunk 0.** The pure-function short-circuit converts a dangerous tool only when `bDryRunSupport==true` OR `bHasBackupManifest==true`. `spawn_actor`, `editor_set_map_game_mode`, `batch_*`, `execute_python`, `execute_console_command` have neither and stay **Deny even with a valid grant** — deferred to a later chunk that first adds a pre-write level/actor-state snapshot under the per-tool backup convention. This makes the reversibility guarantee real.
- **Test (Chunk 0.4):** every `AllowApprovedReal` execution — including one nested two levels deep inside a composite run from the Slate button — produces a paired `approval_granted` + `tool_call` entry with matching `grantId`/`boundArgsHash`, and (for any tool that reached real execute) a resolvable rollback artifact. The gate fails if any approved write has zero audit rows or (for a reversible tool) no manifest.

## 6. Chunk breakdown

### Chunk 0.0 — Pure-function + policy seam (inert by default)
**Goal:** introduce `AllowApprovedReal`, `bHasBoundApproval`, `bHasBackupManifest`, wired but inert.
**Files:** `UnrealMcpCallToolPolicy.h` (append enumerator + two fact fields), `UnrealMcpCallToolPolicy.cpp` (approval short-circuit inside `bDangerous` only, gated on `bDryRunSupport || bHasBackupManifest`), `UnrealMcpCallToolPolicyTests.cpp`.
**Acceptance:**
1. With `bHasBoundApproval=false` the captured baseline decision matrix is byte-identical (regression-safe).
2. `dangerous+dryRunSupport+approval → AllowApprovedReal`; `dangerous+!dryRunSupport+bHasBackupManifest+approval → AllowApprovedReal`; `dangerous+!dryRunSupport+!bHasBackupManifest+approval → Deny (dangerous_no_dryrun)`.
3. Each of the four HARD-DENY facts + `approval=true` still returns its original Deny reason; safe tool + `approval=true` still Allow.
4. `check_ue56_compat.py` → 0 errors / 0 warnings (no engine-version preprocessor logic outside `UnrealMcpEngineCompat.h`).

### Chunk 0.1 — Grant registry (GrantId-keyed, provenance, RunId/StepIndex, monotonic TTL, single-use)
**Goal:** in-editor-only, GrantId-keyed, run/step/provenance-bound, single-use, monotonic-TTL grants with a secondary args-hash integrity check.
**Files:** the existing approval-policy unit (extend registry with `FApprovalGrant`; `Mint/LookupByGrantId/Consume` APIs; total-injective canonical `(resolvedHandler,args)→SHA-256` helper; monotonic-clock TTL + active sweep; in-editor-provenance thread-local).
**Acceptance:**
1. Lookup is by server-chosen `GrantId`; **no public API accepts a client-chosen GrantId**, and `BoundArgsHash` is secondary-only.
2. Canonical hash is stable across args-JSON key ordering **and** rejects (fail-closed) any field it cannot represent.
3. `LookupByGrantId` misses after `Consume` (single-use) and after monotonic-TTL expiry; a backward wall-clock jump does not extend TTL.
4. Provenance thread-local is `true` only inside the editor mint→execute critical section; a unit test proves it `false` on a simulated non-editor stack.

### Chunk 0.2 — CallTool integration (in-editor path only)
**Goal:** `CallTool` honors `AllowApprovedReal` (GrantId + provenance + RunId/StepIndex + hash), skips `dryRun` inject, consumes grant, audits fail-closed.
**Files:** `UnrealMcpCallToolLibrary.cpp` (resolve handler, compute hash, lookup by GrantId, verify provenance/RunId/StepIndex/hash, set facts before classify; on `AllowApprovedReal` skip `:160-163`, audit+snapshot fail-closed before dispatch, consume grant; `MakeMetaPayload` emits `AllowApprovedReal`/`forcedDryRun=false`).
**Acceptance:**
1. Approved reversible/backed-up dangerous tool executes with **no `dryRun` field** reaching the handler; the same call without a grant → unchanged ForceDryRun/Deny.
2. A grant bound to args A does **not** authorize args B (replay); a grant bound to `(RunId,StepIndex)` 3 does not authorize step 1 (reorder).
3. A matching-content grant presented from **wire-ingress provenance** → still Deny/ForceDryRun.
4. `validate_tool_registry.py` unaffected (no tool-count / dispatch-branch drift).

### Chunk 0.3 — In-editor mint sites + reason-aware force gate + audit events
**Goal:** the only mint paths are the human-click Slate confirm + the assistant-run approval resolve; force gate is reason-aware; audit events land.
**Files:** the assistant-run approval unit (mint a bound grant on `Approved`; `approval_granted` event), `STaskAtlasWindow.cpp` (approve affordance on approvable Blocked rows → mint per-step grants → `MakeComposite(force=true)`; no affordance for structural-deny or no-backup rows), `UnrealMcpTaskAtlasService.cpp` (reason-aware `MakeComposite` gate + `IsApprovableDenyReason`; enter composite executor under incremented `FScopedCallToolDepth`; mint `RunId`), ActivityLog payload additions.
**Acceptance:**
1. No `:8765`/`:8000` path reads an `approvalToken` arg or calls `Mint`.
2. A real approved write's `tool_call` entry carries all approval fields + matching `boundArgsHash`; an `approval_granted` event precedes it with the same `grantId`.
3. `MakeComposite(force=true)` on a composite containing a `workflow_run`/`user_tool_forbidden` step still returns Blocked (`structural_deny_not_approvable`); no approve affordance renders for those rows.
4. UE5.7 Mac `MyProjectEditor` build green.

### Chunk 0.4 — E2E + regression gate
**Goal:** prove the Task Atlas Blocked→approved-real path (for a reversible/backed-up step) and prove no default-path regression.
**Files:** new automation fixtures under `Tools/UnrealMcpTests`.
**Acceptance:**
1. A Blocked row still greys the Make Tool Set button; the approve affordance appears only on approvable rows.
2. The approve affordance produces a real (reversible/backed-up) write **and** a paired `approval_granted`+`tool_call` audit with matching `grantId`, plus a resolvable rollback artifact; a write nested two levels deep in a composite is individually logged.
3. Declining/timeout leaves zero side effects; the grant is swept and audited as abandoned.
4. A wire call of the same dangerous tool **with a matching-content grant but no in-editor provenance** still gets dry-run/deny (cannot ride the grant); full Release Verification SOP on a `/tmp` extract passes.

## 7. Open director decisions

1. **Target branch.** This is Make-Tool-Set core safety evolution on the public product line. Per memory, `experiment/v0.33-ue58-validation` **never** merges to main; Chunk 0 should land on a **main-derived v0.34 branch**, un-entangled from the UE5.8 validation experiment. **Confirm target = main-derived v0.34.**
2. **Wire hardening (out of Chunk 0 scope — decide separately).** The `:8765`/`:8000` wire permits real writes by design (local trusted client). Its access control is localhost + `ValidateOrigin` (passes when the Origin header is absent) + an **optional** bearer (`ValidateAuthorization` returns true when `AuthToken` is empty). Chunk 0 does **not** change this and does **not** route the wire through the classifier (that would break the local-AI-client write path). Separate question for a later hardening pass: **should `AuthToken` be required by default?** Not a Chunk 0 blocker.
3. **`dangerous_no_dryrun` eligibility.** Chunk 0 (§5.5) makes the raw scene/exec bucket **approval-ineligible** until a pre-write snapshot exists — only `bDryRunSupport` or `bHasBackupManifest` tools reach `AllowApprovedReal`. Confirm the scene/exec bucket (esp. `execute_python`/`execute_console_command`, the highest blast radius) is deferred to a later snapshot-adding chunk. **(Recommended.)**
4. **Approval granularity.** Per-step grants (N clicks) vs one grant covering the whole step list (1 click, N bound hashes). Per-step is safer/simpler for Chunk 0; **recommend per-step**, revisit batch later.
5. **TTL & GC.** Propose **120s single-use**, strictly under the 300s approval-wait ceiling, with an **active monotonic-clock sweep** (not lazy expiry, per §5.2). Confirm.
6. **Official UE5.8 `:8000` delegation.** When the official ToolsetRegistry path is opt-in, add a mandatory policy-delegation validator asserting generated official toolsets delegate to the UEAtelier executor and **never** touch the grant registry or set in-editor provenance. Flag for the validator.

## 8. Rollout / verification SOP

1. **Static validators:** `python3 Tools/validate_tool_registry.py` (toolCount == mirrorToolCount == JSON length; issueCount=0; dispatch `matched` increments only by intended new branches); `python3 Tools/check_ue56_compat.py` (0 errors / 0 warnings — the new enum/fields must not add engine-version preprocessor logic outside `UnrealMcpEngineCompat.h`).
2. **Baseline capture, not hardcode.** Snapshot the current `ClassifyCallToolTarget_Pure` decision matrix (all registry tools × facts) into a golden fixture BEFORE the change; assert byte-identical with `bHasBoundApproval=false` after. (Avoids the stale-hardcoded-count trap noted in project memory.)
3. **Pure-function + registry unit tests** (0.0/0.1): full decision-table matrix incl. the four HARD-deny + approval cases, the `!bDryRunSupport && !bHasBackupManifest` Deny case, canonicalization stability/fail-closed, single-use, monotonic-TTL, provenance-false-off-editor-stack.
4. **Dual-engine build** (0.3): before smoke, `rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate` (stale-dylib trap), then build `MyProjectEditor` for UE5.7 Mac:
   ```bash
   "/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
     MyProjectEditor Mac Development \
     -project="/Users/wmbt7052/Documents/Unreal Projects/MyProject/Examples/UEvolveExample57/UEvolveExample57.uproject" \
     -WaitMutex
   ```
   Repeat for the UE5.6 host.
5. **Automation gate** (0.4): the four 0.4 acceptance criteria as UE Automation tests.
6. **Security regression asserts** (must all pass, fail-closed): matching-content grant + wire provenance → Deny; `MakeComposite(force=true)` with a structural-deny step → Blocked; approved write with a simulated audit-append failure → write aborted (no side effect).
7. **Full Release Verification SOP** on a fresh `/tmp` extract-and-test project (do not declare ready off `git status clean` + local build alone).
8. **AGENTS.md Freshness Rule:** evaluate for the tool-count line, tool-list section, RAG/Knowledge section, C++ file-inventory (new `FApprovalGrant`/`AllowApprovedReal` seam), and safety-rules section; fold minimal edits into the same commit if any threshold is crossed.

---

_Appendix — provenance of this spec: authored by the PM from a 4-phase orchestration (parallel code-mapping readers → design synthesis → 5-lens adversarial red-team → synthesis). All file:line anchors were verified against the working tree at `7e796ca`. The red-team's "route the external wire through the classifier" recommendation was **rejected** by PM review as product-breaking and redundant (see §1 PM calibration note); its GrantId + in-editor-provenance mechanism was **kept** as the actual fix. Its other four exploitable findings (content-hash replay, depth-0 re-entrancy, reason-blind force gate, audit/rollback gap) are folded in as §5.1–§5.5._
