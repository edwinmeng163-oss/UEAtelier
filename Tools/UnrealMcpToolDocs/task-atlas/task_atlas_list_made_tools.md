# unreal.task_atlas_list_made_tools

**Category**: task-atlas
**Title**: List Task Atlas Made Tools
**Risk level**: read_only

List Task Atlas generated user composites for CLI diagnostics and Made Tools views.

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
  "properties": {
    "includeStale": {
      "type": "boolean",
      "default": true,
      "description": "Include generated dirs that are not currently loaded in the user registry."
    },
    "includeFailureMarkers": {
      "type": "boolean",
      "default": true,
      "description": "Include generated_smoke_failed and MakeToolSetFailures diagnostic links when present."
    },
    "sourceTaskId": {
      "type": "string",
      "default": "",
      "description": "Optional exact task id filter. Empty means no filter."
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
- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 read-only Task Atlas generated composite listing wrapper.
- Notes: AssistantRun approval: not_required. Read-only registry and generated-dir metadata listing.
