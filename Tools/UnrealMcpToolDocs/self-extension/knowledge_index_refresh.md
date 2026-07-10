# unreal.knowledge_index_refresh

**Category**: self-extension
**Title**: Refresh Knowledge Index
**Risk level**: low

Rebuilds the local Saved/UnrealMcp/KnowledgeIndex/ JSONL index from fetched docs plus visible tool metadata for RAG retrieval; call after upstream docs change or after a registry-changing chunk.

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
    "sourceRoot": {
      "type": "string",
      "description": "Optional project-Saved root containing fetched knowledge sources. Must remain inside the current project's Saved directory; defaults to Saved/UnrealMcp/KnowledgeSources."
    },
    "indexRoot": {
      "type": "string",
      "description": "Optional project-Saved output directory for the generated KnowledgeCard index. Must remain inside the current project's Saved directory; defaults to Saved/UnrealMcp/KnowledgeIndex."
    },
    "includeOfficialDocs": {
      "type": "boolean",
      "description": "Include fetched official documentation documents.jsonl files.",
      "default": true
    },
    "includePromotedSources": {
      "type": "boolean",
      "description": "Include promoted local markdown sources such as Task Atlas RAG cards independently of official docs.",
      "default": true
    },
    "includeVersionedDocs": {
      "type": "boolean",
      "description": "Include versioned README/Docs markdown files.",
      "default": true
    },
    "includeToolRegistry": {
      "type": "boolean",
      "description": "Include visible ToolRegistry entries as searchable tool cards.",
      "default": true
    },
    "includeActivityLog": {
      "type": "boolean",
      "description": "Include local activity-log summary cards. Disabled by default because logs can contain project-specific context.",
      "default": false
    },
    "includeSkills": {
      "type": "boolean",
      "description": "Include promoted project skills as searchable cards.",
      "default": true
    },
    "allowEmptyIndex": {
      "type": "boolean",
      "description": "Allow an explicitly empty index for tests. By default, an empty refresh preserves the last-known-good index.",
      "default": false
    },
    "skipLowContent": {
      "type": "boolean",
      "description": "Skip source rows flagged as low-content by the docs fetcher.",
      "default": true
    },
    "maxCards": {
      "type": "number",
      "description": "Maximum KnowledgeCards to write.",
      "default": 2000
    },
    "maxChunkChars": {
      "type": "number",
      "description": "Maximum text characters per card chunk.",
      "default": 1800
    },
    "chunkOverlapChars": {
      "type": "number",
      "description": "Overlapping characters between adjacent chunks.",
      "default": 160
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview source/card counts without writing index files.",
      "default": false
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
  "includeOfficialDocs": false,
  "includePromotedSources": false,
  "includeVersionedDocs": true,
  "includeToolRegistry": true,
  "includeActivityLog": false,
  "includeSkills": true,
  "allowEmptyIndex": false,
  "indexRoot": "Saved/UnrealMcp/Tests/SelfExtensionKnowledge/Index",
  "maxCards": 60,
  "maxChunkChars": 1200,
  "chunkOverlapChars": 80
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: writes local Saved/UnrealMcp KnowledgeCard indexes for RAG search over docs and tool metadata.
- Notes: Downloaded official docs remain under ignored Saved/UnrealMcp; this tool writes only the local index.
