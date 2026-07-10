# unreal.mcp_user_registry_reload

**Category**: self-extension
**Title**: Reload User Tool Registry
**Risk level**: low

Rescan project-local user Python tools at <projDir>/Tools/UnrealMcpPyTools/ and refresh the overlay registry. Hot-reloads main.py via Python module reimport (sys.modules.pop + reimport; no editor restart needed). Reports added/updated/removed/rejected tools with structured lifecycle.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "acceptChangedHashes": {
      "type": "boolean",
      "description": "If true, accept main.py edits that change sha256 (records new hash). Default false rejects with python_sha_mismatch.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "If true, walk and categorize without mutating overlay.",
      "default": false
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: Tools/UnrealMcpSkills/mcp-self-extension/SKILL.md
- Reason: Explicit registry: user-extension overlay refresh. Required v0.26 control tool for hot-reload of project-local Python user tools without editor restart.
