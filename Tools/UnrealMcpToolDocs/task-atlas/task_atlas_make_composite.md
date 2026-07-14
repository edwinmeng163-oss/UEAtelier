# unreal.task_atlas_make_composite

**Category**: task-atlas
**Title**: Make Task Atlas Composite
**Risk level**: high

Turn a Task Atlas task into either a generated preview composite Python user tool or a document-only draft, depending on eligibility.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "taskId": {
      "type": "string",
      "minLength": 1,
      "description": "Task Atlas task id to classify and convert."
    },
    "preferDocumentOnly": {
      "type": "boolean",
      "default": false,
      "description": "Dry-run/document-only mode. When true, never writes Tools/UnrealMcpPyTools and writes/returns a markdown draft instead."
    },
    "forceWriteEvenIfBlocked": {
      "type": "boolean",
      "default": false,
      "description": "Developer escape hatch. Still requires AssistantRun approval when invoked by AI; UI must not set this."
    },
    "overrideStepArgs": {
      "type": "array",
      "description": "Optional strict override bridge for developer tools. Each item carries JSON as a string to keep the MCP schema closed.",
      "items": {
        "type": "object",
        "properties": {
          "ordinal": {
            "type": "integer",
            "minimum": 0
          },
          "toolName": {
            "type": "string",
            "minLength": 1
          },
          "argumentsJson": {
            "type": "string",
            "minLength": 2
          }
        },
        "required": [
          "ordinal",
          "argumentsJson"
        ],
        "additionalProperties": false
      },
      "default": []
    }
  },
  "required": [
    "taskId"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "taskId": "<string>"
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas Make Tool Set MCP wrapper over TaskAtlasService::MakeComposite.
- Notes: AssistantRun approval: required for generated PyTools writes and forceWriteEvenIfBlocked=true; preferDocumentOnly=true is the dry-run equivalent.
