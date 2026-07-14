# unreal.chat_history_tail

**Category**: editor
**Title**: Tail Chat Panel History
**Risk level**: read_only

Read the last N entries from the editor Chat Panel persisted history (Saved/UnrealMcp/ChatHistory.json).

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
      "description": "Maximum chat history entries to return."
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
- Reason: v0.31 R4 chunk 9 read-only editor Chat Panel history tail.
- Notes: AssistantRun approval: not_required. Read-only chat dialog and tool log surfaces with body truncation and argument-value redaction.
