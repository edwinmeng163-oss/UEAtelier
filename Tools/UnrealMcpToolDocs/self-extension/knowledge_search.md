# unreal.knowledge_search

**Category**: self-extension
**Title**: Search Knowledge Index
**Risk level**: read_only

Reads the local KnowledgeCard index and returns compact source-linked cards; use for planning, tool choice, and verification, and call unreal.knowledge_index_refresh first if the index is missing.

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
    "query": {
      "type": "string",
      "description": "Search query for local KnowledgeCards."
    },
    "categories": {
      "type": "array",
      "description": "Optional KnowledgeCard categories to include.",
      "items": {
        "type": "string"
      }
    },
    "sourceKinds": {
      "type": "array",
      "description": "Optional sourceKind values to include.",
      "items": {
        "type": "string",
        "enum": [
          "tool-registry",
          "versioned-doc",
          "official-docs",
          "skill",
          "runtime-memory",
          "activity-log",
          "test-fixture",
          "unknown"
        ]
      }
    },
    "groupByKind": {
      "type": "boolean",
      "description": "Group results by sourceKind instead of returning one flat results array.",
      "default": false
    },
    "indexRoot": {
      "type": "string",
      "description": "Optional index directory inside the current project's Saved directory. Defaults to Saved/UnrealMcp/KnowledgeIndex."
    },
    "limit": {
      "type": "number",
      "description": "Maximum search results.",
      "default": 8
    },
    "maxExcerptChars": {
      "type": "number",
      "description": "Maximum excerpt characters per result.",
      "default": 420
    },
    "includeText": {
      "type": "boolean",
      "description": "Include full card text in results. Off by default to keep Chat context compact.",
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
  "query": "self extension tool registry",
  "sourceKinds": [
    "versioned-doc",
    "tool-registry"
  ],
  "groupByKind": false,
  "indexRoot": "Saved/UnrealMcp/Tests/SelfExtensionKnowledge/Index",
  "limit": 5,
  "maxExcerptChars": 240
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: read-only retrieval over local KnowledgeCard indexes for planning and verification.
- Notes: If the index is missing, run unreal.knowledge_index_refresh and retry.
