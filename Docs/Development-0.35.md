# UEAtelier v0.35 Development Track

Status: in development as of 2026-07-10. This document records verified branch
state; it is not a public-release announcement. The latest public release
remains v0.34.0 until packaging and cross-platform release gates finish.

## Support decision

| Engine | v0.35 tier | Current evidence |
| --- | --- | --- |
| UE 5.8 | primary | clean Example57-host UBT; RAG 11/11; Gate D 1/1; EngineCompat 2/2; migration 1/1 |
| UE 5.7 | primary | clean Example57-host UBT; RAG 11/11; Gate D 1/1; EngineCompat 2/2; migration 1/1 |
| UE 5.6 | maintenance | compile-time floor retained; not a primary v0.35 release gate |

The root `UEvolve.uproject` now uses `EngineAssociation` `5.7`.
`Examples/UEvolveExample57` is reused as the UE 5.8 UBT/commandlet host; a
duplicate full-content UE 5.8 project is intentionally not committed. The old
`experiment/v0.33-ue58-validation` branch remains evidence only. v0.35
forward-ports the small JSON-object compatibility shim and unreachable-code
cleanup, not the old branch as a whole. Epic's official UE 5.8 MCP stack stays
optional and outside this batch.

The shared 5.7/5.8 example host drops the deprecated
`r.Mobile.VirtualTextures` key because UE 5.8 raises a startup ensure and
requires platform-specific `r.VirtualTextures` instead. The UE 5.6 maintenance
host keeps its original setting.

`unreal.project_version_migration` accepts `5.6`, `5.7`, and `5.8`, returns
`targetSupportTier=primary` for 5.7/5.8 and `maintenance` for 5.6, and rejects
unverified versions. The standalone installer applies the same classification
and requires `--allow-unverified-engine` for custom/missing associations.

## RAG reliability batch

The production failure fixed here was concrete: the Gate D RAG test used the
default KnowledgeIndex, deleted its synthetic source in cleanup, then refreshed
the default index to zero cards. Chat only recognized an English “not found”
message, so an empty index could silently disable RAG.

v0.35 changes that contract:

- Gate D and Knowledge eval fixtures use isolated `indexRoot` directories.
- A zero-card refresh fails closed by default and preserves last-known-good
  cards. `allowEmptyIndex=true` is an explicit test-only escape hatch.
- `cards.jsonl` and `index.json` are written through same-directory temporary
  files, restored on pair verification failure, and bound by `cardsSha256`.
- Index schema `UEvolve.KnowledgeIndex.v2` records `buildId`,
  `generatedAtUtc`, `sourceFingerprint`, card/source/engine counts,
  deduplication/truncation counts, and build parameters.
- Search/recommendation return machine-readable
  `missing|empty|stale|ready|corrupt` status. Chat refreshes from that field,
  not localized error text.
- ActivityLog indexing defaults to false. Skills remain enabled. Promoted local
  markdown uses `includePromotedSources` independently of official-doc caches.
- Public schemas now expose `includeActivityLog`, `includeSkills`,
  `includePromotedSources`, `allowEmptyIndex`, `sourceKinds`, `groupByKind`,
  and isolated `indexRoot` controls where implemented.
- An explicit repository-relative `Tools/UnrealMcpKnowledge/Evals` path now
  falls back to the shared repository resolver when the plugin runs from a
  nested example host; the fallback is limited to that canonical eval root.
- Verified outcome-card appends now rewrite `cards.jsonl` and `index.json` as
  one checked pair, update the v2 hash/count/fingerprint metadata, and refuse
  to hide a stale or corrupt base index.
- Public knowledge source/index overrides are confined to the current
  project's `Saved` directory, eval JSON reads are project/shared-eval only,
  and traversal/absolute-path escapes fail closed. Recursive source/eval scans
  do not follow symlinks or reparse points; manifest `textPath`, fixed
  `cards.jsonl`/`index.json` leaves, and card `sourcePath` metadata probes are
  revalidated at I/O time. `knowledge_eval_run` is a low-risk write because
  `refreshIndex:true` can replace its isolated index.

## Retrieval-quality batch

- Latin tokens use word boundaries, so `ui` no longer matches `build`.
- `5.7`, `5.8`, `UE5.7`, and `UE5.8` survive tokenization; original tokens
  rank above expanded synonyms, and comparison queries keep cards for every
  explicitly named engine version. CJK tokens gain adjacent bigrams.
- Official-doc rows and KnowledgeCards carry `engineVersion`; an explicit
  version query rejects cards for a different declared engine version.
- Card metadata separates `sourceUpdatedAt`, `indexedAt`, and
  `contentSha256`; `updatedAt` remains readable for backward compatibility.
- Global truncation reserves at least one card per source-kind/engine bucket,
  and equal-score ordering is deterministic by title, path, then card ID.
- Eval schema v2 supports `expectSourcePathsAtK`,
  `expectAnySourcePathContains`, `forbidTopSourcePathContains`, and
  `expectToolAtRank`, with aggregate rank-assertion counts/rate.

The official-doc fetcher now preserves H1-H6 as Markdown headings, requires a
seed engine version, replaces rather than ignores a CLI version override, keeps
5.7/5.8 caches separate, and writes engine metadata. Curated 5.7 and 5.8 seeds
live under `Tools/UnrealMcpKnowledge/Sources/`; fetched Epic content remains
local and ignored under `Saved/UnrealMcp/KnowledgeSources/`.

## Verification completed

- `python3 Tools/validate_tool_registry.py`: 190 tools, issueCount 0, mirror
  byte-identical (the 79 non-strict dispatch warnings are the existing baseline).
- `python3 Tools/check_ue56_compat.py --min-engine 5.7`: 0 errors, 0 warnings.
- Fetcher unit tests: 4/4; installer support-tier tests: 3/3.
- UE 5.7 and UE 5.8: clean Example57-host UBT, RAG reliability/retrieval,
  shared eval-path, outcome-append integrity, and path-containment coverage
  11/11, Gate D 1/1,
  EngineCompat 2/2, and
  ProjectVersionMigration support contract 1/1.
- Live UE 5.8 MCP `unreal.knowledge_eval_run`: 8/8 cases and 3/3 rank
  assertions with an isolated index.
- UE 5.7 and UE 5.8 safety baselines: VetMadeTool 11/11, VettedToolset 5/5,
  CallTool 9/9, and TaskAtlas 38/38 on each engine.

## Remaining release gates

- `sourceFingerprint` currently covers the source files represented by the
  final deduplicated/truncated card set; it is not a complete source-inventory
  hash. Adding a source file that produces no retained card may therefore stay
  `ready` until the caller explicitly refreshes the index.
- Run the complete `UnrealMcp.*` host automation matrix on UE 5.7 and UE 5.8
  and reconcile any result against the two documented baseline failures; this
  batch completed the changed-system and v0.34 safety suites, not every test.
- Verify Windows UBT cells for UE 5.7 and UE 5.8; the package-only workflow is
  not sufficient evidence by itself.
- Build macOS/Windows `ue57-ue58-projectroot` artifacts, run strict integrity,
  fresh-project install, endpoint smoke, and MCP SDK conformance before tagging.
- Do not call UE 5.8 publicly supported until those package gates pass.
