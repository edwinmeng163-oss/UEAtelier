#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpEngineCompat.h"

class FJsonObject;
class FJsonValue;

namespace UnrealMcp
{
	struct FTaskAtlasStepRef
	{
		FString ToolName;
		FString EventId;
		FString CaptureStatus;
		FString CaptureRef;
		FString CaptureSummary;
	};

	struct FTaskAtlasModel
	{
		FString TaskId;
		TArray<FTaskAtlasStepRef> StepRefs;
		TArray<FString> CriticalPath;
	};
}

namespace UnrealMcp::TaskAtlasComposite
{
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	struct FOfficialToolsetToolInfo
	{
		FString Name;
		TMap<FString, FString> ParamSchemaTypes;
		FString ReturnType;
		FString GovernedToolId;
		TSharedPtr<FJsonObject> ArgsTemplate;
	};

	struct FOfficialToolsetBuildProduct
	{
		FString ModuleName;
		FString ClassName;
		FString ToolsetName;
		FString MainPy;
		FString MainPySha256;
		FString ManifestJson;
		FString SchemaHash;
		TArray<FOfficialToolsetToolInfo> Tools;
	};

	bool BuildOfficialToolsetFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FOfficialToolsetBuildProduct& OutProduct,
		FString& OutFailureReason);
#endif
}

namespace UnrealMcp::TaskAtlasService
{
	enum class EEligibility : uint8
	{
		PreviewReady,
		Partial,
		SkeletonPreCapture,
		Blocked
	};

	enum class EMakeCompositeOutcome : uint8
	{
		CompositeWritten,
		DocumentOnly,
		Blocked,
		StagingFailed,
		ReloadRejected,
		Skipped
	};

	enum class EDeleteMadeToolOutcome : uint8
	{
		Deleted,
		DryRun,
		NotFound,
		Refused,
		ReloadRejected,
		Failed
	};

	enum class EPromoteToRagOutcome : uint8
	{
		Promoted,
		DryRun,
		NotFound,
		Refused,
		RefreshFailed,
		Failed
	};

	enum class ESmokeOutcome : uint8
	{
		Passed,
		Failed,
		DryRun,
		NotFound,
		Refused,
		ReloadRejected,
		FailedToExecute
	};

	struct FStepPolicy
	{
		int32 Ordinal = -1;                 // 0-based order from ordered stepRefs/criticalPath
		FString ToolName;                   // visible core unreal.* target from the task step
		FString PolicyDecision;             // "allow" | "force_dry_run" | "deny"
		FString DenyReason;                 // e.g. "dangerous_no_dryrun", "workflow_run_forbidden", "user_tool_forbidden", "tool_not_visible", empty unless deny
		bool bHasCapturedArgs = false;       // true only if private captured args exist and were redacted/sanitized into generated defaults
		FString CaptureEventId;              // public capture metadata eventId if known; never include raw private args here
		FString CaptureSummary;              // short safe summary: "captured", "missing", "redacted", "too_large", etc.
		bool bForceDryRun = false;           // convenience bool matching PolicyDecision == "force_dry_run"
	};

	struct FEligibilityResult
	{
		EEligibility Eligibility = EEligibility::SkeletonPreCapture;
		int32 BlockedFirstStep = -1;          // -1 if none; otherwise first denied ordinal
		FString BlockedFirstReason;           // reason from first denied step for UI tooltip/status
		int32 AllowCount = 0;
		int32 ForceDryRunCount = 0;
		int32 DenyCount = 0;
		int32 CapturedArgsCount = 0;
		int32 TotalStepCount = 0;
		TArray<FStepPolicy> Steps;            // ordered and non-deduped; mirrors Task Atlas schema v2 stepRefs
	};

	struct FMakeCompositeRequest
	{
		FString TaskId;
		bool bPreferDocumentOnly = false;     // UI/MCP dry-run equivalent; never writes PyTools when true
		bool bForceWriteEvenIfBlocked = false;// developer escape hatch only; still writes diagnostic and requires approval when exposed
		TMap<FString, TSharedPtr<FJsonObject>> OverrideStepArgs; // optional service-internal override keyed by ordinal or stepRef; MCP wrapper should keep strict schema by accepting JSON strings/closed array entries, not open root maps
	};

	struct FMakeCompositeResult
	{
		EMakeCompositeOutcome Outcome = EMakeCompositeOutcome::Skipped;
		FEligibilityResult Eligibility;
		FString GeneratedDir;                 // absolute dir under <Project>/Tools/UnrealMcpPyTools/<name>; empty unless CompositeWritten or ReloadRejected-before-rollback snapshot
		FString StagingDir;                   // absolute __staging_<guid> path while diagnosing failures; empty after success cleanup
		FString DocumentPath;                 // markdown path under Saved/UnrealMcp/TaskAtlasDrafts for document-only/blocked/partial/skeleton
		FString ToolName;                     // generated user.* name when derivable
		FString CompositeKind;                // "preview" | "skeleton" | "blocked" | "partial"
		FString ReplayStatus;                 // "preview_ready" | "partial" | "skeleton_pre_capture" | "blocked" | "generated_smoke_failed"
		TArray<FString> RejectedReasons;       // flattened from reload RejectedTools; check this, not only bIsError
		FString FailureDiagnosticPath;         // Saved/UnrealMcp/MakeToolSetFailures/<ts>-<name>/diagnostic.json
		FString ErrorCode;                    // stable machine code: task_not_found, invalid_name, staging_write_failed, reload_rejected, etc.
		FString ErrorMessage;                 // human-facing summary; empty on success
		TSharedPtr<FJsonObject> StructuredContent; // exact payload returned by MCP wrapper and UI may inspect
	};

	struct FMadeToolEntry
	{
		FString ToolName;                     // user.<snake_name>
		FString ScaffoldDir;                  // absolute dir under Tools/UnrealMcpPyTools
		FString CompositeKind;                // "preview" | "skeleton" | "unknown"
		FString ReplayStatus;                 // "preview_ready" | "generated_smoke_failed" | "stale" | ...
		FDateTime CreatedUtc;                 // parsed from metadata, else filesystem timestamp
		FString SourceTaskId;                 // Task Atlas taskId embedded in generator metadata
		FString Generator;                    // expected "task_atlas_make_composite"
		FString PythonHandlerSha256;          // from tool.json or computed handler sha if available
		bool bLoadedInUserRegistry = false;
		bool bHasFailureMarker = false;
		FString FailureDiagnosticPath;
	};

	struct FDeleteMadeToolResult
	{
		EDeleteMadeToolOutcome Outcome = EDeleteMadeToolOutcome::Refused;
		FString ToolName;
		FString RemovedDir;                   // absolute dir deleted, empty on dry-run/not-found/refused
		bool bWasStaleEntry = false;           // true when dir existed but registry entry was already absent, or metadata stale
		int32 ReloadRemovedCount = 0;          // removed count from reload result after deletion
		int32 ReloadBeforeCount = 0;
		int32 ReloadAfterCount = 0;
		TArray<FString> RejectedReasons;       // unrelated reload rejections must be surfaced
		FString FailureDiagnosticPath;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> StructuredContent;
	};

	struct FPromoteToRagResult
	{
		EPromoteToRagOutcome Outcome = EPromoteToRagOutcome::Failed;
		FString TaskId;
		bool bDryRun = false;
		FString SourcePath;                   // markdown source: draft path or generated README/manifest summary
		FString KnowledgeSourcePath;           // target under Saved/UnrealMcp/KnowledgeSources/TaskAtlas or configured source root
		int32 MarkdownLength = 0;
		FString RefreshResultText;             // text from knowledge_index_refresh if invoked
		TSharedPtr<FJsonObject> RefreshResult; // structured refresh result if available
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> StructuredContent;
	};

	struct FPromoteToRagRequest
	{
		FString TaskId;
		bool bDryRun = false;
	};

	struct FSmokeResult
	{
		ESmokeOutcome Outcome = ESmokeOutcome::Refused;
		FString ToolName;
		FString SmokeText;                    // concise human output from user smoke call or dry-run plan
		TSharedPtr<FJsonObject> StructuredContent;
		bool bVerdictMatchesEligibility = false; // true when smoke result is consistent with preview_ready/blocked state
		bool bDryRun = false;
		FString ReplayStatusBefore;
		FString ReplayStatusAfter;            // set to generated_smoke_failed on failed real smoke, unchanged on dry-run
		FString FailureDiagnosticPath;
		FString ErrorCode;
		FString ErrorMessage;
	};

	struct FSmokeRequest
	{
		FString ToolName;
		bool bDryRun = false;
	};

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	struct FOfficialToolsetDraftRequest
	{
		FString ToolId;
		FString Title;
		FString Description;
		FString TaskId;
		FString ReplayEligibility = TEXT("preview_ready");
		FString ReplayUnavailableReason;
		TArray<FString> CriticalPath;
		TArray<TSharedPtr<FJsonValue>> StepRefs;
		TSet<FString> VisibleCoreToolNames;
	};

	struct FOfficialToolsetDraftResult
	{
		bool bSucceeded = false;
		FString ErrorCode;
		FString ErrorMessage;
		FString GeneratedDir;
		FString StagingDir;
		FString ModulePath;
		FString ManifestPath;
		FString ModuleName;
		FString ClassName;
		FString ToolsetName;
		FString MainPySha256;
		FString ManifestJson;
		FString FailureDiagnosticPath;
		TArray<FString> ToolNames;
		TArray<FString> ValidatorIssues;
	};

	struct FOfficialToolsetRollbackResult
	{
		bool bSucceeded = false;
		FString ErrorCode;
		FString ErrorMessage;
		FString UpdatedManifestJson;
	};

	struct FOfficialCppToolsetBuildDraftResult
	{
		bool bSucceeded = false;
		FString BuildStatus;                  // succeeded | failed | buildBlockedByOpenEditor
		FString ErrorCode;
		FString ErrorMessage;
		FString Instructions;
		FString ToolId;
		FString PluginName;
		FString DraftDir;
		FString PluginSourceDir;
		FString MountedPluginDir;
		FString ProjectFilePath;
		FString BuildScriptPath;
		FString BuildLogPath;
		FString ManifestPath;
		FString ManifestJson;
		int32 ReturnCode = -1;
		double ElapsedSeconds = 0.0;
		bool bRestartRequired = true;
		bool bMountedCopyRemoved = false;
		bool bCleanupFailed = false;
		FString CleanupErrorMessage;
	};
#endif

	struct FUserToolView
	{
		FString ToolName;
		FString ScaffoldDir;
		FString Generator;                    // e.g. task_atlas_make_composite, scaffold_mcp_tool, imported_package, unknown
		FString PythonSha;                    // python handler sha256 if known
		FString LifecycleState;               // LoadedUserPythonHot, Rejected, MissingFiles, SmokeFailed, etc.
		FString SourceKind;                   // UserRegistry, GeneratedComposite, ImportedPackage, Unknown
		FString SourceTaskId;
		FString ToolJsonPath;
		FString PythonPath;
		bool bLoaded = false;
		bool bRejected = false;
		FString RejectionReason;
	};

	FEligibilityResult ClassifyTask(const FTaskAtlasModel& Task);
	FMakeCompositeResult MakeComposite(const FMakeCompositeRequest& Req);
	FDeleteMadeToolResult DeleteMadeTool(const FString& ToolName);
	TArray<FMadeToolEntry> ListMadeTools();
#if WITH_DEV_AUTOMATION_TESTS
	void SetMadeToolsRootDirForTests(const FString& RootDir);
	void ClearMadeToolsRootDirForTests();
	void SetSavedRootDirForTests(const FString& RootDir);
	void ClearSavedRootDirForTests();
#endif
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	FString OfficialToolsetDraftsRootDir();
	FOfficialToolsetDraftResult GenerateOfficialToolsetDraft(const FOfficialToolsetDraftRequest& Req);
	FOfficialToolsetRollbackResult RollbackOfficialToolsetDraft(const FString& ToolsetName, const FString& ModuleName, const FString& GeneratedDir);
	FOfficialCppToolsetBuildDraftResult BuildOfficialCppToolsetDraft(const FString& ToolId);
#if WITH_DEV_AUTOMATION_TESTS
	void SetOfficialCppBuildEditorRunningForTests(bool bIsRunning);
	void ClearOfficialCppBuildEditorRunningForTests();
	void SetOfficialCppBuildExampleRootForTests(const FString& ExampleRootDir);
	void ClearOfficialCppBuildExampleRootForTests();
	void FailNextOfficialCppBuildAfterCopyForTests();
#endif
#endif
	FPromoteToRagResult PromoteToRag(const FPromoteToRagRequest& Req);
	FPromoteToRagResult PromoteToRag(const FString& TaskId);
	FSmokeResult SmokeMadeTool(const FSmokeRequest& Req);
	FSmokeResult SmokeMadeTool(const FString& ToolName);
	TArray<FUserToolView> IntrospectUserRegistry();
	FString SanitizeToolIdPart(const FString& In);
	FString MakeAtlasToolId(const FString& TaskLabel, const FString& TaskId);
}
