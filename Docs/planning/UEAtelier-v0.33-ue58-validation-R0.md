# UEAtelier v0.33 UE5.8 Validation — R0 Plan

**Status**: R0 draft for `experiment/v0.33-ue58-validation`.

**Baseline**: branch is isolated from main `7e796ca` (`v0.32.2`) plus the
minimum cherry-picked UE5.8 compatibility commits. This branch is a validation
worktree and **never merges to main** as-is.

**Goal**: Validate a UE5.8-only official-MCP integration direction without
weakening the shipped v0.32.x UE5.6/UE5.7 line.

**Decision locked**: v0.33 is a UE5.8-only validation build. UE5.6/UE5.7 keep
building from the v0.32.x line; they are not part of this validation release
surface.

---

## 1. Scope and explicit non-goals

### Scope

v0.33 validates whether UEAtelier can safely layer UE5.8's official MCP stack
into the product model:

1. Build the existing UEAtelier plugin and UE5.8 example host against promoted
   UE 5.8.
2. Keep UEAtelier's current Streamable HTTP MCP server on `127.0.0.1:8765/mcp`
   for the existing `unreal.*` tool surface.
3. Add an optional UE5.8 official ToolsetRegistry / ModelContextProtocol usage
   track on the official side, expected at `127.0.0.1:8000/mcp` when enabled.
4. Validate official Python `ToolsetDefinition` as the runtime-hot substrate for
   Task Atlas Make Tools.
5. Restore the Task Atlas Make Tool Set product path only through a guarded,
   policy-preserving official-Python adapter.
6. Produce enough evidence to choose the next milestone: validation-only,
   delegating adapter subset, or full official-path Make Tools surface.

### Explicit non-goals

- No tri-engine v0.33 public release.
- No UE5.6/UE5.7 official-MCP backport.
- No removal of UE5.6/UE5.7 support from the main product line.
- No merge of this validation branch to main as-is.
- No replacement of UEAtelier's `:8765` server.
- No dependency on Epic AIAssistant or Epic cloud services.
- No redistribution of Epic NoRedist / Experimental modules.
- No direct AI-authored core C++ handler generation.
- No runtime C++ Make Tools path.
- No weakening of UEAtelier approval, dry-run, audit, path policy, backup,
  manifest, rollback, or captured-argument redaction guarantees.

---

## 2. Architecture direction

### Dual-track tool usage

v0.33 validation keeps two tool-usage tracks:

- **UEAtelier track**: the existing `unreal.*` server at `127.0.0.1:8765/mcp`,
  preserving the shipped policy, audit, Task Atlas, code-tools, rollback, RAG,
  and self-extension behavior.
- **Official UE5.8 track**: optional official ToolsetRegistry +
  ModelContextProtocol, exposed through the official server when enabled
  (`127.0.0.1:8000/mcp` by default).

The two tracks are additive during validation. Official ToolsetRegistry does not
become the cross-version foundation for UE5.6/UE5.7.

### Official-first tool creation

Tool creation is official-first on UE5.8, with a strict split by runtime model:

- **Python-first runtime creation**: official Python `ToolsetDefinition` is the
  default candidate for runtime Make Tools. Spike 1 proved the load-bearing
  assumption: Python toolsets can register, list, call, and reload without an
  editor restart through official MCP.
- **C++ restart-required promotion**: official C++ `UToolsetDefinition` remains
  a developer-promotion path only. It is useful for deliberate native toolsets,
  but it is not a runtime Make Tools substrate because reflected C++ still needs
  compile + editor restart.

---

## 3. Restoring Task Atlas Make Tools safely

Task Atlas **Make Tool Set** should be restored, but the backend changes:

1. Task Atlas classifies a task or cluster.
2. RAG/tool recommendation runs first to avoid duplicate tool creation.
3. If the task is `official_python_ready`, UEAtelier generates an official
   Python `ToolsetDefinition` wrapper.
4. The wrapper registers into official ToolsetRegistry and becomes reachable via
   official MCP.
5. The generated wrapper **does not directly mutate editor or project state**.
   It delegates execution back into a UEAtelier policy executor.

This is the key safety boundary. The button was walled off after the v0.26 risk
class because AI-generated handlers could bypass product guardrails. Restoring
Make Tools through official Python is acceptable only if generated official
wrappers delegate back into UEAtelier for:

- approval checks,
- dry-run and force-dry-run policy,
- deny policy,
- path safety,
- ActivityLog/audit event recording,
- captured-argument redaction,
- backups/manifests/rollback for write paths,
- postcheck verification.

In other words: official ToolsetRegistry provides discovery and invocation; it
must not become an ungoverned execution bypass around UEAtelier's safety model.

---

## 4. Task Atlas re-architecture and AgentSkill promotion

### Proposed Task Atlas tracks

- `existing_tool_available`: recommend an existing UEAtelier or official tool;
  do not generate.
- `official_python_ready`: generate a runtime-hot official Python
  `ToolsetDefinition` wrapper.
- `ueatelier_python_ready`: keep existing UEAtelier Python user-tool path for
  cases where official schema or runtime behavior is insufficient.
- `official_skill_only`: promote instructions to an official AgentSkill without
  generating a callable tool.
- `official_cpp_candidate`: produce a restart-required developer scaffold or
  plan only.
- `partial`: write a markdown/preview artifact only.
- `blocked`: disable Make Tool Set and show the first blocker/reason.

### Composite mapping

Task Atlas composites should map to official ToolsetRegistry as follows:

- A generated composite becomes one official Python `ToolsetDefinition` class,
  or one method in a shared generated Task Atlas toolset.
- Method schemas are derived from the composite's stable input model.
- Method bodies delegate to the UEAtelier composite executor with composite ID,
  sanitized defaults, runtime arguments, dry-run/policy metadata, and expected
  postchecks.
- Generated source has a manifest recording class/toolset name, schema hash,
  source path, Task Atlas task IDs, registration status, smoke result, and
  rollback/delete handles.

### AgentSkill promotion

Spike 1 saw official MCP expose `ToolsetRegistry.AgentSkillToolset` with
list/read/create-update skill tools. Treat AgentSkill promotion as an official
instruction-asset path:

- `AgentSkill` stores description/instructions and can document when/how to use
  a generated toolset.
- It is not the executable mechanism by itself.
- A promoted skill should point at the generated callable ToolsetDefinition when
  execution is needed.
- Rollback must delete or restore both generated Python source/registration and
  any generated AgentSkill asset.

---

## 5. Spike evidence appendix

### Spike 1 — official Python hot-registration: PASS

Source report: `/tmp/hermes-ue58-spike-results.md`.

Evidence captured on a scratch UE5.8 project with official `ToolsetRegistry` and
`ModelContextProtocol`, without the UEAtelier plugin:

- Registered `HermesRuntimeToolset` deriving `unreal.ToolsetDefinition` without
  editor restart.
- Official MCP discovery worked through `tools/list`, `list_toolsets`, and
  `describe_toolset`.
- Official MCP `call_tool` executed `greet`, `echo_list`, `sum_map`,
  `maybe_double`, and a USTRUCT roundtrip.
- Schema checks passed for primitives, `list[str]`, `dict[str,float]`, optional
  float, and USTRUCT input/output.
- Bare `list` failed as expected with
  `ToolCallMissingAnnotation("Type <class 'list'>: missing specification for contained type.")`.
- `toolset_registry.reload_module` reloaded the module without restart.
- A fresh MCP session saw changed behavior (`v2 hi Ada`) and the newly added
  method (`new_value() -> v2-new-method-live`).

### Hosting lesson

The reliable hosting mode is a live editor process with:

```text
-ModelContextProtocolStartServer -ModelContextProtocolPort=N
```

The `UnrealEditor-Cmd -run=pythonscript` commandlet path registered Python
classes in-process, but did not reliably serve official MCP calls. Use a live
editor for official MCP end-to-end validation.

### Remaining spikes

- **Spike 2**: UEAtelier `:8765` and official `:8000` coexistence in one UE5.8
  editor. Pending this UE5.8 plugin build.
- **Spike 3**: official `MCPClientToolset` connecting to UEAtelier `:8765`.
  Not run yet.

---

## 6. Compat gating

The UE5.8 official-toolset adapter must be compile- and runtime-gated:

- Add a single macro in `UnrealMcpEngineCompat.h`, for example:
  `UNREALMCP_HAS_OFFICIAL_TOOLSETS`.
- Define it true only when compiling with UE5.8+ and when the official modules
  are available.
- Add conditional dependencies in `UnrealMcp.Build.cs` for official modules such
  as `ToolsetRegistry`, `ModelContextProtocol`, and any needed client/toolset
  modules.
- Keep all official-module includes behind the adapter boundary.
- Keep 5.6/5.7 byte-unaffected: no scattered version branches through Task Atlas
  or UI code, and no `.uplugin` required-plugin entry that makes old engines
  resolve unavailable UE5.8 Experimental modules.

Runtime behavior should feature-detect official support and return clear setup
status when official modules are missing or disabled.

---

## 7. Phasing, risks, and open decisions

### Phase A — validation milestone

- Build UEAtelier on UE5.8.
- Run Spike 2 two-server coexistence.
- Run Spike 3 MCPClientToolset-to-UEAtelier reachability.
- Add only diagnostics/probes needed to validate the official adapter boundary.

### Phase B — delegating adapter subset

- Add `UNREALMCP_HAS_OFFICIAL_TOOLSETS` and Build.cs conditional dependencies.
- Add a minimal official ToolsetRegistry adapter with one read-only generated
  wrapper that delegates back to UEAtelier policy execution.
- Prove official MCP calls cannot bypass policy/audit.

### Phase C — full Task Atlas surface

- Rewire Task Atlas Make Tool Set to the official Python generation path.
- Add manifests, rollback/delete, smoke, schema drift detection, and AgentSkill
  promotion.
- Update docs and release positioning based on validation evidence.

### Ranked risks

1. **NoRedist + Experimental churn**: official modules may change API or
   redistribution posture; UEAtelier must not ship Epic NoRedist code.
2. **Governance bypass**: generated official MCP tools could bypass UEAtelier
   approval/dry-run/audit unless wrappers delegate into the policy executor.
3. **Unverified two-server coexistence**: both servers use UE's HTTP server
   infrastructure; `:8765` + `:8000` must be proven in one editor.
4. **Client semantics drift**: official MCP tool-search mode exposes only
   meta-tools at top level; clients must use `list_toolsets`, `describe_toolset`,
   and `call_tool` correctly.
5. **Schema and naming limits**: official ToolsetRegistry names can be long and
   module-derived; generated names need collision and client-compat handling.

### Open decisions for the director

1. Is v0.33 validation-only, or should it produce a public UE5.8 preview
   artifact?
2. Should official `:8000` autostart by default, or remain explicitly opt-in?
3. Is delegation to the UEAtelier policy executor mandatory for every generated
   official tool? Recommendation: yes.
4. Where should generated official Python toolsets live:
   `Content/Python`, `Tools/UnrealMcpPyTools`, or a new
   `Tools/UnrealMcpOfficialToolsets` root?
5. Should AgentSkill promotion create instruction assets only, or always pair
   with a generated callable ToolsetDefinition?
6. Should restart-required C++ ToolsetDefinitions appear in UI, or remain
   developer-only export/promotion artifacts?
7. Which client matrix is required for official `:8000`: official clients only,
   or Codex/Claude/SDK clients as well?
8. If Spike 2 fails, do we adapt with delayed start/single-server bridge/separate
   process, or abandon dual-server usage for v0.33?

## R0 verdict

Proceed with UE5.8 validation only. Spike 1 removes the largest uncertainty for
runtime official Python authoring, but the plan is not shippable until Spike 2
proves `:8765` + `:8000` coexistence and until generated official tools are
forced through UEAtelier's policy/audit executor.
