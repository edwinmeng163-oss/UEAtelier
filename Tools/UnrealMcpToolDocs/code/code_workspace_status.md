# unreal.code_workspace_status

**Category**: code
**Title**: Code Workspace Status
**Risk level**: read_only

Returns Code tool workspace roots, path policy, extension allowlists, latest manifest, and extension lock status.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {},
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
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: v0.29 Code tools read-only workspace policy status.
