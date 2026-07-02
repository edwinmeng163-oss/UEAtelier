# UEAtelier Release Notes - v0.33.0-preview (2026-07)

> **EXPERIMENTAL PREVIEW.** UE 5.8 official-MCP validation build from
> `experiment/v0.33-ue58-validation`. This branch never merges to main as-is;
> the supported public line is **v0.34.0** (UE 5.6/5.7). Use this preview only
> to evaluate UE 5.8 first-party MCP integration.

## English

### What this preview is

UEAtelier layered additively on top of Unreal Engine 5.8's first-party MCP
stack (ToolsetRegistry + ModelContextProtocol). Everything is compiled behind
a single gate (`UNREALMCP_HAS_OFFICIAL_TOOLSETS`): UE 5.6/5.7 builds are
byte-unaffected, verified by gate-exclusion builds with zero official-API
references. UEAtelier's own `:8765` server remains the supported surface.

### What's included

- **Opt-in official `:8000` server**: default OFF; start/stop from the
  UEAtelier Workbench card or the engine console commands
  `ModelContextProtocol.StartServer <port>` / `ModelContextProtocol.StopServer`.
  The official server is stateful streamable-HTTP (carry the `Mcp-Session-Id`
  from initialize) and exposes `list_toolsets` / `describe_toolset` /
  `call_tool` meta-tools.
- **Official Python toolset generation** from Task Atlas: `emitOfficial` on
  `task_atlas_make_composite` generates a delegating official Python
  `ToolsetDefinition` (hot-registered, no restart).
- **Official C++ toolset draft path**: `officialVariant:"cpp"` emits a
  complete native `UToolsetDefinition` plugin draft (descriptor, Build.cs,
  module, class with AICallable methods delegating through
  `UUnrealMcpCallToolLibrary::CallTool`), validated by an allowlist source
  validator, hash-bound manifest, post-publish drift detector, and an explicit
  project-mounted UBT build probe. Build + editor restart are required and
  honestly reported; nothing is auto-installed or auto-registered.
- **AgentSkill promotion (instruction-only)**: a made tool can be promoted to
  an engine AgentSkill asset documenting when/how to use it (and removed
  again); execution always stays with the callable tool under policy.
- Every generated path delegates side effects through UEAtelier's audited
  `call_tool` policy executor.

### Verified

- Official-area automation suites 13/13 green (generation, wiring, C++
  emitter/validator/drift, AgentSkill promotion, server toggle lifecycle).
- UE 5.6 + 5.7 + 5.8 builds green; 5.7 gate-exclusion build has zero
  official-API references. Registry validator 190/190.
- Live post-restart proof: a built native toolset registered with the engine
  ToolsetRegistry, was discovered/described/called over official `:8000`, and
  its execution delegated into the UEAtelier policy executor with an audit
  row, while `:8765` served in the same session.

### Known caveats

- Interactive-editor `automation_list/run` see zero tests (commandlets are
  authoritative).
- Stopping the official server removes the MCP route; the TCP port stays
  bound by the engine's shared HTTP listener.
- MCPClientToolset -> `:8765` (Spike 3) has not been exercised.

### Assets

```text
UnrealMcp-v0.33.0-preview-mac-ue56-ue57-projectroot.zip
SHA-256: 47734b988e4cd0c4b02769e0b7345a16d0e80c3a90af27fec1c0b2ed526f2858
UnrealMcp-v0.33.0-preview-win-ue56-ue57-projectroot.zip
SHA-256: <filled by Win CI / release process>
```

## 中文

### 本预览版是什么

UEAtelier 以增量方式叠加在 Unreal Engine 5.8 官方 MCP 栈（ToolsetRegistry +
ModelContextProtocol）之上。所有代码都在单一编译门
`UNREALMCP_HAS_OFFICIAL_TOOLSETS` 之后：UE 5.6/5.7 构建字节级不受影响（由
gate-exclusion 构建验证，日志中官方 API 引用为零）。UEAtelier 自有的 `:8765`
服务器仍是受支持的接入面。**实验性预览：本分支不会按原样合入 main；受支持的
公开版本是 v0.34.0（UE 5.6/5.7）。**

### 包含内容

- **可选启用的官方 `:8000` 服务器**：默认关闭；通过 Workbench 卡片或引擎控制台
  命令 `ModelContextProtocol.StartServer <port>` / `.StopServer` 启停。官方服务器
  为有状态 streamable-HTTP（initialize 后续请求需携带 `Mcp-Session-Id`），暴露
  `list_toolsets` / `describe_toolset` / `call_tool` 元工具。
- **Task Atlas 生成官方 Python toolset**：`task_atlas_make_composite` 的
  `emitOfficial` 生成委托执行的官方 Python `ToolsetDefinition`（热注册，无需重启）。
- **官方 C++ toolset 草稿路径**：`officialVariant:"cpp"` 生成完整的原生
  `UToolsetDefinition` 插件草稿（descriptor、Build.cs、模块、带 AICallable 方法的
  类，全部通过 `UUnrealMcpCallToolLibrary::CallTool` 委托执行），配套 allowlist
  源码校验器、哈希绑定 manifest、发布后漂移检测器，以及显式的项目挂载 UBT 构建
  探针。需要构建 + 编辑器重启，并如实上报状态；不会自动安装或自动注册。
- **AgentSkill 提升（仅说明性）**：可将生成工具提升为引擎 AgentSkill 资产
  （记录何时/如何使用，可撤销）；执行始终归属受策略管控的可调用工具。
- 所有生成路径的副作用都通过 UEAtelier 审计的 `call_tool` 策略执行器。

### 已验证

官方区自动化 13/13 全绿；UE 5.6+5.7+5.8 构建通过；5.7 gate-exclusion 构建官方
API 引用为零；registry 校验 190/190；重启后实机验证：原生 toolset 注册、经官方
`:8000` 发现/描述/调用、执行委托进入 UEAtelier 策略执行器并产生审计记录，
`:8765` 同会话共存。

### 已知注意事项

交互式编辑器内 `automation_list/run` 看不到测试（以 commandlet 为准）；停止官方
服务器会移除 MCP 路由，但 TCP 端口仍被引擎共享 HTTP 监听器占用；Spike 3
（MCPClientToolset -> `:8765`）未执行。

## 日本語

### このプレビューについて

UEAtelier を Unreal Engine 5.8 のファーストパーティ MCP スタック
（ToolsetRegistry + ModelContextProtocol）の上に追加レイヤーとして統合した
検証ビルドです。すべて単一のコンパイルゲート
`UNREALMCP_HAS_OFFICIAL_TOOLSETS` の背後にあり、UE 5.6/5.7 ビルドはバイト単位で
不変（gate-exclusion ビルドで公式 API 参照ゼロを確認済み）。UEAtelier 自身の
`:8765` サーバーが引き続きサポート対象の窓口です。**実験的プレビュー：この
ブランチはそのまま main にマージされません。サポート対象の公開版は v0.34.0
（UE 5.6/5.7）です。**

### 含まれる内容

- **オプトインの公式 `:8000` サーバー**：デフォルト OFF。Workbench カードまたは
  コンソールコマンド `ModelContextProtocol.StartServer <port>` / `.StopServer`
  で起動/停止。公式サーバーはステートフルな streamable-HTTP（initialize 後は
  `Mcp-Session-Id` を必ず付与）で、`list_toolsets` / `describe_toolset` /
  `call_tool` のメタツールを公開します。
- **Task Atlas からの公式 Python toolset 生成**：`task_atlas_make_composite` の
  `emitOfficial` が委譲実行型の公式 Python `ToolsetDefinition` を生成
  （ホット登録、再起動不要）。
- **公式 C++ toolset ドラフトパス**：`officialVariant:"cpp"` がネイティブ
  `UToolsetDefinition` プラグインドラフト一式を生成（descriptor、Build.cs、
  モジュール、`UUnrealMcpCallToolLibrary::CallTool` へ委譲する AICallable
  メソッド群）。allowlist ソースバリデーター、ハッシュ結合 manifest、公開後の
  ドリフト検出器、明示的なプロジェクトマウント UBT ビルドプローブ付き。
  ビルド + エディタ再起動が必要で、状態は正直に報告されます。自動インストール
  や自動登録は行いません。
- **AgentSkill 昇格（説明のみ）**：生成ツールをエンジンの AgentSkill アセットに
  昇格（使いどころを文書化、取り消し可能）。実行は常にポリシー管理下の
  呼び出し可能ツール側にあります。
- 生成されたすべての経路の副作用は、監査付き `call_tool` ポリシー実行器を
  経由します。

### 検証済み

公式領域の自動化スイート 13/13 グリーン。UE 5.6+5.7+5.8 ビルド成功。5.7
gate-exclusion ビルドで公式 API 参照ゼロ。registry 検証 190/190。再起動後の
実機証明：ネイティブ toolset が ToolsetRegistry に登録され、公式 `:8000` 経由で
発見/記述/呼び出しされ、実行が UEAtelier ポリシー実行器へ委譲され監査行を
生成、同一セッションで `:8765` と共存。

### 既知の注意点

対話型エディタ内の `automation_list/run` はテストを検出しません（commandlet が
正）。公式サーバー停止で MCP ルートは除去されますが、TCP ポートはエンジン共有
HTTP リスナーが保持します。Spike 3（MCPClientToolset -> `:8765`）は未実施。
