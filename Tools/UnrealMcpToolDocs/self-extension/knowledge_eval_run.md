# unreal.knowledge_eval_run

**Category**: self-extension
**Title**: Run Knowledge Evals
**Risk level**: low

Runs the offline RAG retrieval evaluation suite under Tools/UnrealMcpKnowledge/Evals/ and reports recall plus per-question diagnostics.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "evalPath": {
      "type": "string",
      "description": "Eval JSON file or directory inside the current project or the shared Tools/UnrealMcpKnowledge/Evals root."
    },
    "indexRoot": {
      "type": "string",
      "description": "Optional KnowledgeIndex root inside the current project's Saved directory. Use an isolated Saved root for repeatable evals."
    },
    "refreshIndex": {
      "type": "boolean",
      "description": "Refresh the local KnowledgeCard index before running evals.",
      "default": false
    },
    "includeDetails": {
      "type": "boolean",
      "description": "Include per-case structuredContent in the eval output.",
      "default": true
    },
    "limit": {
      "type": "number",
      "description": "Search/recommendation limit used by each eval case.",
      "default": 6
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
  "evalPath": "Tools/UnrealMcpKnowledge/Evals",
  "indexRoot": "Saved/UnrealMcp/Tests/KnowledgeEval/Index",
  "refreshIndex": true,
  "includeDetails": false,
  "limit": 6
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: refreshIndex can replace a bounded project-Saved KnowledgeIndex before running local evals; evalPath is limited to project/shared eval roots.
- Notes: Reads versioned eval cases from Tools/UnrealMcpKnowledge/Evals by default.
