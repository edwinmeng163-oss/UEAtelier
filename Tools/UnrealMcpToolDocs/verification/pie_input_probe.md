# unreal.pie_input_probe

**Category**: verification
**Title**: Probe PIE Input Movement
**Risk level**: medium

Starts or polls an asynchronous PIE input probe that injects one gameplay input profile on the core ticker and reports movement or rotation deltas.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "description": "Start a new probe or poll an existing probe result.",
      "enum": [
        "start",
        "result"
      ]
    },
    "inputProfile": {
      "type": "string",
      "description": "Input profile to inject while action=start.",
      "enum": [
        "moveForward",
        "moveBackward",
        "moveRight",
        "moveLeft",
        "jump",
        "lookYaw",
        "lookPitch"
      ]
    },
    "durationSeconds": {
      "type": "number",
      "description": "Sampling duration in seconds for action=start.",
      "default": 0.5,
      "minimum": 0.05,
      "maximum": 5.0
    },
    "probeId": {
      "type": "string",
      "description": "Probe id returned by action=start; required when action=result."
    }
  },
  "required": [
    "action"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "action": "result",
  "probeId": "__missing_probe__"
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.28 runtime verification probe for real PIE movement or look deltas without editor tick pumping.
- Notes: action=start requires an already-active PIE session and returns immediately; action=result polls the cached sampling result by probeId.
