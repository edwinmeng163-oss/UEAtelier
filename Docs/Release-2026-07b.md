# UEAtelier Release Notes - v0.35.0 (2026-07)

> Trilingual: [English](#english) · [中文](#中文) · [日本語](#日本語)
> Public release: UE 5.7/5.8 primary source targets + knowledge-index reliability and retrieval-quality overhaul (v0.35 Batch 1)
> Previous public release: [`Docs/Release-2026-07.md`](Release-2026-07.md) (v0.34.0).

---

## English

### Summary

v0.35.0 makes Unreal Engine 5.7 and 5.8 the primary source targets (UE 5.6 stays a maintenance compile canary) and overhauls the RAG knowledge layer: the index can no longer be destroyed by an interrupted refresh, and retrieval is deterministic across platforms and languages. This is Batch 1 of the v0.35 plan; the dual-variant structure and optional Epic official-MCP integration remain a later batch (`Docs/Development-0.35.md`).

Tool count stays 190 (visible `tools/list` count stays 178). No schema migration is required.

### What's New

1. **UE 5.7 / UE 5.8 primary**: the root host targets 5.7 and `Examples/UEvolveExample57` is reused as the 5.8 validation host. All engine-version compatibility logic stays confined to `UnrealMcpEngineCompat.h`; the compat validator gains a UE 5.8 `FJsonObject::Values` pattern.
2. **KnowledgeIndex v2 reliability**: `cards.jsonl`/`index.json` are staged, verified, and replaced through recoverable last-known-good `.bak` pairs; an interrupted write auto-recovers on the next load. Search/recommend/refresh report machine-readable `missing|empty|stale|ready|corrupt` states; warm reads use a size/timestamp fast path.
3. **Retrieval quality**: deterministic ASCII/CJK tokenization (Latin tokens inside 中文/日本語 prose stay searchable; CJK bigrams behave identically on every platform), numeric version filtering only for known engine versions (a `0.35` or `3.11` query no longer hides versioned docs), original tokens rank above synonym expansions, and results reserve source-kind/engine diversity.
4. **Official-doc pipeline**: curated UE 5.7 and UE 5.8 seed lists, per-entry `engineVersion` metadata, version-separated caches, and Markdown heading preservation; skipped HTML regions no longer leak markers into card text.
5. **`unreal.project_version_migration`** reports support tiers: primary 5.7/5.8, maintenance 5.6.
6. **Windows CI hardening**: artifact-only candidate packages from an exact branch SHA, a fail-closed tag↔`VersionName` gate, `persist-credentials: false`, and the Python test suite wired into pre-package validation.
7. **Packaged CLIs**: `Tools/unreal_mcp_fetch_docs.py` and `Tools/install_unrealmcp_to_project.py` (with an engine-support gate) now ship in the projectroot zips, whose names carry the `ue57-ue58` suffix.

### Reliability & Safety Model

Index containment is fail-closed: RAG roots, recursive scans, manifest `textPath` reads, fixed index leaves, and eval files are confined without following symlinks or reparse points. `allowEmptyIndex` is hard-gated to explicit test roots under `Saved/UnrealMcp/Tests`. The in-editor chat auto-refreshes a bad index at most once per panel session. `unreal.knowledge_eval_run` is now classified as a low-risk write tool (`requiresWrite=true`) because it can refresh isolated eval roots; strictly read-only clients (`riskMax=read_only`) will no longer see it.

### Upgrade Notes

Drop-in upgrade from v0.34.0 for UE 5.7 and UE 5.8 hosts. UE 5.6 still compiles as a maintenance canary, but v0.35 public packages target 5.7/5.8; the last 5.6-packaged line is v0.34.0. A pre-v0.35 knowledge index is rewritten to Index v2 on its first refresh; that first rewrite predates the `.bak` protection, so run it once from a healthy editor session.

### Verification

- UE 5.7 + UE 5.8: clean Example57-host UBT builds from wiped intermediates.
- Per engine: RAG reliability/retrieval 17/17, Gate D 1/1, EngineCompat 2/2, version migration 1/1, VetMadeTool 11/11, VettedToolset 5/5, CallTool 9/9, TaskAtlas 38/38.
- Full-host automation on both engines converges to the two known baseline failures (`RunRecoversStale`, `PieSmoke.MapValidation`).
- `python3 Tools/validate_tool_registry.py` stays clean at 190/190; Python fetcher/installer tests 8/8.
- Stage 2 e2e on fresh `/tmp` projects for UE 5.7 and UE 5.8: extract, UBT build, editor boot, port 8765, MCP SDK conformance, and smoke calls.
- Windows zips are packaged by the tag-push CI from the tagged revision; the Windows UE 5.7/5.8 UBT validation report is tracked as a follow-up issue.

### Asset Naming

```text
UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip
SHA-256: da235d1ad42db13fec2b9b312e961ba79cb63282f4c0ea03bb62be281b07cb53
SHA-256 sidecar: UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256

UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
SHA-256: f78fa6f219bb2ca38a54d17cf1cf6dfe2dce0e22be40d37f1f19f5297092d37f
SHA-256 sidecar: UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

---

## 中文

### 摘要

v0.35.0 将 Unreal Engine 5.7 和 5.8 设为主要源码目标（UE 5.6 仅保留维护性编译 canary），并全面加固 RAG 知识层：refresh 中断不再可能摧毁索引，检索在所有平台和语言下都是确定性的。本版本是 v0.35 计划的 Batch 1；dual-variant 结构与可选的 Epic 官方 MCP 集成留待后续 batch（见 `Docs/Development-0.35.md`）。

工具总数保持 190（可见 `tools/list` 数保持 178），无需 schema 迁移。

### 新特性

1. **UE 5.7 / UE 5.8 主目标**：根项目主机指向 5.7，`Examples/UEvolveExample57` 复用为 5.8 验证主机。所有引擎版本兼容逻辑仍集中在 `UnrealMcpEngineCompat.h`；兼容性校验器新增 UE 5.8 `FJsonObject::Values` 模式。
2. **KnowledgeIndex v2 可靠性**：`cards.jsonl`/`index.json` 通过可恢复的 last-known-good `.bak` 对进行 staged/verified 替换；写入被中断时下次加载自动恢复。搜索/推荐/刷新返回机器可读的 `missing|empty|stale|ready|corrupt` 状态；未变化的热读取走 size/timestamp 快路径。
3. **检索质量**：确定性的 ASCII/CJK 分词（中文/日文文本内嵌的 Latin token 可检索；CJK bigram 跨平台一致），数字版本过滤仅对已知引擎版本生效（查询 `0.35` 或 `3.11` 不再隐藏带版本的文档），原始 token 排名高于同义词扩展，结果保留 source-kind/引擎多样性。
4. **官方文档管线**：UE 5.7 与 5.8 的 curated seed 列表、逐条 `engineVersion` 元数据、按版本分离的缓存、Markdown 标题保留；被跳过的 HTML 区域不再向卡片文本泄漏标记。
5. **`unreal.project_version_migration`** 报告支持层级：primary 5.7/5.8、maintenance 5.6。
6. **Windows CI 加固**：可从精确 branch SHA 打 artifact-only 候选包、fail-closed 的 tag↔`VersionName` 校验、`persist-credentials: false`、Python 测试并入 pre-package 校验。
7. **打包 CLI**：`Tools/unreal_mcp_fetch_docs.py` 与 `Tools/install_unrealmcp_to_project.py`（带引擎支持门槛）随 projectroot zip 发布，zip 名称使用 `ue57-ue58` 后缀。

### 可靠性与安全模型

索引 containment 为 fail-closed：RAG 根目录、递归扫描、manifest `textPath` 读取、固定索引叶文件与 eval 文件都被限制在边界内，且不跟随 symlink/reparse point。`allowEmptyIndex` 硬性限制在 `Saved/UnrealMcp/Tests` 下的显式测试根。编辑器内聊天对坏索引的自动刷新每个面板会话至多一次。`unreal.knowledge_eval_run` 现归类为低风险写工具（`requiresWrite=true`），严格只读客户端（`riskMax=read_only`）将不再看到它。

### 升级说明

对 UE 5.7 / UE 5.8 主机可从 v0.34.0 直接升级。UE 5.6 仍可编译（维护 canary），但 v0.35 公开包面向 5.7/5.8；最后一个包含 5.6 打包的版本线是 v0.34.0。v0.35 之前的知识索引会在首次 refresh 时重写为 Index v2；该首次重写早于 `.bak` 保护生效，请在健康的编辑器会话中执行一次。

### 验证

- UE 5.7 + UE 5.8：从清空的 intermediates 完成干净的 Example57-host UBT 构建。
- 每引擎：RAG 可靠性/检索 17/17、Gate D 1/1、EngineCompat 2/2、版本迁移 1/1、VetMadeTool 11/11、VettedToolset 5/5、CallTool 9/9、TaskAtlas 38/38。
- 两个引擎的 full-host automation 均收敛到两个已知 baseline failures（`RunRecoversStale`、`PieSmoke.MapValidation`）。
- `python3 Tools/validate_tool_registry.py` 保持 190/190 干净；Python fetcher/installer 测试 8/8。
- UE 5.7 与 UE 5.8 各自在全新 `/tmp` 项目上完成 Stage 2 e2e：解压、UBT 构建、编辑器启动、端口 8765、MCP SDK conformance 与 smoke 调用。
- Windows zip 由 tag 推送触发的 CI 从 tagged revision 打包；Windows UE 5.7/5.8 UBT 验证报告作为后续 issue 跟踪。

### 资产命名

```text
UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip
SHA-256: da235d1ad42db13fec2b9b312e961ba79cb63282f4c0ea03bb62be281b07cb53
SHA-256 sidecar: UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256

UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
SHA-256: f78fa6f219bb2ca38a54d17cf1cf6dfe2dce0e22be40d37f1f19f5297092d37f
SHA-256 sidecar: UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```

---

## 日本語

### 概要

v0.35.0 は Unreal Engine 5.7 と 5.8 を主要ソースターゲットにし（UE 5.6 はメンテナンス用コンパイル canary として維持）、RAG ナレッジ層を全面的に強化します。refresh の中断でインデックスが破壊されることはなくなり、検索はプラットフォームと言語を問わず決定的です。本リリースは v0.35 計画の Batch 1 です。dual-variant 構造と Epic 公式 MCP 統合（オプション）は後続の batch で扱います（`Docs/Development-0.35.md`）。

ツール総数は 190 のまま（可視 `tools/list` 数は 178 のまま）で、schema 移行は不要です。

### 新機能

1. **UE 5.7 / UE 5.8 プライマリ**: root host は 5.7 を使用し、`Examples/UEvolveExample57` を 5.8 検証ホストとして再利用します。エンジンバージョン互換ロジックは `UnrealMcpEngineCompat.h` に集約され、互換バリデータに UE 5.8 の `FJsonObject::Values` パターンが追加されました。
2. **KnowledgeIndex v2 の信頼性**: `cards.jsonl`/`index.json` は復旧可能な last-known-good `.bak` ペアを介して staged/verified 置換されます。書き込みが中断されても次回ロード時に自動復旧します。検索/推薦/refresh は機械可読の `missing|empty|stale|ready|corrupt` 状態を返し、変更のないウォームリードは size/timestamp の高速パスを使います。
3. **検索品質**: 決定的な ASCII/CJK トークン化(中文・日本語文中の Latin トークンも検索可能、CJK bigram は全プラットフォームで同一挙動)、既知エンジンバージョンのみに作用する数値バージョンフィルタ(`0.35` や `3.11` のクエリがバージョン付きドキュメントを隠しません)、オリジナルトークンがシノニム展開より上位、source-kind/エンジンの多様性を保持。
4. **公式ドキュメントパイプライン**: UE 5.7 / 5.8 の curated seed、エントリ毎の `engineVersion` メタデータ、バージョン別キャッシュ、Markdown 見出し保持。スキップされた HTML 領域からカードテキストへのマーカー漏れはなくなりました。
5. **`unreal.project_version_migration`** がサポート層を報告します: primary 5.7/5.8、maintenance 5.6。
6. **Windows CI 強化**: 正確な branch SHA からの artifact-only 候補パッケージ、fail-closed の tag↔`VersionName` ゲート、`persist-credentials: false`、Python テストの pre-package 検証への組み込み。
7. **同梱 CLI**: `Tools/unreal_mcp_fetch_docs.py` と `Tools/install_unrealmcp_to_project.py`(エンジンサポートゲート付き)が projectroot zip に同梱され、zip 名は `ue57-ue58` サフィックスになります。

### 信頼性・安全モデル

インデックスの containment は fail-closed です。RAG ルート、再帰スキャン、manifest `textPath` 読み取り、固定インデックスリーフ、eval ファイルは symlink/reparse point を辿らずに境界内へ制限されます。`allowEmptyIndex` は `Saved/UnrealMcp/Tests` 配下の明示的なテストルートに限定されます。エディタ内チャットの不良インデックス自動 refresh はパネルセッション毎に最大 1 回です。`unreal.knowledge_eval_run` は低リスク書き込みツール(`requiresWrite=true`)に再分類されたため、厳密な読み取り専用クライアント(`riskMax=read_only`)からは見えなくなります。

### アップグレードノート

UE 5.7 / UE 5.8 ホストでは v0.34.0 からドロップインでアップグレードできます。UE 5.6 は引き続きコンパイル可能(メンテナンス canary)ですが、v0.35 の公開パッケージは 5.7/5.8 向けです。5.6 向けパッケージを含む最後のラインは v0.34.0 です。v0.35 以前のナレッジインデックスは初回 refresh で Index v2 に書き換えられます。この初回書き換えは `.bak` 保護より前に走るため、健全なエディタセッションで一度実行してください。

### 検証

- UE 5.7 + UE 5.8: intermediates を消去した状態からのクリーンな Example57-host UBT ビルド。
- エンジン毎: RAG 信頼性/検索 17/17、Gate D 1/1、EngineCompat 2/2、バージョン移行 1/1、VetMadeTool 11/11、VettedToolset 5/5、CallTool 9/9、TaskAtlas 38/38。
- 両エンジンの full-host automation は 2 つの既知 baseline failures(`RunRecoversStale`、`PieSmoke.MapValidation`)に収束します。
- `python3 Tools/validate_tool_registry.py` は 190/190 でクリーン。Python fetcher/installer テスト 8/8。
- UE 5.7 / UE 5.8 それぞれで新規 `/tmp` プロジェクトの Stage 2 e2e(展開、UBT ビルド、エディタ起動、ポート 8765、MCP SDK conformance、smoke 呼び出し)を完了。
- Windows zip は tag push CI が tagged revision からパッケージします。Windows UE 5.7/5.8 UBT 検証レポートはフォローアップ issue として追跡します。

### アセット命名

```text
UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip
SHA-256: da235d1ad42db13fec2b9b312e961ba79cb63282f4c0ea03bb62be281b07cb53
SHA-256 sidecar: UnrealMcp-v0.35.0-mac-ue57-ue58-projectroot.zip.sha256

UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip
SHA-256: f78fa6f219bb2ca38a54d17cf1cf6dfe2dce0e22be40d37f1f19f5297092d37f
SHA-256 sidecar: UnrealMcp-v0.35.0-win-ue57-ue58-projectroot.zip.sha256
```
