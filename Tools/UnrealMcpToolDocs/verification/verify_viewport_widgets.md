# unreal.verify_viewport_widgets

**Category**: verification
**Title**: Verify Viewport Widgets
**Risk level**: read_only

Lists UUserWidget instances that are currently in the PIE viewport, optionally filtered by generated widget class path.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "widgetClassFilter": {
      "type": "string",
      "description": "Optional exact widget generated class path filter, for example /Game/UI/WBP_TestHUD.WBP_TestHUD_C."
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.28 read-only runtime verifier for on-screen PIE UMG widgets.
- Notes: Read-only PIE inspection; returns needsPie=true when no PIE session is active.
