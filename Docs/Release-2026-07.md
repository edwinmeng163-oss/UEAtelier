# UEAtelier Release Notes - v0.34.0 (2026-07)

> Trilingual: [English](#english) · [中文](#中文) · [日本語](#日本語)
> Public release: Make-Tool-Set vetted real-write foundation + Codex bridge MCP reachability fix
> Previous public release: [`Docs/Release-2026-06c.md`](Release-2026-06c.md) (v0.32.2). Version 0.33 was an internal validation track.

---

## English

### Summary

v0.34.0 ships the Make-Tool-Set vetting foundation and the bridge network fix on top of v0.32.2. A Task Atlas generated composite (`user.atlas_*`) can now be approved in-editor for vetted real writes, while the Codex bridge can reach the local Unreal MCP endpoint from its workspace-write sandbox.

Version 0.33 was an internal UE5.8 validation track and is not used by the public release line.

### What's New

1. **Vetted-toolset standing authority**: a made tool can be approved from the Task Atlas made-tools row with **Approve real writes**. Once approved, the composite can execute dangerous `call_tool` steps for real without per-run confirmation.
2. **Approval dialog detail**: the dialog lists each dangerous step with its policy decision and reason, shows the exact `main.py` SHA-256 being trusted, requires a typed AI-review summary plus an explicit checkbox, and warns that the approval test run executes real writes.
3. **Vetted status reporting**: the made-tools list and `unreal.user_registry_introspect` now report vetted marker fields such as `vetted`, `markerSha`, `liveShaMatches`, approver, approval time, revocation fields, and live hash state.
4. **Bridge MCP reachability**: the Codex bridge now sets workspace-write `network_access=true`, so in-editor AI can call `http://127.0.0.1:8765/mcp` through the bridge.

### Safety Model

Vetting is fail-closed. Approval runs a source-policy validator with closed imports, no dynamic access or reflection, no direct Unreal API usage, and no file IO. It then checks that the generated manifest is a subset of the allowed call list, binds authority to the live `main.py` SHA-256, runs a real vetted-context test, re-checks the hash for TOCTOU drift, writes the marker atomically, reloads the user registry, and emits a `toolset_vetted` audit event. Authority never persists unaudited.

Every vetted real write emits a sanitized `vetted_real_write` audit event. Revocation is available from the same made-tools row with **Revoke** and is fail-safe. Any code edit that changes the live `main.py` hash re-applies the dry-run wall.

The four structural hard denies are not overridable: hidden tools, user-to-user calls, call-tool depth, and `workflow_run`. Wire/MCP clients cannot grant vetting; only an in-editor human flow can persist a vetted marker.

### Upgrade Notes

This is a drop-in upgrade from v0.32.2 for UE 5.6 and UE 5.7. There are no new MCP tools, no tool-count change, and no schema migration required. Existing tools and existing unvetted Task Atlas made tools remain unchanged.

Vetting is opt-in per made tool. Until an in-editor user approves a specific generated composite, dangerous `call_tool` steps keep the existing force-dry-run or deny behavior.

### Verification

- UE 5.6 + UE 5.7 dual-engine builds pass.
- Automation suites pass: VetMadeTool 11/11, VettedToolset 5/5, CallTool 9/9, TaskAtlas 38/38.
- `python3 Tools/validate_tool_registry.py` stays clean at 190/190; visible `tools/list` count stays 178.
- Full-host automation converges to the two known baseline failures.

### Asset Naming

```text
UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip
SHA-256: 0433eece26423c191d21b5bf6ff8df69ca80abb7b74a2f8466a15915295fc70d
SHA-256 sidecar: UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip.sha256

UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip
SHA-256: 4c666d718c838719f547e1c6e40f0795a8835f864ea04d7e80312b8b8c61a853
SHA-256 sidecar: UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip.sha256
```

---

## 中文

### 更新摘要

v0.34.0 在 v0.32.2 基础上发布 Make-Tool-Set vetting foundation 和 bridge network fix。Task Atlas 生成的 composite（`user.atlas_*`）现在可以在编辑器内被批准为 vetted real writes；同时 Codex bridge 的 workspace-write sandbox 现在可以访问本地 Unreal MCP endpoint。

版本 0.33 是内部 UE5.8 validation track，不用于公开 release 线。

### 新增内容

1. **Vetted-toolset standing authority**：made tool 可以在 Task Atlas made-tools 行通过 **Approve real writes** 批准。批准后，该 composite 可以真实执行危险 `call_tool` 步骤，不再要求每次运行确认。
2. **批准对话框细节**：对话框列出每个危险步骤的 policy decision 与 reason，显示被信任的精确 `main.py` SHA-256，要求填写 AI-review summary 并勾选明确确认，同时警告批准测试运行会执行真实写入。
3. **Vetted 状态回报**：made-tools list 与 `unreal.user_registry_introspect` 现在回报 `vetted`、`markerSha`、`liveShaMatches`、approver、approval time、revocation fields 和 live hash state 等 vetted marker 字段。
4. **Bridge MCP 可达性**：Codex bridge 现在设置 workspace-write `network_access=true`，因此编辑器内 AI 可以通过 bridge 调用 `http://127.0.0.1:8765/mcp`。

### 安全模型

Vetting 默认 fail-closed。批准流程会运行 source-policy validator，要求 closed imports、禁止 dynamic access/reflection、禁止直接使用 Unreal API、禁止 file IO；随后检查生成 manifest 是允许调用列表的子集，把权限绑定到 live `main.py` SHA-256，执行真实 vetted-context 测试，再次检查 hash 防止 TOCTOU drift，原子写入 marker，reload user registry，并写出 `toolset_vetted` audit event。未经审计的 authority 不会被持久化。

每一次 vetted real write 都会写出 sanitized `vetted_real_write` audit event。可以在同一 made-tools 行通过 **Revoke** 撤销，且撤销 fail-safe。任何导致 live `main.py` hash 改变的代码编辑都会重新套上 dry-run wall。

四个 structural hard denies 不可覆盖：hidden tools、user-to-user calls、call-tool depth、`workflow_run`。Wire/MCP clients 不能授予 vetting；只有编辑器内 human flow 可以持久化 vetted marker。

### 升级说明

这是从 v0.32.2 到 UE 5.6 / UE 5.7 的 drop-in upgrade。本版没有新增 MCP tools，没有工具数量变化，也不需要 schema migration。既有工具与既有未 vetted 的 Task Atlas made tools 行为不变。

Vetting 对每个 made tool 单独 opt-in。在编辑器内用户批准某个生成 composite 之前，危险 `call_tool` 步骤仍保持原有 force-dry-run 或 deny 行为。

### 验证

- UE 5.6 + UE 5.7 双引擎构建通过。
- Automation suites 通过：VetMadeTool 11/11、VettedToolset 5/5、CallTool 9/9、TaskAtlas 38/38。
- `python3 Tools/validate_tool_registry.py` 保持 190/190 干净；可见 `tools/list` 数量保持 178。
- full-host automation 收敛到两个已知 baseline failures。

### 资产命名

```text
UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip
SHA-256: 0433eece26423c191d21b5bf6ff8df69ca80abb7b74a2f8466a15915295fc70d
SHA-256 sidecar: UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip.sha256

UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip
SHA-256: 4c666d718c838719f547e1c6e40f0795a8835f864ea04d7e80312b8b8c61a853
SHA-256 sidecar: UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip.sha256
```

---

## 日本語

### 更新概要

v0.34.0 は v0.32.2 の上に Make-Tool-Set vetting foundation と bridge network fix を載せたリリースです。Task Atlas が生成した composite（`user.atlas_*`）をエディタ内で vetted real writes として承認できるようになり、Codex bridge の workspace-write sandbox から local Unreal MCP endpoint へ到達できるようになりました。

バージョン 0.33 は内部 UE5.8 validation track であり、公開 release line では使用しません。

### 新機能

1. **Vetted-toolset standing authority**: made tool を Task Atlas の made-tools 行から **Approve real writes** で承認できます。承認後、その composite は危険な `call_tool` ステップを毎回の確認なしに実行できます。
2. **承認ダイアログの詳細**: ダイアログは各危険ステップの policy decision と reason、信頼対象の正確な `main.py` SHA-256 を表示し、AI-review summary の入力と明示チェックを要求します。また、承認テスト実行が real writes を行うことを警告します。
3. **Vetted 状態の表示**: made-tools list と `unreal.user_registry_introspect` は、`vetted`、`markerSha`、`liveShaMatches`、approver、approval time、revocation fields、live hash state などの vetted marker fields を返します。
4. **Bridge MCP 到達性**: Codex bridge は workspace-write `network_access=true` を設定するため、エディタ内 AI は bridge 経由で `http://127.0.0.1:8765/mcp` を呼び出せます。

### 安全モデル

Vetting は fail-closed です。承認フローは source-policy validator を実行し、closed imports、dynamic access/reflection の禁止、直接の Unreal API 使用禁止、file IO 禁止を確認します。その後、生成 manifest が許可された call list の subset であることを検証し、権限を live `main.py` SHA-256 に束縛し、real vetted-context test を実行し、TOCTOU drift 防止のため hash を再確認し、marker を atomic に書き込み、user registry を reload し、`toolset_vetted` audit event を出力します。監査されていない authority は永続化されません。

すべての vetted real write は sanitized `vetted_real_write` audit event を出力します。同じ made-tools 行の **Revoke** で撤回でき、撤回は fail-safe です。live `main.py` hash が変わるコード編集があれば dry-run wall が再適用されます。

4 つの structural hard denies は上書きできません: hidden tools、user-to-user calls、call-tool depth、`workflow_run`。Wire/MCP clients は vetting を付与できません。vetted marker を永続化できるのは、エディタ内の human flow のみです。

### アップグレードノート

v0.32.2 から UE 5.6 / UE 5.7 への drop-in upgrade です。新しい MCP tools はなく、tool count の変更も schema migration もありません。既存ツールと、まだ vetted されていない Task Atlas made tools の挙動は変わりません。

Vetting は made tool ごとの opt-in です。エディタ内ユーザーが特定の generated composite を承認するまでは、危険な `call_tool` ステップは従来どおり force-dry-run または deny のままです。

### 検証

- UE 5.6 + UE 5.7 dual-engine builds pass。
- Automation suites pass: VetMadeTool 11/11、VettedToolset 5/5、CallTool 9/9、TaskAtlas 38/38。
- `python3 Tools/validate_tool_registry.py` は 190/190 で clean。visible `tools/list` count は 178 のままです。
- full-host automation は 2 つの known baseline failures に収束します。

### アセット命名

```text
UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip
SHA-256: 0433eece26423c191d21b5bf6ff8df69ca80abb7b74a2f8466a15915295fc70d
SHA-256 sidecar: UnrealMcp-v0.34.0-mac-ue56-ue57-projectroot.zip.sha256

UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip
SHA-256: 4c666d718c838719f547e1c6e40f0795a8835f864ea04d7e80312b8b8c61a853
SHA-256 sidecar: UnrealMcp-v0.34.0-win-ue56-ue57-projectroot.zip.sha256
```
