# Unreal MCP Knowledge Sources

This folder stores versioned source manifests for UEAtelier's local knowledge/RAG
bootstrap. It should contain source lists, schemas, and small metadata files, not
downloaded third-party documentation payloads.

The generated local index writes `KnowledgeCard` JSONL records under
`Saved/UnrealMcp/KnowledgeIndex`. The versioned card schema lives at:

```text
Schemas/UnrealMcpKnowledgeCard.schema.json
```

`cards.jsonl` is written as UTF-8 JSONL so external scripts, CI checks, and
package validators can inspect the same index that the plugin reads.

Current retrieval is intentionally local-first and lexical: section-aware
chunks, boundary-aware Latin tokens, CJK bigrams, UE 5.7/5.8 version tokens,
lower-weight synonym expansion, source/engine diversity, source/confidence
weighting, and duplicate source-section suppression. KnowledgeIndex v2 binds
cards to hashes and freshness metadata and reports
`missing|empty|stale|ready|corrupt`. Verified outcome-card appends update the
card/manifest pair through staged candidates and a verified recoverable backup,
and fail closed when the base index is stale or corrupt. Plain numeric tokens
become engine filters only when the indexed versions or explicit UE context
identify them as engine versions. Embeddings can be added later as an optional backend, but the baseline
must keep working offline.

All public `sourceRoot` and `indexRoot` overrides are confined to the current
project's `Saved` directory. Eval JSON paths are limited to the current project
or the shared versioned eval root; traversal and unrelated absolute paths fail
closed. Recursive source/eval discovery does not follow symlinks or reparse
points, manifest `textPath` values stay inside their manifest directory, and
the fixed index leaves are revalidated before every read or replacement.

## Evals

Versioned RAG regression cases live under:

```text
Tools/UnrealMcpKnowledge/Evals
```

Run them from Chat or Workbench with:

```text
/tool unreal.knowledge_eval_run {"evalPath":"Tools/UnrealMcpKnowledge/Evals","indexRoot":"Saved/UnrealMcp/Tests/KnowledgeEval/Index","refreshIndex":true,"includeDetails":false}
```

The eval runner covers `knowledge_search`, `tool_recommend`,
`tool_gap_analyze`, and `workflow_recommend` so retrieval quality can be checked
after changing sources, scoring, synonyms, or ToolRegistry metadata. Eval v2
can assert exact source paths/tools at rank, required paths within K, and
forbidden top results.

High-value local RAG pages include deployment troubleshooting and Unreal task
recipes for first-person characters, Widget HUDs, Blueprint graph edits, and
self-extension tool creation.

## Official Unreal Engine Docs

The curated primary-engine seed lists are:

```text
Tools/UnrealMcpKnowledge/Sources/unreal_engine_official_docs_5_7.json
Tools/UnrealMcpKnowledge/Sources/unreal_engine_official_docs_5_8.json
```

Fetch the seed pages into a local ignored cache:

```bash
python3 Tools/unreal_mcp_fetch_docs.py --max-pages 20
python3 Tools/unreal_mcp_fetch_docs.py --seed-file Tools/UnrealMcpKnowledge/Sources/unreal_engine_official_docs_5_8.json --max-pages 20
```

Output defaults to:

```text
Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.7
Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.8
```

The downloader prefers Epic's structured documentation JSON endpoint for normal
documentation pages and falls back to static HTML for pages such as the Unreal
Python API. It preserves H1-H6 headings, validates seed engine versions, and
replaces existing URL version parameters when an override is explicit. Low
extracted text counts are flagged in the generated manifest so the indexer can
skip or deprioritize weak pages.

Do not commit fetched official documentation content unless the upstream license
explicitly allows redistribution. Commit only source manifests and downloader
code.
