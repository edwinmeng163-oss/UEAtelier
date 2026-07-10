# unreal.task_atlas_promote_to_rag

**Category**: task-atlas
**Title**: Promote Task Atlas Task To RAG
**Risk level**: medium

Promote a Task Atlas task or draft into a RAG knowledge source and refresh the knowledge index.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
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
      "description": "Task Atlas task id to promote."
    },
    "dryRun": {
      "type": "boolean",
      "default": false,
      "description": "When true, report source/target paths and refresh plan without writing KnowledgeSources."
    },
    "refreshIndex": {
      "type": "boolean",
      "default": true,
      "description": "Run knowledge_index_refresh after writing the source."
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
- Reason: v0.31 Task Atlas RAG promotion wrapper over TaskAtlasService::PromoteToRag.
- Notes: AssistantRun approval: required for dryRun=false because this writes long-lived KnowledgeSources. refreshIndex=false real writes are refused by the wrapper until service support exists.
