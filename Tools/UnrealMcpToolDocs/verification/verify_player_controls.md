# unreal.verify_player_controls

**Category**: verification
**Title**: Verify Player Controls
**Risk level**: medium

Verifies PIE possession, pawn class, camera/spring arm components, and Jump/move/look binding existence without injecting input or checking movement deltas.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "expectedPawnClass": {
      "type": "string",
      "description": "Optional expected pawn class path, for example /Game/Player/BP_Player.BP_Player_C or /Script/Engine.Character."
    },
    "startIfNeeded": {
      "type": "boolean",
      "description": "Start PIE when no PIE session is active; the tool waits privately for BeginPIE before inspecting.",
      "default": false
    },
    "stopAfter": {
      "type": "boolean",
      "description": "Stop PIE after verification. If omitted at runtime, auto-started PIE is stopped and pre-existing PIE is left running.",
      "default": false
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "timeoutSeconds": 10,
  "aliveWindowSeconds": 30,
  "startIfNeeded": false
}
```

## Provenance
- Source docs: Tools/UnrealMcpToolDocs/verification/verify_player_controls.md
- Reason: v0.27.1 core runtime setup verifier that waits privately for BeginPIE and performs existence-only control checks.
- Notes: Existence and possession verifier only: no input injection and no movement-delta assertion. A private BeginPIE wait helper is used when startIfNeeded=true.
