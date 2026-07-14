# unreal.chat_tool_log_tail

**Category**: editor
**Title**: Tail Chat Tool Log
**Risk level**: read_only

Read the last N tool_call events from the current ActivityLog session (the events shown beside the chat dialog).

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
    "count": {
      "type": "integer",
      "minimum": 1,
      "maximum": 100,
      "default": 20,
      "description": "Maximum tool_call events to return."
    },
    "sessionId": {
      "type": "string",
      "default": "",
      "description": "Optional ActivityLog sessionId. Default: current process session."
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
- Source docs: Docs/ChatSync.md
- Reason: v0.31 R4 chunk 9 read-only editor Chat tool log tail.
- Notes: AssistantRun approval: not_required. Read-only chat dialog and tool log surfaces with body truncation and argument-value redaction.
