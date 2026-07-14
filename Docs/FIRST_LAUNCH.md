# UEAtelier Source-Only First Launch

## English

### Prerequisites

- Unreal Engine 5.7 or 5.8 on macOS or Windows, matching your project's `EngineAssociation`. UE 5.6 is a maintenance compatibility target for v0.35, not a primary package target.
- Local C++ build tools: Xcode command line tools on macOS, or Visual Studio 2022 with the "Game Development with C++" workload, a Windows 10/11 SDK, and .NET 6 or newer on Windows.
- The v0.35.0 development-candidate projectroot zip is source-only; Unreal Build Tool compiles the plugin on first editor launch or during an explicit editor-target build.

The `ue57-ue58` filename describes the v0.35 target contract, not completed public-release evidence. Before UE 5.8 can be called publicly supported, the candidate must pass a real UE 5.8 UBT build plus strict package-integrity, fresh-project endpoint, and MCP SDK conformance checks. The latest public release remains v0.34.0 until those release gates finish.

### Step 1 - Extract

Extract `UnrealMcp-v0.35.0-<os>-ue57-ue58-projectroot.zip` into your Unreal project root directory, next to `<YourProject>.uproject` (`<os>` is `mac` or `win`).
After extraction you should see `Plugins/`, `Tools/`, and `Docs/` at the project root.

### Step 2 - Enable Plugins

Edit `<YourProject>.uproject` and add both plugins to the `Plugins` array:

```json
{ "Name": "PythonScriptPlugin", "Enabled": true },
{ "Name": "UnrealMcp", "Enabled": true }
```

### Step 3 - Open Unreal Editor

Double-click `<YourProject>.uproject`.
Accept the rebuild prompt if Unreal shows one; UBT compiles UnrealMcp against your local UE 5.7 or 5.8 install. You can also run an explicit `YourProjectEditor` build with Unreal's `Build.sh` or `Build.bat` before opening the editor. UE 5.6 users are on the maintenance path and should use the matching maintenance/public-release guidance rather than treating this candidate filename as a primary-support promise.

### Step 4 - Start The Codex Bridge

On macOS, run `Tools/UnrealMcpCodexBridge/start-bridge.sh`. On Windows, double-click or run `Tools\UnrealMcpCodexBridge\start-bridge.cmd`.
A console window should show `Bridge listening on ws://127.0.0.1:8766/uevolve`.
Leave that window open while using Chat.

### Step 5 - Open Chat Or Workbench

In Unreal Editor, open `Window > UEAtelier Chat` or `Window > UEAtelier Workbench`.
Configure your AI provider in `Project Settings > Plugins > Unreal MCP`.
Test the setup by asking Chat to `list maps` or by calling `unreal.editor_status`.

### Troubleshooting

- If the plugin does not load or compilation fails, confirm you are using primary-target UE 5.7 or 5.8, the `.uproject` `EngineAssociation` matches that install, and the local C++ toolchain is installed. Run an explicit editor-target build to capture the full UBT error. UE 5.6 failures belong to the maintenance compatibility track.
- If the bridge script flashes and closes, open a terminal in `Tools/UnrealMcpCodexBridge` and run `./start-bridge.sh` on macOS or `.\start-bridge.cmd` in PowerShell on Windows so the error stays visible.
- If Chat cannot see tools, confirm `Tools\UnrealMcpToolRegistry\tools.json` exists under the project root. If it is missing, the zip was probably extracted into the wrong directory.

## 中文

### 前提条件

- macOS 或 Windows 上的 Unreal Engine 5.7 或 5.8，并且与你项目的 `EngineAssociation` 匹配。UE 5.6 在 v0.35 中仅为维护兼容目标，不是主要打包目标。
- 本地 C++ 构建工具：macOS 需要 Xcode command line tools；Windows 需要 Visual Studio 2022，并安装 "Game Development with C++" 工作负载、Windows 10/11 SDK，以及 .NET 6 或更新版本。
- v0.35.0 开发候选 projectroot zip 是 source-only；Unreal Build Tool 会在首次启动编辑器或显式构建 editor target 时编译插件。

文件名中的 `ue57-ue58` 表示 v0.35 的目标契约，并不等于已经完成公开发布验证。只有候选包通过真实 UE 5.8 UBT 构建、严格包完整性检查、全新项目 endpoint smoke 以及 MCP SDK conformance 后，才能公开宣称支持 UE 5.8。在这些发布门完成之前，最新公开版本仍是 v0.34.0。

### 步骤 1 - 解压

把 `UnrealMcp-v0.35.0-<os>-ue57-ue58-projectroot.zip` 解压到你的 Unreal 项目根目录，也就是 `<YourProject>.uproject` 所在目录（`<os>` 为 `mac` 或 `win`）。
解压后，项目根目录下应该出现 `Plugins/`、`Tools/` 和 `Docs/`。

### 步骤 2 - 启用插件

编辑 `<YourProject>.uproject`，在 `Plugins` 数组中加入这两个插件：

```json
{ "Name": "PythonScriptPlugin", "Enabled": true },
{ "Name": "UnrealMcp", "Enabled": true }
```

### 步骤 3 - 打开 Unreal Editor

双击 `<YourProject>.uproject`。
如果 Unreal 提示重新构建，请允许它继续；UBT 会针对你的本地 UE 5.7 或 5.8 安装编译 UnrealMcp。你也可以在打开编辑器前，用 Unreal 的 `Build.sh` 或 `Build.bat` 显式构建 `YourProjectEditor`。UE 5.6 用户属于维护路径，应遵循对应维护版或公开版说明，不要把这个候选包文件名理解为主要支持承诺。

### 步骤 4 - 启动 Codex Bridge

macOS 上运行 `Tools/UnrealMcpCodexBridge/start-bridge.sh`。Windows 上双击或运行 `Tools\UnrealMcpCodexBridge\start-bridge.cmd`。
控制台窗口应显示 `Bridge listening on ws://127.0.0.1:8766/uevolve`。
使用 Chat 时请保持这个窗口打开。

### 步骤 5 - 打开 Chat 或 Workbench

在 Unreal Editor 中打开 `Window > UEAtelier Chat` 或 `Window > UEAtelier Workbench`。
在 `Project Settings > Plugins > Unreal MCP` 中配置 AI provider。
可以通过让 Chat 执行 `list maps`，或调用 `unreal.editor_status` 来测试配置。

### 故障排查

- 如果插件无法加载或编译失败，请确认使用的是主要目标 UE 5.7 或 5.8、`.uproject` 的 `EngineAssociation` 与该安装匹配，并且本地 C++ 工具链已安装。运行显式 editor-target build 可以看到完整 UBT 错误。UE 5.6 的问题属于维护兼容轨道。
- 如果 bridge 脚本窗口一闪而过，请在 `Tools\UnrealMcpCodexBridge` 中打开终端，macOS 运行 `./start-bridge.sh`，Windows PowerShell 运行 `.\start-bridge.cmd`，这样错误信息会保留在窗口里。
- 如果 Chat 看不到工具，请确认项目根目录下存在 `Tools\UnrealMcpToolRegistry\tools.json`。如果缺失，通常是 zip 解压到了错误目录。

## 日本語

### 前提条件

- macOS または Windows 上の Unreal Engine 5.7 または 5.8。プロジェクトの `EngineAssociation` と一致している必要があります。UE 5.6 は v0.35 の保守互換ターゲットであり、主要パッケージターゲットではありません。
- ローカル C++ ビルドツール: macOS では Xcode command line tools、Windows では Visual Studio 2022 の "Game Development with C++" ワークロード、Windows 10/11 SDK、.NET 6 以降が必要です。
- v0.35.0 開発候補の projectroot zip は source-only です。Unreal Build Tool が初回エディタ起動時、または editor target の明示的なビルド時にプラグインをコンパイルします。

ファイル名の `ue57-ue58` は v0.35 の目標契約を示すもので、公開リリース検証の完了を意味しません。UE 5.8 を公開サポートと呼ぶ前に、実際の UE 5.8 UBT ビルド、厳密なパッケージ整合性確認、新規プロジェクトでの endpoint smoke、MCP SDK conformance を通過する必要があります。これらのリリースゲートが完了するまで、最新の公開リリースは v0.34.0 です。

### Step 1 - 展開

`UnrealMcp-v0.35.0-<os>-ue57-ue58-projectroot.zip` を Unreal プロジェクトのルート、つまり `<YourProject>.uproject` と同じ場所へ展開します（`<os>` は `mac` または `win`）。
展開後、プロジェクトルートに `Plugins/`、`Tools/`、`Docs/` があることを確認してください。

### Step 2 - プラグインを有効化

`<YourProject>.uproject` を編集し、`Plugins` 配列に次の 2 つを追加します。

```json
{ "Name": "PythonScriptPlugin", "Enabled": true },
{ "Name": "UnrealMcp", "Enabled": true }
```

### Step 3 - Unreal Editor を開く

`<YourProject>.uproject` をダブルクリックします。
Unreal が再ビルドを求めた場合は許可してください。UBT がローカルの UE 5.7 または UE 5.8 インストールに合わせて UnrealMcp をコンパイルします。エディタを開く前に、Unreal の `Build.sh` または `Build.bat` で `YourProjectEditor` を明示的にビルドすることもできます。UE 5.6 は保守経路なので、この候補ファイル名を主要サポートの約束とみなさず、対応する保守版または公開版の案内に従ってください。

### Step 4 - Codex Bridge を起動

macOS では `Tools/UnrealMcpCodexBridge/start-bridge.sh` を実行します。Windows では `Tools\UnrealMcpCodexBridge\start-bridge.cmd` をダブルクリックまたは実行します。
コンソールに `Bridge listening on ws://127.0.0.1:8766/uevolve` と表示されます。
Chat を使う間はこのウィンドウを開いたままにしてください。

### Step 5 - Chat または Workbench を開く

Unreal Editor で `Window > UEAtelier Chat` または `Window > UEAtelier Workbench` を開きます。
`Project Settings > Plugins > Unreal MCP` で AI provider を設定します。
Chat に `list maps` と依頼するか、`unreal.editor_status` を呼び出して動作確認してください。

### トラブルシューティング

- プラグインが読み込まれない、またはコンパイルに失敗する場合は、主要ターゲットの UE 5.7 または UE 5.8 を使っていること、`.uproject` の `EngineAssociation` がそのインストールと一致すること、ローカル C++ toolchain が入っていることを確認してください。明示的な editor-target build を実行すると UBT の完全なエラーを確認できます。UE 5.6 の不具合は保守互換トラックとして扱います。
- bridge script のウィンドウがすぐ閉じる場合は、`Tools\UnrealMcpCodexBridge` でターミナルを開き、macOS では `./start-bridge.sh`、Windows PowerShell では `.\start-bridge.cmd` を実行してエラーを確認してください。
- Chat からツールが見えない場合は、プロジェクトルートに `Tools\UnrealMcpToolRegistry\tools.json` があるか確認してください。存在しない場合、zip を展開した場所が間違っている可能性があります。
