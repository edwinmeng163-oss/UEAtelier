# UEAtelier Release Notes — v0.32.2 (2026-06)

> Trilingual: [中文](#中文) · [English](#english) · [日本語](#日本語)
> Patch release: MCP protocol conformance fix for codex-cli 0.132 / rmcp 0.15 strict clients + wire-shape guardrails
> Previous release: [`Docs/Release-2026-06b.md`](Release-2026-06b.md) (v0.32.1)

---

## 中文

### 更新摘要

v0.32.2 是 v0.32.1 的 MCP 协议合规补丁版,两项交付:

1. **修复 codex-cli 0.132(rmcp 0.15)无法发现/调用 unreal.* 工具的问题**(`MCP startup failed: Unexpected response type`)
2. **wire 形状双层护栏**:协议响应精确键集 Automation 测试 + 官方 SDK 合规冒烟(进发布门)

### 工具数

**190 不变**(canonical 与 plugin 镜像 byte-equivalent;`tools/list` 可见数 178 不变;本版无工具增删)。

### MCP 协议合规修复

codex-cli 0.132 将其 MCP 客户端切换到官方 rmcp 0.15 Rust SDK。rmcp 用 untagged 枚举按声明顺序解析 JSON-RPC result,`CallToolResult` 排在 `ListToolsResult` 之前,且其反序列化器接受任何带 `structuredContent` 的对象——本插件 `tools/list` result 携带的非规范顶层 `structuredContent: {toolsListVersion}` 字段导致整个响应被误判为 `CallToolResult`,MCP 客户端启动失败,编辑器全部 unreal.* 工具对 codex 会话不可见(Plan B bridge 与原生 `~/.codex/config.toml` 注册两条路径同时受影响)。

修复:从 `tools/list` result 中移除该字段。聊天面板在进程内直接读取版本计数器,wire 上不存在软件消费者;`tools/call` result 内的 `structuredContent` 是规范合法用法,不受影响。

### Wire 形状护栏

- **协议响应纯函数化 + 精确键集测试**:协议层响应构造抽取为 `UnrealMcp::Protocol` 纯函数(`UnrealMcpProtocolBuilders.h`),新增 5 个 `UnrealMcp.Protocol.*` Automation 测试,断言 initialize / ping / tools-list / tool-call / JSON-RPC 信封的**精确键集合**(tools/list 显式断言 `structuredContent` 不存在,作为本次事故的回归钉)。未来任何非规范字段污染都会在 commandlet 自动化中直接红灯。
- **官方 SDK 合规冒烟**:新增 `Tools/UnrealMcpCodexBridge/test-sdk-conformance.ts`(`@modelcontextprotocol/sdk` 1.29.0,devDependency,产品包仍为 source-only),对活体编辑器端点执行 initialize / listTools / callTool + GET/DELETE 405 + 通知 202 探针,已写入 Stage 2 发布 SOP 第 6 步作为必过门:`bun install --cwd Tools/UnrealMcpCodexBridge` 后 `bun run --cwd Tools/UnrealMcpCodexBridge test-sdk-conformance.ts`。
- **客户端版本矩阵**:bridge README 的钉定版本升级为矩阵(codex-cli 0.130.0 原始探针线 / 0.132.0 + rmcp 0.15 当前验证线),并注明 Codex Desktop 会自动升级 CLI,升级后需复核矩阵(`codex --version`)。

### 兼容性提示

- 此前 `tools/list` result 顶层的 `structuredContent.toolsListVersion` 已移除。若有外部脚本曾消费该字段,请改用工具计数变化或 `unreal.mcp_tool_audit`。
- 升级后需重启 Unreal 编辑器加载新插件二进制;若使用 Codex Desktop bridge(Plan B),编辑器重启后也请重启 bridge,使其 `codex app-server` 子进程以干净状态重建 MCP 客户端。

### 验证

- UE 5.6 + 5.7 双引擎构建通过;注册表与 5.6 兼容校验器干净(190/190,0 errors)。
- 5.7 example host 全量 `UnrealMcp.*` 自动化收敛到两个已知失败(RunRecoversStale、PieSmoke.MapValidation);5 个新 `UnrealMcp.Protocol.*` 测试全过。
- 官方 SDK 活体冒烟 6/6 通过;CLI↔Chat 端到端实测通过(`chat_inject_user_input` → codex 经 MCP 调用 `unreal.editor_status` → 正常回复)。
- 全新 `/tmp` 工程 Stage 2 zip e2e 通过(含新增的 SDK 冒烟门步骤)。

### 校验(Verify)

```text
UnrealMcp-v0.32.2-mac-ue56-ue57-projectroot.zip
SHA-256: 2f13f966dc9e69061c0d3b9e85cc1577c04223bce4e434bf287c94e1b7d3855b
UnrealMcp-v0.32.2-win-ue56-ue57-projectroot.zip
SHA-256: 见 GitHub Release 资产页(Win CI 产出)
```

---

## English

### Summary

v0.32.2 is an MCP protocol conformance patch on v0.32.1, with two deliverables:

1. **Fix codex-cli 0.132 (rmcp 0.15) failing to discover/call unreal.* tools** (`MCP startup failed: Unexpected response type`)
2. **Two-layer wire-shape guardrails**: exact-key protocol Automation tests + an official-SDK conformance smoke wired into the release gate

### Tool count

**Unchanged at 190** (canonical and plugin mirror byte-equivalent; visible `tools/list` count stays 178; no tools added or removed).

### MCP protocol conformance fix

codex-cli 0.132 switched its MCP client to the official rmcp 0.15 Rust SDK. rmcp parses JSON-RPC results through an untagged enum tried in declaration order; `CallToolResult` precedes `ListToolsResult`, and its deserializer accepts any object carrying `structuredContent`. The non-spec top-level `structuredContent: {toolsListVersion}` field this plugin attached to the `tools/list` result therefore made the whole response parse as a `CallToolResult`, failing MCP client startup and hiding every unreal.* tool from codex sessions (both the Plan B bridge and native `~/.codex/config.toml` registrations were affected).

The fix removes that field from the `tools/list` result. The chat panel reads the version counter in-process, so no software consumer existed on the wire; `structuredContent` inside `tools/call` results is spec-legal and untouched.

### Wire-shape guardrails

- **Pure protocol builders + exact-key tests**: protocol response construction is extracted into pure `UnrealMcp::Protocol` functions (`UnrealMcpProtocolBuilders.h`), with five new `UnrealMcp.Protocol.*` Automation tests asserting the **exact key set** of initialize / ping / tools-list / tool-call / JSON-RPC envelopes (tools/list carries an explicit structuredContent-absent regression pin). Any future non-spec key pollution fails commandlet automation immediately.
- **Official-SDK conformance smoke**: new `Tools/UnrealMcpCodexBridge/test-sdk-conformance.ts` (`@modelcontextprotocol/sdk` 1.29.0, devDependency only; the packaged bridge stays source-only) drives a live editor endpoint through initialize / listTools / callTool plus raw GET/DELETE 405 and notification 202 probes. It is now step 6 of the Stage 2 release SOP: `bun install --cwd Tools/UnrealMcpCodexBridge`, then `bun run --cwd Tools/UnrealMcpCodexBridge test-sdk-conformance.ts`.
- **Tested-client matrix**: the bridge README's pinned version becomes a matrix (codex-cli 0.130.0 original probe line; 0.132.0 / rmcp 0.15 current verified line) with a note that Codex Desktop auto-updates the CLI, so re-verify after updates (`codex --version`).

### Compatibility notes

- The former top-level `structuredContent.toolsListVersion` on the `tools/list` result is gone. External scripts that consumed it should switch to tool-count deltas or `unreal.mcp_tool_audit`.
- Restart the Unreal Editor after upgrading. If you use the Codex Desktop bridge (Plan B), restart the bridge after the editor so its `codex app-server` child rebuilds the MCP client from a clean state.

### Verification

- UE 5.6 + 5.7 dual-engine builds pass; registry and 5.6-compat validators clean (190/190, 0 errors).
- Full `UnrealMcp.*` automation on the 5.7 example host converges to the two known failures (RunRecoversStale, PieSmoke.MapValidation); all five new `UnrealMcp.Protocol.*` tests pass.
- Official-SDK live smoke passes 6/6; CLI↔Chat end-to-end verified (`chat_inject_user_input` → codex calls `unreal.editor_status` over MCP → clean reply).
- Stage 2 zip e2e on a fresh `/tmp` project passes, including the new SDK conformance gate step.

### Verify

```text
UnrealMcp-v0.32.2-mac-ue56-ue57-projectroot.zip
SHA-256: 2f13f966dc9e69061c0d3b9e85cc1577c04223bce4e434bf287c94e1b7d3855b
UnrealMcp-v0.32.2-win-ue56-ue57-projectroot.zip
SHA-256: see the GitHub Release asset page (produced by Win CI)
```

---

## 日本語

### 更新概要

v0.32.2 は v0.32.1 に対する MCP プロトコル適合パッチで、2 つの内容を含みます:

1. **codex-cli 0.132(rmcp 0.15)が unreal.* ツールを発見/呼び出しできない問題の修正**(`MCP startup failed: Unexpected response type`)
2. **ワイヤ形状の二層ガードレール**:プロトコル応答の厳密キー集合 Automation テスト + 公式 SDK 適合スモーク(リリースゲート化)

### ツール数

**190 のまま変更なし**(canonical とプラグインミラーは byte-equivalent;`tools/list` の可視数 178 も変更なし;ツールの追加・削除なし)。

### MCP プロトコル適合修正

codex-cli 0.132 は MCP クライアントを公式 rmcp 0.15 Rust SDK に切り替えました。rmcp は JSON-RPC result を untagged enum で宣言順に解析し、`CallToolResult` が `ListToolsResult` より先に試され、`structuredContent` を持つ任意のオブジェクトを受理します。本プラグインが `tools/list` result に付けていた非標準のトップレベル `structuredContent: {toolsListVersion}` により、応答全体が `CallToolResult` と誤判定され、MCP クライアントの起動が失敗し、すべての unreal.* ツールが codex セッションから不可視になっていました(Plan B bridge とネイティブ `~/.codex/config.toml` 登録の両経路に影響)。

修正としてこのフィールドを `tools/list` result から除去しました。チャットパネルはバージョンカウンタをプロセス内で直接読むため、ワイヤ上にソフトウェア消費者は存在しません。`tools/call` result 内の `structuredContent` は仕様準拠の正当な使用であり、変更ありません。

### ワイヤ形状ガードレール

- **プロトコル応答の純関数化 + 厳密キーテスト**:応答構築を `UnrealMcp::Protocol` 純関数(`UnrealMcpProtocolBuilders.h`)へ抽出し、initialize / ping / tools-list / tool-call / JSON-RPC エンベロープの**厳密なキー集合**を検証する 5 つの `UnrealMcp.Protocol.*` Automation テストを追加(tools/list には structuredContent 不在の回帰ピン付き)。
- **公式 SDK 適合スモーク**:新規 `Tools/UnrealMcpCodexBridge/test-sdk-conformance.ts`(`@modelcontextprotocol/sdk` 1.29.0、devDependency のみ。配布 bridge は source-only のまま)。Stage 2 リリース SOP のステップ 6 として必須化:`bun install --cwd Tools/UnrealMcpCodexBridge` の後 `bun run --cwd Tools/UnrealMcpCodexBridge test-sdk-conformance.ts`。
- **検証済みクライアントマトリクス**:bridge README に codex-cli 0.130.0(初期プローブ)と 0.132.0 / rmcp 0.15(現行検証ライン)を記載し、Codex Desktop が CLI を自動更新する旨の注意書きを追加(更新後は `codex --version` で再確認)。

### 互換性に関する注意

- `tools/list` result のトップレベル `structuredContent.toolsListVersion` は削除されました。外部スクリプトで消費していた場合は、ツール数の変化または `unreal.mcp_tool_audit` をご利用ください。
- アップグレード後は Unreal エディタを再起動してください。Codex Desktop bridge(Plan B)使用時は、エディタ再起動後に bridge も再起動し、`codex app-server` 子プロセスに MCP クライアントをクリーンな状態で再構築させてください。

### 検証

- UE 5.6 + 5.7 デュアルエンジンビルド成功。レジストリ / 5.6 互換バリデータはクリーン(190/190、0 errors)。
- 5.7 example host のフル `UnrealMcp.*` automation は既知の 2 失敗(RunRecoversStale、PieSmoke.MapValidation)に収束。新規 5 つの `UnrealMcp.Protocol.*` テストはすべて成功。
- 公式 SDK ライブスモーク 6/6 成功。CLI↔Chat の end-to-end 検証済み(`chat_inject_user_input` → codex が MCP 経由で `unreal.editor_status` を呼び出し → 正常応答)。
- 新規 `/tmp` プロジェクトでの Stage 2 zip e2e 成功(新設の SDK 適合ゲートステップを含む)。

### Verify

```text
UnrealMcp-v0.32.2-mac-ue56-ue57-projectroot.zip
SHA-256: 2f13f966dc9e69061c0d3b9e85cc1577c04223bce4e434bf287c94e1b7d3855b
UnrealMcp-v0.32.2-win-ue56-ue57-projectroot.zip
SHA-256: GitHub Release のアセットページを参照(Win CI 生成)
```

---

## Branch / Tag History

- `fa22576` fix(mcp): strip non-spec structuredContent from tools/list result (direct on main)
- `ecef387` test(mcp): pin protocol response shapes with key-whitelist guardrails (direct on main)
- `a87d63e` test(mcp): add official-SDK conformance smoke as release gate (direct on main)
- `v0.32.2` tag on the release commit (uplugin VersionName bump + AGENTS.md status refresh + these notes)
