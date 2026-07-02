#include "UnrealMcpTaskAtlasService.h"

#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpOfficialCppToolsetEmitter.h"
#include "UnrealMcpSelfExtensionTools.h"
#include "UnrealMcpSharedPathResolver.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpTaskAtlasTools.h"
#include "UnrealMcpUserToolListVersion.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
#include "IPythonScriptPlugin.h"
#endif

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <limits.h>
#include <stdlib.h>
#endif

namespace UnrealMcp::TaskAtlasComposite
{
	bool BuildCompositeUserToolFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FString& OutToolName,
		FString& OutMainPy,
		FString& OutMainPySha256,
		FString& OutToolJson,
		TSharedPtr<FJsonObject>& OutSmokeArgs,
		TArray<FString>& OutStepTools,
		FString& OutFailureReason);
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
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

namespace UnrealMcp::UnrealMcpUserToolSmokeTool
{
	FUnrealMcpExecutionResult Execute(const FJsonObject& Arguments);
}

namespace UnrealMcp::TaskAtlasService
{
	// Service functions return result structs. They do not throw or use checkf for user/data errors.
	// Report errors through Outcome, ErrorCode, ErrorMessage, RejectedReasons, FailureDiagnosticPath, and structured JSON fields.

	namespace
	{
		static FCriticalSection GTaskAtlasServiceMutationLock;

		struct FLoadedTask
		{
			FTaskAtlasModel Model;
			FString Label;
			FString Description;
			FString Rating;
			bool bPinned = false;
			FString SessionId;
			FString TStartUtc;
			FString TEndUtc;
			FString UserIntent;
			FString ReplayEligibility;
			FString ReplayUnavailableReason;
			FString TaskPath;
			TSharedPtr<FJsonObject> TaskJson;
		};

		struct FResolvedMadeTool
		{
			FString ToolName;
			FString ToolId;
			FString RootDir;
			FString TargetDir;
			FString ToolJsonPath;
			TSharedPtr<FJsonObject> ToolJson;
		};

#if WITH_DEV_AUTOMATION_TESTS
		FString GTaskAtlasServiceMadeToolsRootForTests;
		FString GTaskAtlasServiceSavedRootForTests;
		bool bTaskAtlasServiceCorruptNextStagingShaForTests = false;
		bool bTaskAtlasServiceRejectNextTargetReloadForTests = false;
		bool bTaskAtlasServiceFailNextKnowledgeRefreshForTests = false;
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
		TOptional<bool> GTaskAtlasServiceOfficialCppEditorRunningForTests;
		FString GTaskAtlasServiceOfficialCppExampleRootForTests;
		bool bTaskAtlasServiceFailNextOfficialCppBuildAfterCopyForTests = false;
#endif
#endif

		FString TaskAtlasServiceNormalizeAbsolutePath(const FString& Path)
		{
			FString Result = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Result);
			FPaths::CollapseRelativeDirectories(Result);
			Result.RemoveFromEnd(TEXT("/"));
			return Result;
		}

		bool TaskAtlasServiceTryResolveRealPath(const FString& Path, FString& OutPath)
		{
			OutPath.Reset();
#if PLATFORM_MAC || PLATFORM_LINUX
			char Buffer[PATH_MAX + 1] = {};
			FTCHARToUTF8 Converter(*Path);
			if (realpath(Converter.Get(), Buffer) == nullptr)
			{
				return false;
			}
			FUTF8ToTCHAR TargetConverter(Buffer);
			OutPath = TaskAtlasServiceNormalizeAbsolutePath(FString(TargetConverter.Length(), TargetConverter.Get()));
			return true;
#else
			(void)Path;
			return false;
#endif
		}

		FString TaskAtlasServiceCanonicalExistingPath(const FString& Path)
		{
			FString Resolved;
			if (TaskAtlasServiceTryResolveRealPath(Path, Resolved))
			{
				return Resolved;
			}
			return TaskAtlasServiceNormalizeAbsolutePath(Path);
		}

		bool TaskAtlasServicePathEqualsOrChild(const FString& ChildPath, const FString& RootPath)
		{
			FString Child = TaskAtlasServiceNormalizeAbsolutePath(ChildPath);
			FString Root = TaskAtlasServiceNormalizeAbsolutePath(RootPath);
#if PLATFORM_WINDOWS || PLATFORM_MAC
			Child = Child.ToLower();
			Root = Root.ToLower();
#endif
			return Child == Root || Child.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive);
		}

		bool TaskAtlasServiceIsSafeDirectoryName(const FString& DirectoryName)
		{
			return !DirectoryName.IsEmpty()
				&& !DirectoryName.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				&& FPaths::IsRelative(DirectoryName);
		}

		bool TaskAtlasServiceIsSymlink(const FString& Path)
		{
			return FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Path) == ESymlinkResult::Symlink;
		}

		const TCHAR* TaskAtlasServiceDecisionToString(ECallToolDecision Decision)
		{
			switch (Decision)
			{
			case ECallToolDecision::Allow:
				return TEXT("allow");
			case ECallToolDecision::ForceDryRun:
				return TEXT("force_dry_run");
			case ECallToolDecision::Deny:
			default:
				return TEXT("deny");
			}
		}

		FCallToolTargetFacts TaskAtlasServiceMakePolicyFacts(const FString& RawToolName)
		{
			const FString ToolName = RawToolName.TrimStartAndEnd();
			FCallToolTargetFacts Facts;

			if (ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive))
			{
				Facts.bVisible = true;
				Facts.SourceKind = Extension::ESourceKind::UserRegistry;
				Facts.Depth = 0;
				return Facts;
			}

			if (const FToolRegistryEntry* Entry = FindToolRegistryEntry(ToolName))
			{
				Facts.bVisible = Entry->Exposure == EToolExposure::Visible;
			}
			Facts.SourceKind = ResolveToolSourceKind(ToolName);
			const FToolPolicy Policy = GetToolPolicy(ToolName);
			Facts.RiskLevel = Policy.RiskLevel;
			Facts.bRequiresLock = Policy.bRequiresLock;
			Facts.bRequiresWrite = Policy.bRequiresWrite;
			Facts.bRequiresRestart = Policy.bRequiresRestart;
			Facts.bRequiresExternalProcess = Policy.bRequiresExternalProcess;
			Facts.bRequiresBuild = Policy.bRequiresBuild;
			Facts.bDryRunSupport = Policy.bDryRunSupport;
			Facts.bIsWorkflowRun = ToolName == TEXT("unreal.workflow_run");
			Facts.Depth = 0;
			return Facts;
		}

		FString TaskAtlasServiceCaptureSummary(const FString& CaptureStatus, const FString& FallbackSummary)
		{
			const FString Summary = FallbackSummary.TrimStartAndEnd().ToLower();
			if (Summary == TEXT("captured")
				|| Summary == TEXT("missing")
				|| Summary == TEXT("redacted")
				|| Summary == TEXT("too_large")
				|| Summary == TEXT("blocked_by_policy"))
			{
				return Summary;
			}

			const FString Status = CaptureStatus.TrimStartAndEnd().ToLower();
			if (Status == TEXT("captured")
				|| Status == TEXT("redacted")
				|| Status == TEXT("too_large")
				|| Status == TEXT("blocked_by_policy"))
			{
				return Status;
			}
			return TEXT("missing");
		}

		bool TaskAtlasServiceHasCapturedArgs(const FString& CaptureStatus)
		{
			const FString Status = CaptureStatus.TrimStartAndEnd().ToLower();
			return Status == TEXT("captured") || Status == TEXT("redacted");
		}

		bool TaskAtlasServiceLoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *Path))
			{
				return false;
			}
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool TaskAtlasServiceReadRequiredString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FString& OutValue)
		{
			OutValue.Reset();
			if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue))
			{
				return false;
			}
			OutValue = OutValue.TrimStartAndEnd();
			return !OutValue.IsEmpty();
		}

		FDateTime TaskAtlasServiceReadCreatedUtc(const TSharedPtr<FJsonObject>& Object, const FString& ToolJsonPath)
		{
			FString CreatedUtcText;
			FDateTime CreatedUtc;
			if (Object.IsValid()
				&& Object->TryGetStringField(TEXT("createdUtc"), CreatedUtcText)
				&& FDateTime::ParseIso8601(*CreatedUtcText.TrimStartAndEnd(), CreatedUtc))
			{
				return CreatedUtc;
			}

			const FFileStatData Stat = IFileManager::Get().GetStatData(*ToolJsonPath);
			return Stat.bIsValid ? Stat.ModificationTime : FDateTime();
		}

		bool TaskAtlasServiceHasFailureMarker(const FString& Directory)
		{
			static const TCHAR* MarkerNames[] = {
				TEXT("_failure.marker"),
				TEXT("failure.marker"),
				TEXT("_failure.json"),
				TEXT("failure.json")
			};
			for (const TCHAR* MarkerName : MarkerNames)
			{
				if (FPaths::FileExists(FPaths::Combine(Directory, MarkerName)))
				{
					return true;
				}
			}
			return false;
		}

		FString TaskAtlasServiceReadFailureDiagnosticPath(const TSharedPtr<FJsonObject>& Object)
		{
			FString Path;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("failureDiagnosticPath"), Path))
			{
				return FString();
			}
			Path = Path.TrimStartAndEnd();
			if (Path.IsEmpty())
			{
				return FString();
			}
			Path = FPaths::IsRelative(Path)
				? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Path))
				: FPaths::ConvertRelativePathToFull(Path);
			Path = TaskAtlasServiceNormalizeAbsolutePath(Path);
			return FPaths::FileExists(Path) ? Path : FString();
		}

		FString TaskAtlasServiceMadeToolsRootDir()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!GTaskAtlasServiceMadeToolsRootForTests.IsEmpty())
			{
				return TaskAtlasServiceNormalizeAbsolutePath(GTaskAtlasServiceMadeToolsRootForTests);
			}
#endif
			UserRegistry::InitializeUserToolRegistry();
			return TaskAtlasServiceNormalizeAbsolutePath(UserRegistry::GetUserToolsRootDir());
		}

		FString TaskAtlasServiceSavedRootDir()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!GTaskAtlasServiceSavedRootForTests.IsEmpty())
			{
				return TaskAtlasServiceNormalizeAbsolutePath(GTaskAtlasServiceSavedRootForTests);
			}
#endif
			return TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp")));
		}

		FString TaskAtlasServiceTaskRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("Tasks"));
		}

		FString TaskAtlasServiceDraftRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("TaskAtlasDrafts"));
		}

		FString TaskAtlasServiceFailureRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("MakeToolSetFailures"));
		}

		bool TaskAtlasServiceIsSafeTaskId(const FString& TaskId)
		{
			const FString Trimmed = TaskId.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.Len() > 180)
			{
				return false;
			}
			for (const TCHAR Character : Trimmed)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('-') && Character != TEXT('_') && Character != TEXT('.'))
				{
					return false;
				}
			}
			return true;
		}

		FString TaskAtlasServiceSanitizeTaskToken(const FString& Value)
		{
			FString Result;
			for (const TCHAR Character : Value.TrimStartAndEnd().ToLower())
			{
				if (FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_') || Character == TEXT('.'))
				{
					Result.AppendChar(Character);
				}
				else if (FChar::IsWhitespace(Character))
				{
					Result.AppendChar(TEXT('-'));
				}
			}
			while (Result.Contains(TEXT("--")))
			{
				Result.ReplaceInline(TEXT("--"), TEXT("-"));
			}
			Result.RemoveFromStart(TEXT("-"));
			Result.RemoveFromEnd(TEXT("-"));
			return Result.IsEmpty() ? TEXT("task") : Result.Left(120);
		}

		FString TaskAtlasServiceTrimUnderscores(FString Value)
		{
			while (Value.StartsWith(TEXT("_")))
			{
				Value.RightChopInline(1);
			}
			while (Value.EndsWith(TEXT("_")))
			{
				Value.LeftChopInline(1);
			}
			return Value;
		}

		void TaskAtlasServiceAppendNormalizedUnderscore(FString& OutValue)
		{
			if (!OutValue.IsEmpty() && !OutValue.EndsWith(TEXT("_")))
			{
				OutValue.AppendChar(TEXT('_'));
			}
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasServiceMakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			JsonValues.Reserve(Values.Num());
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		FString TaskAtlasServiceJsonToPrettyString(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return TEXT("{}");
			}

			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Output;
		}

		bool TaskAtlasServiceWriteJsonObject(const TSharedPtr<FJsonObject>& Object, const FString& Path)
		{
			if (!Object.IsValid())
			{
				return false;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			return FFileHelper::SaveStringToFile(
				TaskAtlasServiceJsonToPrettyString(Object),
				*Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString TaskAtlasServiceJsonToCondensedString(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return TEXT("{}");
			}

			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Output;
		}

		FString TaskAtlasServiceMakeDisplayPath(const FString& Path)
		{
			if (Path.TrimStartAndEnd().IsEmpty())
			{
				return FString();
			}

			FString FullPath = FPaths::IsRelative(Path)
				? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Path))
				: FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(FullPath);

			FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FPaths::NormalizeDirectoryName(ProjectDir);
			if (!ProjectDir.EndsWith(TEXT("/")))
			{
				ProjectDir += TEXT("/");
			}
			FString Relative = FullPath;
			if (FPaths::MakePathRelativeTo(Relative, *ProjectDir))
			{
				return Relative;
			}

			FString HomeDir = FPlatformProcess::UserDir();
			FPaths::NormalizeDirectoryName(HomeDir);
			if (!HomeDir.EndsWith(TEXT("/")))
			{
				HomeDir += TEXT("/");
			}
			if (!HomeDir.IsEmpty() && FullPath.StartsWith(HomeDir, ESearchCase::IgnoreCase))
			{
				return FString(TEXT("<home>/")) + FullPath.RightChop(HomeDir.Len());
			}
			return FullPath;
		}

		FString TaskAtlasServiceMakeSafeTimestamp(const FString& TimestampUtc)
		{
			FString SafeTimestamp = TimestampUtc;
			SafeTimestamp.ReplaceInline(TEXT(":"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("-"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("."), TEXT(""));
			return SafeTimestamp;
		}

		FString TaskAtlasServiceEligibilityToString(EEligibility Eligibility)
		{
			switch (Eligibility)
			{
			case EEligibility::PreviewReady:
				return TEXT("preview_ready");
			case EEligibility::Partial:
				return TEXT("partial");
			case EEligibility::Blocked:
				return TEXT("blocked");
			case EEligibility::SkeletonPreCapture:
			default:
				return TEXT("skeleton_pre_capture");
			}
		}

		FString TaskAtlasServiceOutcomeToString(EMakeCompositeOutcome Outcome)
		{
			switch (Outcome)
			{
			case EMakeCompositeOutcome::CompositeWritten:
				return TEXT("CompositeWritten");
			case EMakeCompositeOutcome::DocumentOnly:
				return TEXT("DocumentOnly");
			case EMakeCompositeOutcome::Blocked:
				return TEXT("Blocked");
			case EMakeCompositeOutcome::StagingFailed:
				return TEXT("StagingFailed");
			case EMakeCompositeOutcome::ReloadRejected:
				return TEXT("ReloadRejected");
			case EMakeCompositeOutcome::Skipped:
			default:
				return TEXT("Skipped");
			}
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceEligibilityToJson(const FEligibilityResult& Eligibility)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("eligibility"), TaskAtlasServiceEligibilityToString(Eligibility.Eligibility));
			Object->SetNumberField(TEXT("blockedFirstStep"), Eligibility.BlockedFirstStep);
			Object->SetStringField(TEXT("blockedFirstReason"), Eligibility.BlockedFirstReason);
			Object->SetNumberField(TEXT("allowCount"), Eligibility.AllowCount);
			Object->SetNumberField(TEXT("forceDryRunCount"), Eligibility.ForceDryRunCount);
			Object->SetNumberField(TEXT("denyCount"), Eligibility.DenyCount);
			Object->SetNumberField(TEXT("capturedArgsCount"), Eligibility.CapturedArgsCount);
			Object->SetNumberField(TEXT("totalStepCount"), Eligibility.TotalStepCount);

			TArray<TSharedPtr<FJsonValue>> StepValues;
			StepValues.Reserve(Eligibility.Steps.Num());
			for (const FStepPolicy& Step : Eligibility.Steps)
			{
				TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
				StepObject->SetNumberField(TEXT("ordinal"), Step.Ordinal);
				StepObject->SetStringField(TEXT("toolName"), Step.ToolName);
				StepObject->SetStringField(TEXT("policyDecision"), Step.PolicyDecision);
				StepObject->SetStringField(TEXT("denyReason"), Step.DenyReason);
				StepObject->SetBoolField(TEXT("hasCapturedArgs"), Step.bHasCapturedArgs);
				StepObject->SetStringField(TEXT("captureEventId"), Step.CaptureEventId);
				StepObject->SetStringField(TEXT("captureSummary"), Step.CaptureSummary);
				StepObject->SetBoolField(TEXT("forceDryRun"), Step.bForceDryRun);
				StepValues.Add(MakeShared<FJsonValueObject>(StepObject));
			}
			Object->SetArrayField(TEXT("steps"), StepValues);
			return Object;
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceMakeResultContent(const FMakeCompositeResult& Result)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("action"), TEXT("task_atlas_make_composite"));
			Object->SetStringField(TEXT("outcome"), TaskAtlasServiceOutcomeToString(Result.Outcome));
			Object->SetStringField(TEXT("toolName"), Result.ToolName);
			Object->SetStringField(TEXT("generatedDir"), Result.GeneratedDir);
			Object->SetStringField(TEXT("stagingDir"), Result.StagingDir);
			Object->SetStringField(TEXT("documentPath"), Result.DocumentPath);
			Object->SetStringField(TEXT("compositeKind"), Result.CompositeKind);
			Object->SetStringField(TEXT("replayStatus"), Result.ReplayStatus);
			Object->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(Result.RejectedReasons));
			Object->SetStringField(TEXT("failureDiagnosticPath"), Result.FailureDiagnosticPath);
			Object->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Object->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			Object->SetObjectField(TEXT("eligibility"), TaskAtlasServiceEligibilityToJson(Result.Eligibility));
			return Object;
		}

		TArray<FString> TaskAtlasServiceReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, JsonValues) || !JsonValues)
			{
				return Values;
			}
			for (const TSharedPtr<FJsonValue>& Value : *JsonValues)
			{
				FString StringValue;
				if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.TrimStartAndEnd().IsEmpty())
				{
					Values.Add(StringValue.TrimStartAndEnd());
				}
			}
			return Values;
		}

		FString TaskAtlasServiceGetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
		}

		bool TaskAtlasServiceLoadTaskById(const FString& TaskId, FLoadedTask& OutTask, FString& OutFailureReason)
		{
			OutTask = FLoadedTask();
			OutFailureReason.Reset();

			const FString TrimmedTaskId = TaskId.TrimStartAndEnd();
			if (!TaskAtlasServiceIsSafeTaskId(TrimmedTaskId))
			{
				OutFailureReason = TEXT("Task id is missing or unsafe.");
				return false;
			}

			const FString TaskPath = FPaths::Combine(TaskAtlasServiceTaskRootDir(), TrimmedTaskId + TEXT(".json"));
			TSharedPtr<FJsonObject> TaskJson;
			if (!TaskAtlasServiceLoadJsonObject(TaskPath, TaskJson))
			{
				OutFailureReason = FString::Printf(TEXT("Task '%s' was not found."), *TrimmedTaskId);
				return false;
			}

			OutTask.Model.TaskId = TaskAtlasServiceGetStringField(TaskJson, TEXT("taskId"));
			if (OutTask.Model.TaskId.IsEmpty())
			{
				OutTask.Model.TaskId = TrimmedTaskId;
			}
			OutTask.Label = TaskAtlasServiceGetStringField(TaskJson, TEXT("label"));
			OutTask.Description = TaskAtlasServiceGetStringField(TaskJson, TEXT("aiSummaryText"));
			if (OutTask.Description.IsEmpty())
			{
				OutTask.Description = TaskAtlasServiceGetStringField(TaskJson, TEXT("userIntentText"));
			}
			if (OutTask.Description.IsEmpty())
			{
				OutTask.Description = FString::Printf(TEXT("Composite user tool generated from Task Atlas task %s."), *OutTask.Model.TaskId);
			}
			OutTask.Rating = TaskAtlasServiceGetStringField(TaskJson, TEXT("rating"));
			if (OutTask.Rating.IsEmpty())
			{
				OutTask.Rating = TEXT("unrated");
			}
			TaskJson->TryGetBoolField(TEXT("pinned"), OutTask.bPinned);
			OutTask.SessionId = TaskAtlasServiceGetStringField(TaskJson, TEXT("sessionId"));
			OutTask.TStartUtc = TaskAtlasServiceGetStringField(TaskJson, TEXT("tStartUtc"));
			OutTask.TEndUtc = TaskAtlasServiceGetStringField(TaskJson, TEXT("tEndUtc"));
			OutTask.UserIntent = TaskAtlasServiceGetStringField(TaskJson, TEXT("userIntentText"));
			OutTask.ReplayEligibility = TaskAtlasServiceGetStringField(TaskJson, TEXT("replayEligibility"));
			OutTask.ReplayUnavailableReason = TaskAtlasServiceGetStringField(TaskJson, TEXT("replayUnavailableReason"));
			OutTask.TaskPath = TaskPath;
			OutTask.TaskJson = TaskJson;
			OutTask.Model.CriticalPath = TaskAtlasServiceReadStringArrayField(TaskJson, TEXT("criticalPath"));

			const TArray<TSharedPtr<FJsonValue>>* StepRefs = nullptr;
			if (TaskJson->TryGetArrayField(TEXT("stepRefs"), StepRefs) && StepRefs)
			{
				for (const TSharedPtr<FJsonValue>& StepValue : *StepRefs)
				{
					const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
					if (!StepObject.IsValid())
					{
						continue;
					}

					FTaskAtlasStepRef Step;
					Step.ToolName = TaskAtlasServiceGetStringField(StepObject, TEXT("tool"));
					Step.EventId = TaskAtlasServiceGetStringField(StepObject, TEXT("eventId"));
					Step.CaptureStatus = TaskAtlasServiceGetStringField(StepObject, TEXT("captureStatus"));
					Step.CaptureRef = TaskAtlasServiceGetStringField(StepObject, TEXT("captureRef"));
					Step.CaptureSummary = TaskAtlasServiceGetStringField(StepObject, TEXT("captureSummary"));
					OutTask.Model.StepRefs.Add(MoveTemp(Step));
				}
			}
			return true;
		}

		TSet<FString> TaskAtlasServiceVisibleCoreToolNames()
		{
			TSet<FString> VisibleTools;
			for (const FToolRegistryEntry& Entry : GetToolRegistryEntries())
			{
				if (Entry.Exposure == EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
				{
					VisibleTools.Add(Entry.Name);
				}
			}
			return VisibleTools;
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasServiceMakeStepRefValues(const FTaskAtlasModel& Task, const FEligibilityResult& Eligibility)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(Task.StepRefs.Num());
			for (int32 Index = 0; Index < Task.StepRefs.Num(); ++Index)
			{
				const FTaskAtlasStepRef& StepRef = Task.StepRefs[Index];
				TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
				StepObject->SetNumberField(TEXT("ordinal"), Index);
				StepObject->SetStringField(TEXT("tool"), StepRef.ToolName);
				StepObject->SetStringField(TEXT("eventId"), StepRef.EventId);
				StepObject->SetStringField(TEXT("captureStatus"), StepRef.CaptureStatus);
				if (!StepRef.CaptureRef.TrimStartAndEnd().IsEmpty())
				{
					StepObject->SetStringField(TEXT("captureRef"), StepRef.CaptureRef);
				}
				if (Eligibility.Steps.IsValidIndex(Index))
				{
					StepObject->SetStringField(TEXT("policyClassAtCapture"), Eligibility.Steps[Index].PolicyDecision);
				}
				Values.Add(MakeShared<FJsonValueObject>(StepObject));
			}
			return Values;
		}

		bool TaskAtlasServiceWriteDocumentDraft(
			const FLoadedTask& Task,
			const FEligibilityResult& Eligibility,
			const FString& Kind,
			FString& OutPath,
			FString& OutFailureReason)
		{
			OutPath.Reset();
			OutFailureReason.Reset();

			const FString SafeTaskId = TaskAtlasServiceSanitizeTaskToken(Task.Model.TaskId);
			const FString Path = FPaths::Combine(TaskAtlasServiceDraftRootDir(), FString::Printf(TEXT("%s.%s.md"), *SafeTaskId, *Kind));
			FString Markdown;
			Markdown += FString::Printf(TEXT("# %s\n\n"), *(Task.Label.IsEmpty() ? Task.Model.TaskId : Task.Label));
			Markdown += FString::Printf(TEXT("- taskId: `%s`\n"), *Task.Model.TaskId);
			Markdown += FString::Printf(TEXT("- outcome: `%s`\n"), *Kind);
			if (Eligibility.BlockedFirstStep >= 0)
			{
				Markdown += FString::Printf(
					TEXT("- blocked first step: `%d`, reason `%s`\n"),
					Eligibility.BlockedFirstStep,
					*Eligibility.BlockedFirstReason);
			}
			Markdown += TEXT("\n## Denied Steps\n\n");
			bool bAnyDenied = false;
			for (const FStepPolicy& Step : Eligibility.Steps)
			{
				if (Step.PolicyDecision == TEXT("deny"))
				{
					bAnyDenied = true;
					Markdown += FString::Printf(
						TEXT("- step `%d` `%s`: `%s`\n"),
						Step.Ordinal,
						*Step.ToolName,
						*Step.DenyReason);
				}
			}
			if (!bAnyDenied)
			{
				Markdown += TEXT("- none\n");
			}
			Markdown += TEXT("\nRecommendation: use Distill Skill or To RAG for this task until the workflow has safe captured preview data.\n");

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveStringToFile(Markdown, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write document draft '%s'."), *Path);
				return false;
			}

			OutPath = TaskAtlasServiceNormalizeAbsolutePath(Path);
			return true;
		}

		bool TaskAtlasServiceNormalizeCompositeMetadata(
			const FString& ToolName,
			const FString& MainPySha256,
			const FLoadedTask& Task,
			const FString& CreatedUtc,
			FString& InOutToolJson,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			TSharedPtr<FJsonObject> ToolJson;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InOutToolJson);
			if (!FJsonSerializer::Deserialize(Reader, ToolJson) || !ToolJson.IsValid())
			{
				OutFailureReason = TEXT("Generated tool.json did not parse.");
				return false;
			}

			ToolJson->SetStringField(TEXT("name"), ToolName);
			ToolJson->SetStringField(TEXT("generator"), TEXT("task_atlas_make_composite"));
			ToolJson->SetStringField(TEXT("compositeKind"), TEXT("preview"));
			ToolJson->SetStringField(TEXT("replayStatus"), TEXT("preview_ready"));
			ToolJson->SetStringField(TEXT("replayEligibility"), TEXT("preview_ready"));
			ToolJson->SetStringField(TEXT("sourceTaskId"), Task.Model.TaskId);
			ToolJson->SetStringField(TEXT("taskId"), Task.Model.TaskId);
			ToolJson->SetStringField(TEXT("createdUtc"), CreatedUtc);
			ToolJson->SetStringField(TEXT("pythonHandlerSha256"), MainPySha256);

			InOutToolJson = TaskAtlasServiceJsonToPrettyString(ToolJson);
			return true;
		}

		FString TaskAtlasServiceBuildReadme(const FLoadedTask& Task, const FString& ToolName, const FEligibilityResult& Eligibility)
		{
			FString Text;
			Text += FString::Printf(TEXT("# %s\n\n"), *ToolName);
			Text += TEXT("Generated by Task Atlas MakeComposite.\n\n");
			Text += TEXT("This is preview-only composite glue. It is not real replay, and write-capable steps remain controlled by call_tool policy, including forced dry-run behavior.\n\n");
			Text += FString::Printf(TEXT("- sourceTaskId: `%s`\n"), *Task.Model.TaskId);
			Text += FString::Printf(TEXT("- replayStatus: `%s`\n"), TEXT("preview_ready"));
			Text += FString::Printf(TEXT("- stepCount: `%d`\n"), Eligibility.TotalStepCount);
			Text += TEXT("\nCaptured defaults are sanitized public data only; raw private captured arguments are not written into this generated tool.\n");
			return Text;
		}

		bool TaskAtlasServiceValidateStaging(
			const FString& StagingDir,
			const FString& ToolName,
			const FString& ExpectedSha256,
			TSharedPtr<FJsonObject>& OutDiagnostic,
			FString& OutErrorCode,
			FString& OutErrorMessage)
		{
			OutDiagnostic = MakeShared<FJsonObject>();
			OutDiagnostic->SetStringField(TEXT("stagingDir"), StagingDir);
			OutErrorCode.Reset();
			OutErrorMessage.Reset();

			const FString MainPyPath = FPaths::Combine(StagingDir, TEXT("main.py"));
			const FString ToolJsonPath = FPaths::Combine(StagingDir, TEXT("tool.json"));
			if (!FPaths::FileExists(MainPyPath))
			{
				OutErrorCode = TEXT("staging_missing_main_py");
				OutErrorMessage = TEXT("Staging validation failed: main.py is missing.");
				return false;
			}

			TSharedPtr<FJsonObject> ToolJson;
			if (!TaskAtlasServiceLoadJsonObject(ToolJsonPath, ToolJson))
			{
				OutErrorCode = TEXT("staging_invalid_tool_json");
				OutErrorMessage = TEXT("Staging validation failed: tool.json is missing or invalid.");
				return false;
			}

			static const TCHAR* RequiredStringFields[] = {
				TEXT("name"),
				TEXT("generator"),
				TEXT("compositeKind"),
				TEXT("replayStatus"),
				TEXT("pythonHandlerSha256"),
				TEXT("sourceTaskId"),
				TEXT("createdUtc")
			};
			for (const TCHAR* FieldName : RequiredStringFields)
			{
				FString Value;
				if (!TaskAtlasServiceReadRequiredString(ToolJson, FieldName, Value))
				{
					OutErrorCode = FString::Printf(TEXT("staging_missing_%s"), FieldName);
					OutErrorMessage = FString::Printf(TEXT("Staging validation failed: tool.json missing required field '%s'."), FieldName);
					return false;
				}
			}

			FString DeclaredToolName;
			ToolJson->TryGetStringField(TEXT("name"), DeclaredToolName);
			if (DeclaredToolName != ToolName)
			{
				OutErrorCode = TEXT("staging_tool_name_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: tool.json name does not match the target tool.");
				return false;
			}

			FString Generator;
			ToolJson->TryGetStringField(TEXT("generator"), Generator);
			if (Generator != TEXT("task_atlas_make_composite"))
			{
				OutErrorCode = TEXT("staging_generator_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: tool.json generator is incorrect.");
				return false;
			}

			TArray<uint8> MainPyBytes;
			if (!FFileHelper::LoadFileToArray(MainPyBytes, *MainPyPath))
			{
				OutErrorCode = TEXT("staging_main_py_read_failed");
				OutErrorMessage = TEXT("Staging validation failed: could not read main.py.");
				return false;
			}

			FString DeclaredSha256;
			ToolJson->TryGetStringField(TEXT("pythonHandlerSha256"), DeclaredSha256);
			const FString ActualSha256 = HashUtils::Sha256LowerHex(MainPyBytes).ToLower();
			OutDiagnostic->SetStringField(TEXT("declaredSha256"), DeclaredSha256);
			OutDiagnostic->SetStringField(TEXT("actualSha256"), ActualSha256);
			OutDiagnostic->SetStringField(TEXT("expectedSha256"), ExpectedSha256);
			if (!DeclaredSha256.Equals(ExpectedSha256, ESearchCase::CaseSensitive)
				|| !ActualSha256.Equals(DeclaredSha256, ESearchCase::CaseSensitive))
			{
				OutErrorCode = TEXT("staging_sha_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: pythonHandlerSha256 does not match main.py.");
				return false;
			}

			const TSharedPtr<FJsonObject>* InputSchema = nullptr;
			if (!ToolJson->TryGetObjectField(TEXT("inputSchema"), InputSchema) || !InputSchema || !InputSchema->IsValid())
			{
				OutErrorCode = TEXT("staging_missing_input_schema");
				OutErrorMessage = TEXT("Staging validation failed: tool.json missing inputSchema.");
				return false;
			}
			bool bAdditionalProperties = true;
			(*InputSchema)->TryGetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
			if (bAdditionalProperties)
			{
				OutErrorCode = TEXT("staging_open_input_schema");
				OutErrorMessage = TEXT("Staging validation failed: inputSchema must be closed at the root.");
				return false;
			}

			return true;
		}

		bool TaskAtlasServiceWriteStagingFiles(
			const FString& StagingDir,
			const FString& MainPy,
			const FString& ToolJson,
			const FString& Readme,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			if (!IFileManager::Get().MakeDirectory(*StagingDir, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create staging directory '%s'."), *StagingDir);
				return false;
			}
			if (!FFileHelper::SaveStringToFile(MainPy, *FPaths::Combine(StagingDir, TEXT("main.py")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging main.py.");
				return false;
			}
			if (!FFileHelper::SaveStringToFile(ToolJson, *FPaths::Combine(StagingDir, TEXT("tool.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging tool.json.");
				return false;
			}
			if (!FFileHelper::SaveStringToFile(Readme, *FPaths::Combine(StagingDir, TEXT("README.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging README.md.");
				return false;
			}
			return true;
		}

		FString TaskAtlasServiceWriteReloadRejectedDiagnostic(
			const FString& ToolName,
			const FString& TaskId,
			const FString& Eligibility,
			const TArray<FString>& RejectedReasons,
			const FString& StagingDir,
			const FString& GeneratedDir,
			int32 ReloadBeforeCount,
			int32 ReloadAfterCount,
			const FString& TimestampUtc)
		{
			const FString SafeName = SanitizeToolIdPart(ToolName);
			FString SafeTimestamp = TimestampUtc;
			SafeTimestamp.ReplaceInline(TEXT(":"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("-"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("."), TEXT(""));
			const FString DiagnosticDir = FPaths::Combine(
				TaskAtlasServiceFailureRootDir(),
				FString::Printf(TEXT("%s-%s"), *SafeTimestamp, *SafeName));
			const FString DiagnosticPath = FPaths::Combine(DiagnosticDir, TEXT("diagnostic.json"));

			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolName"), ToolName);
			Diagnostic->SetStringField(TEXT("taskId"), TaskId);
			Diagnostic->SetStringField(TEXT("outcome"), TEXT("ReloadRejected"));
			Diagnostic->SetStringField(TEXT("eligibility"), Eligibility);
			Diagnostic->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(RejectedReasons));
			Diagnostic->SetStringField(TEXT("stagingDir"), StagingDir);
			Diagnostic->SetStringField(TEXT("generatedDir"), GeneratedDir);
			Diagnostic->SetNumberField(TEXT("reloadBeforeCount"), ReloadBeforeCount);
			Diagnostic->SetNumberField(TEXT("reloadAfterCount"), ReloadAfterCount);
			Diagnostic->SetStringField(TEXT("timestampUtc"), TimestampUtc);

			return TaskAtlasServiceWriteJsonObject(Diagnostic, DiagnosticPath)
				? TaskAtlasServiceNormalizeAbsolutePath(DiagnosticPath)
				: FString();
		}

		bool TaskAtlasServiceTryExtractAtlasToolId(const FString& RawToolName, FString& OutToolName, FString& OutToolId)
		{
			OutToolName = RawToolName.TrimStartAndEnd();
			OutToolId.Reset();
			if (!OutToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive))
			{
				return false;
			}

			OutToolId = OutToolName.RightChop(5);
			if (!OutToolId.StartsWith(TEXT("atlas_"), ESearchCase::CaseSensitive) || OutToolId.Len() <= 6)
			{
				return false;
			}

			for (const TCHAR Character : OutToolId)
			{
				const bool bLower = Character >= TCHAR('a') && Character <= TCHAR('z');
				const bool bDigit = Character >= TCHAR('0') && Character <= TCHAR('9');
				if (!bLower && !bDigit && Character != TCHAR('_'))
				{
					return false;
				}
			}
			return TaskAtlasServiceIsSafeDirectoryName(OutToolId);
		}

		bool TaskAtlasServiceResolveMadeTool(
			const FString& RawToolName,
			bool bRequireExistingDirectory,
			FResolvedMadeTool& OutTool,
			FString& OutErrorCode,
			FString& OutErrorMessage)
		{
			OutTool = FResolvedMadeTool();
			OutErrorCode.Reset();
			OutErrorMessage.Reset();

			if (!TaskAtlasServiceTryExtractAtlasToolId(RawToolName, OutTool.ToolName, OutTool.ToolId))
			{
				OutErrorCode = TEXT("invalid_name");
				OutErrorMessage = TEXT("ToolName must use the generated Task Atlas form user.atlas_<snake>.");
				return false;
			}

			OutTool.RootDir = TaskAtlasServiceMadeToolsRootDir();
			const FString CanonicalRootDir = IFileManager::Get().DirectoryExists(*OutTool.RootDir)
				? TaskAtlasServiceCanonicalExistingPath(OutTool.RootDir)
				: TaskAtlasServiceNormalizeAbsolutePath(OutTool.RootDir);
			OutTool.TargetDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(OutTool.RootDir, OutTool.ToolId));
			if (!TaskAtlasServicePathEqualsOrChild(OutTool.TargetDir, CanonicalRootDir))
			{
				OutErrorCode = TEXT("path_unsafe");
				OutErrorMessage = TEXT("Resolved user tool directory is outside the user-tool registry root.");
				return false;
			}

			if (!IFileManager::Get().DirectoryExists(*OutTool.TargetDir))
			{
				if (bRequireExistingDirectory)
				{
					OutErrorCode = TEXT("not_found");
					OutErrorMessage = FString::Printf(TEXT("Generated user tool directory does not exist: %s"), *OutTool.TargetDir);
					return false;
				}
				return true;
			}

			if (TaskAtlasServiceIsSymlink(OutTool.TargetDir))
			{
				OutErrorCode = TEXT("path_unsafe");
				OutErrorMessage = TEXT("Refused to use a symlinked user tool directory.");
				return false;
			}

			OutTool.TargetDir = TaskAtlasServiceCanonicalExistingPath(OutTool.TargetDir);
			if (!TaskAtlasServicePathEqualsOrChild(OutTool.TargetDir, CanonicalRootDir))
			{
				OutErrorCode = TEXT("path_unsafe");
				OutErrorMessage = TEXT("Refused symlink/path escape outside the user-tool registry root.");
				return false;
			}

			OutTool.ToolJsonPath = FPaths::Combine(OutTool.TargetDir, TEXT("tool.json"));
			if (!FPaths::FileExists(OutTool.ToolJsonPath) || TaskAtlasServiceIsSymlink(OutTool.ToolJsonPath))
			{
				OutErrorCode = TEXT("not_task_atlas_generated");
				OutErrorMessage = TEXT("Refused because tool.json is missing or unsafe.");
				return false;
			}

			if (!TaskAtlasServiceLoadJsonObject(OutTool.ToolJsonPath, OutTool.ToolJson))
			{
				OutErrorCode = TEXT("not_task_atlas_generated");
				OutErrorMessage = TEXT("Refused because tool.json is not valid JSON.");
				return false;
			}

			FString Generator;
			if (!TaskAtlasServiceReadRequiredString(OutTool.ToolJson, TEXT("generator"), Generator)
				|| Generator != TEXT("task_atlas_make_composite"))
			{
				OutErrorCode = TEXT("not_task_atlas_generated");
				OutErrorMessage = TEXT("Refused because this user tool was not generated by Task Atlas Make Tool Set.");
				return false;
			}

			FString DeclaredToolName;
			if (!TaskAtlasServiceReadRequiredString(OutTool.ToolJson, TEXT("name"), DeclaredToolName)
				|| DeclaredToolName != OutTool.ToolName)
			{
				OutErrorCode = TEXT("invalid_name");
				OutErrorMessage = TEXT("Refused because tool.json name does not match the requested user tool.");
				return false;
			}

			return true;
		}

		const TCHAR* TaskAtlasServiceDeleteOutcomeToString(EDeleteMadeToolOutcome Outcome)
		{
			switch (Outcome)
			{
			case EDeleteMadeToolOutcome::Deleted: return TEXT("Deleted");
			case EDeleteMadeToolOutcome::DryRun: return TEXT("DryRun");
			case EDeleteMadeToolOutcome::NotFound: return TEXT("NotFound");
			case EDeleteMadeToolOutcome::ReloadRejected: return TEXT("ReloadRejected");
			case EDeleteMadeToolOutcome::Failed: return TEXT("Failed");
			case EDeleteMadeToolOutcome::Refused:
			default: return TEXT("Refused");
			}
		}

		const TCHAR* TaskAtlasServicePromoteOutcomeToString(EPromoteToRagOutcome Outcome)
		{
			switch (Outcome)
			{
			case EPromoteToRagOutcome::Promoted: return TEXT("Promoted");
			case EPromoteToRagOutcome::DryRun: return TEXT("DryRun");
			case EPromoteToRagOutcome::NotFound: return TEXT("NotFound");
			case EPromoteToRagOutcome::Refused: return TEXT("Refused");
			case EPromoteToRagOutcome::RefreshFailed: return TEXT("RefreshFailed");
			case EPromoteToRagOutcome::Failed:
			default: return TEXT("Failed");
			}
		}

		const TCHAR* TaskAtlasServiceSmokeOutcomeToString(ESmokeOutcome Outcome)
		{
			switch (Outcome)
			{
			case ESmokeOutcome::Passed: return TEXT("Passed");
			case ESmokeOutcome::Failed: return TEXT("Failed");
			case ESmokeOutcome::DryRun: return TEXT("DryRun");
			case ESmokeOutcome::NotFound: return TEXT("NotFound");
			case ESmokeOutcome::ReloadRejected: return TEXT("ReloadRejected");
			case ESmokeOutcome::FailedToExecute: return TEXT("FailedToExecute");
			case ESmokeOutcome::Refused:
			default: return TEXT("Refused");
			}
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceMakeDeleteResultContent(const FDeleteMadeToolResult& Result)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("action"), TEXT("task_atlas_delete_made_tool"));
			Object->SetStringField(TEXT("outcome"), TaskAtlasServiceDeleteOutcomeToString(Result.Outcome));
			Object->SetStringField(TEXT("toolName"), Result.ToolName);
			Object->SetStringField(TEXT("removedDir"), Result.RemovedDir);
			Object->SetBoolField(TEXT("wasStaleEntry"), Result.bWasStaleEntry);
			Object->SetNumberField(TEXT("reloadRemovedCount"), Result.ReloadRemovedCount);
			Object->SetNumberField(TEXT("reloadBeforeCount"), Result.ReloadBeforeCount);
			Object->SetNumberField(TEXT("reloadAfterCount"), Result.ReloadAfterCount);
			Object->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(Result.RejectedReasons));
			Object->SetStringField(TEXT("failureDiagnosticPath"), Result.FailureDiagnosticPath);
			Object->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Object->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			return Object;
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceMakePromoteResultContent(const FPromoteToRagResult& Result)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("action"), TEXT("task_atlas_promote_to_rag"));
			Object->SetStringField(TEXT("outcome"), TaskAtlasServicePromoteOutcomeToString(Result.Outcome));
			Object->SetStringField(TEXT("taskId"), Result.TaskId);
			Object->SetBoolField(TEXT("dryRun"), Result.bDryRun);
			Object->SetStringField(TEXT("sourcePath"), Result.SourcePath);
			Object->SetStringField(TEXT("knowledgeSourcePath"), Result.KnowledgeSourcePath);
			Object->SetNumberField(TEXT("markdownLength"), Result.MarkdownLength);
			Object->SetStringField(TEXT("refreshResultText"), Result.RefreshResultText);
			if (Result.RefreshResult.IsValid())
			{
				Object->SetObjectField(TEXT("refreshResult"), Result.RefreshResult);
			}
			Object->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Object->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			return Object;
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceMakeSmokeResultContent(const FSmokeResult& Result)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("action"), TEXT("task_atlas_smoke_made_tool"));
			Object->SetStringField(TEXT("outcome"), TaskAtlasServiceSmokeOutcomeToString(Result.Outcome));
			Object->SetStringField(TEXT("toolName"), Result.ToolName);
			Object->SetStringField(TEXT("smokeText"), Result.SmokeText);
			Object->SetBoolField(TEXT("verdictMatchesEligibility"), Result.bVerdictMatchesEligibility);
			Object->SetBoolField(TEXT("dryRun"), Result.bDryRun);
			Object->SetStringField(TEXT("replayStatusBefore"), Result.ReplayStatusBefore);
			Object->SetStringField(TEXT("replayStatusAfter"), Result.ReplayStatusAfter);
			Object->SetStringField(TEXT("failureDiagnosticPath"), Result.FailureDiagnosticPath);
			Object->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Object->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			return Object;
		}

		FString TaskAtlasServiceWriteOperationDiagnostic(
			const FString& Operation,
			const FString& SafeName,
			const TSharedPtr<FJsonObject>& Diagnostic)
		{
			const FString TimestampUtc = FDateTime::UtcNow().ToIso8601();
			const FString DiagnosticDir = FPaths::Combine(
				TaskAtlasServiceFailureRootDir(),
				FString::Printf(
					TEXT("%s-%s-%s"),
					*TaskAtlasServiceMakeSafeTimestamp(TimestampUtc),
					*Operation,
					*SafeName));
			const FString DiagnosticPath = FPaths::Combine(DiagnosticDir, TEXT("diagnostic.json"));

			TSharedPtr<FJsonObject> Object = Diagnostic.IsValid() ? Diagnostic : MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("operation"), Operation);
			Object->SetStringField(TEXT("timestampUtc"), TimestampUtc);
			return TaskAtlasServiceWriteJsonObject(Object, DiagnosticPath)
				? TaskAtlasServiceNormalizeAbsolutePath(DiagnosticPath)
				: FString();
		}

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
		FString TaskAtlasServiceOfficialDraftsRootDir()
		{
			return TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("OfficialToolsetDrafts")));
		}

		FString TaskAtlasServiceQuoteCommandLineArgument(FString Value)
		{
			Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
			return TEXT("\"") + Value + TEXT("\"");
		}

		FString TaskAtlasServiceOfficialCppBuildLogsRootDir()
		{
			return TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("OfficialToolsetBuildLogs")));
		}

		FString TaskAtlasServiceBuildScriptPath()
		{
			const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
#if PLATFORM_WINDOWS
			return FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
#elif PLATFORM_MAC
			return FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Mac/Build.sh"));
#elif PLATFORM_LINUX
			return FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Linux/Build.sh"));
#else
			return FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.sh"));
#endif
		}

		FString TaskAtlasServiceHostPlatformName()
		{
#if PLATFORM_WINDOWS
			return TEXT("Win64");
#elif PLATFORM_MAC
			return TEXT("Mac");
#elif PLATFORM_LINUX
			return TEXT("Linux");
#else
			return TEXT("Unknown");
#endif
		}

		FString TaskAtlasServiceLeafName(const FString& Path)
		{
			FString Normalized = TaskAtlasServiceNormalizeAbsolutePath(Path);
			return FPaths::GetCleanFilename(Normalized);
		}

		bool TaskAtlasServiceResolveOfficialCppExample58Project(FString& OutExampleRoot, FString& OutProjectFile, FString& OutFailureReason)
		{
			OutExampleRoot.Reset();
			OutProjectFile.Reset();
			OutFailureReason.Reset();

#if WITH_DEV_AUTOMATION_TESTS
			if (!GTaskAtlasServiceOfficialCppExampleRootForTests.IsEmpty())
			{
				OutExampleRoot = TaskAtlasServiceNormalizeAbsolutePath(GTaskAtlasServiceOfficialCppExampleRootForTests);
				OutProjectFile = FPaths::Combine(OutExampleRoot, TEXT("UEvolveExample58.uproject"));
				return true;
			}
#endif

			const FString ProjectDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::ProjectDir());
			TArray<FString> CandidateRoots;
			CandidateRoots.Add(FPaths::Combine(ProjectDir, TEXT("Examples/UEvolveExample58")));
			if (TaskAtlasServiceLeafName(ProjectDir) == TEXT("UEvolveExample58"))
			{
				CandidateRoots.Add(ProjectDir);
			}
			CandidateRoots.Add(FPaths::Combine(ProjectDir, TEXT("../UEvolveExample58")));
			CandidateRoots.Add(FPaths::Combine(ProjectDir, TEXT("../../Examples/UEvolveExample58")));

			for (const FString& CandidateRoot : CandidateRoots)
			{
				const FString NormalizedRoot = TaskAtlasServiceNormalizeAbsolutePath(CandidateRoot);
				const FString ProjectFile = FPaths::Combine(NormalizedRoot, TEXT("UEvolveExample58.uproject"));
				if (FPaths::FileExists(ProjectFile))
				{
					OutExampleRoot = NormalizedRoot;
					OutProjectFile = ProjectFile;
					return true;
				}
			}

			OutFailureReason = TEXT("Could not resolve Examples/UEvolveExample58/UEvolveExample58.uproject for the official C++ build probe.");
			return false;
		}

		bool TaskAtlasServiceIsOfficialCppEditorRunning()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (GTaskAtlasServiceOfficialCppEditorRunningForTests.IsSet())
			{
				return GTaskAtlasServiceOfficialCppEditorRunningForTests.GetValue();
			}
#endif

			int32 ReturnCode = -1;
			FString StdOut;
			FString StdErr;
#if PLATFORM_WINDOWS
			const bool bLaunched = FPlatformProcess::ExecProcess(
				TEXT("cmd.exe"),
				TEXT("/C tasklist /FI \"IMAGENAME eq UnrealEditor.exe\" /NH"),
				&ReturnCode,
				&StdOut,
				&StdErr);
			return bLaunched && ReturnCode == 0 && StdOut.Contains(TEXT("UnrealEditor.exe"), ESearchCase::IgnoreCase);
#else
			const bool bLaunched = FPlatformProcess::ExecProcess(
				TEXT("/usr/bin/pgrep"),
				TEXT("-x UnrealEditor"),
				&ReturnCode,
				&StdOut,
				&StdErr);
			return bLaunched && ReturnCode == 0 && !StdOut.TrimStartAndEnd().IsEmpty();
#endif
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceBuildStatusObject(const FOfficialCppToolsetBuildDraftResult& Result)
		{
			TSharedPtr<FJsonObject> BuildStatus = MakeShared<FJsonObject>();
			BuildStatus->SetStringField(TEXT("state"), Result.BuildStatus);
			BuildStatus->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			BuildStatus->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			BuildStatus->SetStringField(TEXT("instructions"), Result.Instructions);
			BuildStatus->SetStringField(TEXT("logPath"), Result.BuildLogPath);
			BuildStatus->SetStringField(TEXT("mountedPluginDir"), Result.MountedPluginDir);
			BuildStatus->SetBoolField(TEXT("mountedCopyRemoved"), Result.bMountedCopyRemoved);
			BuildStatus->SetBoolField(TEXT("cleanupFailed"), Result.bCleanupFailed);
			BuildStatus->SetStringField(TEXT("cleanupErrorMessage"), Result.CleanupErrorMessage);
			BuildStatus->SetNumberField(TEXT("returnCode"), Result.ReturnCode);
			BuildStatus->SetNumberField(TEXT("elapsedSeconds"), Result.ElapsedSeconds);
			BuildStatus->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());
			return BuildStatus;
		}

		bool TaskAtlasServiceUpdateOfficialCppBuildManifest(FOfficialCppToolsetBuildDraftResult& Result)
		{
			if (Result.ManifestPath.IsEmpty())
			{
				return false;
			}

			TSharedPtr<FJsonObject> Manifest;
			if (!TaskAtlasServiceLoadJsonObject(Result.ManifestPath, Manifest) || !Manifest.IsValid())
			{
				return false;
			}
			Manifest->SetObjectField(TEXT("buildStatus"), TaskAtlasServiceBuildStatusObject(Result));
			Manifest->SetBoolField(TEXT("restartRequired"), true);
			TSharedPtr<FJsonObject> RegistrationStatus;
			const TSharedPtr<FJsonObject>* ExistingRegistrationStatus = nullptr;
			if (Manifest->TryGetObjectField(TEXT("registrationStatus"), ExistingRegistrationStatus) && ExistingRegistrationStatus && (*ExistingRegistrationStatus).IsValid())
			{
				RegistrationStatus = *ExistingRegistrationStatus;
			}
			else
			{
				RegistrationStatus = MakeShared<FJsonObject>();
				Manifest->SetObjectField(TEXT("registrationStatus"), RegistrationStatus);
			}
			RegistrationStatus->SetStringField(TEXT("state"), TEXT("requires_build_restart"));
			RegistrationStatus->SetStringField(TEXT("lastError"), Result.ErrorMessage);
			const bool bWrote = TaskAtlasServiceWriteJsonObject(Manifest, Result.ManifestPath);
			Result.ManifestJson = TaskAtlasServiceJsonToPrettyString(Manifest);
			return bWrote;
		}

		void TaskAtlasServiceSetOfficialCppBuildStatus(
			FOfficialCppToolsetBuildDraftResult& Result,
			const FString& BuildStatus,
			const FString& ErrorCode,
			const FString& ErrorMessage,
			const FString& Instructions = FString())
		{
			Result.BuildStatus = BuildStatus;
			Result.ErrorCode = ErrorCode;
			Result.ErrorMessage = ErrorMessage;
			Result.Instructions = Instructions;
			Result.bSucceeded = BuildStatus == TEXT("succeeded");
		}

		FString TaskAtlasServicePythonStringLiteral(const FString& Value)
		{
			FString Result = TEXT("\"");
			for (const TCHAR Character : Value)
			{
				if (Character == TEXT('\\'))
				{
					Result += TEXT("\\\\");
				}
				else if (Character == TEXT('"'))
				{
					Result += TEXT("\\\"");
				}
				else if (Character == TEXT('\n'))
				{
					Result += TEXT("\\n");
				}
				else if (Character == TEXT('\r'))
				{
					Result += TEXT("\\r");
				}
				else if (Character == TEXT('\t'))
				{
					Result += TEXT("\\t");
				}
				else
				{
					Result.AppendChar(Character);
				}
			}
			Result += TEXT("\"");
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasServiceMakeIssueValues(const TArray<FString>& Issues)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Issue : Issues)
			{
				Values.Add(MakeShared<FJsonValueString>(Issue));
			}
			return Values;
		}

		bool TaskAtlasServiceResolveOfficialValidatorPath(FString& OutValidatorPath, FString& OutFailureReason)
		{
			OutValidatorPath.Reset();
			OutFailureReason.Reset();
			const FToolsReadResolution Resolution = ResolveToolsReadSubpath(
				TEXT("UnrealMcpOfficialToolsets/validate_official_toolset.py"),
				{ TEXT("validate_official_toolset.py") });
			if (!Resolution.bFound || !FPaths::FileExists(Resolution.Path))
			{
				OutFailureReason = Resolution.Warning.IsEmpty()
					? FString(TEXT("Could not resolve Tools/UnrealMcpOfficialToolsets/validate_official_toolset.py."))
					: Resolution.Warning;
				return false;
			}
			OutValidatorPath = Resolution.Path;
			return true;
		}

		bool TaskAtlasServiceRunOfficialValidator(
			const FString& SourcePath,
			TArray<FString>& OutIssues,
			FString& OutFailureReason)
		{
			OutIssues.Reset();
			OutFailureReason.Reset();

			FString ValidatorPath;
			if (!TaskAtlasServiceResolveOfficialValidatorPath(ValidatorPath, OutFailureReason))
			{
				OutIssues.Add(OutFailureReason);
				return false;
			}

			const FString Params = FString::Printf(
				TEXT("%s %s"),
				*TaskAtlasServiceQuoteCommandLineArgument(ValidatorPath),
				*TaskAtlasServiceQuoteCommandLineArgument(SourcePath));
			int32 ReturnCode = -1;
			FString StdOut;
			FString StdErr;
			const bool bLaunched = FPlatformProcess::ExecProcess(
#if PLATFORM_MAC
				TEXT("/usr/bin/python3"),
#else
				TEXT("python3"),
#endif
				*Params,
				&ReturnCode,
				&StdOut,
				&StdErr,
				*FPaths::ProjectDir());

			TArray<FString> Lines;
			(StdOut + TEXT("\n") + StdErr).ParseIntoArrayLines(Lines, true);
			for (const FString& Line : Lines)
			{
				const FString Trimmed = Line.TrimStartAndEnd();
				if (!Trimmed.IsEmpty())
				{
					OutIssues.Add(Trimmed);
				}
			}

			if (!bLaunched)
			{
				OutFailureReason = TEXT("Failed to launch python3 for official toolset validation.");
				OutIssues.Add(OutFailureReason);
				return false;
			}
			if (ReturnCode != 0)
			{
				OutFailureReason = FString::Printf(TEXT("Official toolset validator exited with code %d."), ReturnCode);
				if (OutIssues.Num() == 0)
				{
					OutIssues.Add(OutFailureReason);
				}
				return false;
			}
			if (OutIssues.Num() > 0)
			{
				OutFailureReason = TEXT("Official toolset validator returned issues.");
				return false;
			}
			return true;
		}

		void TaskAtlasServiceUpdateOfficialManifestPaths(
			TSharedPtr<FJsonObject>& Manifest,
			const FString& ModulePath,
			const FString& SourcePath,
			const FString& DeletePath)
		{
			if (!Manifest.IsValid())
			{
				Manifest = MakeShared<FJsonObject>();
			}
			Manifest->SetStringField(TEXT("modulePath"), ModulePath);
			Manifest->SetStringField(TEXT("sourcePath"), SourcePath);
			TSharedPtr<FJsonObject> Rollback;
			const TSharedPtr<FJsonObject>* ExistingRollback = nullptr;
			if (Manifest->TryGetObjectField(TEXT("rollback"), ExistingRollback) && ExistingRollback && (*ExistingRollback).IsValid())
			{
				Rollback = *ExistingRollback;
			}
			else
			{
				Rollback = MakeShared<FJsonObject>();
				Manifest->SetObjectField(TEXT("rollback"), Rollback);
			}
			Rollback->SetStringField(TEXT("deletePath"), DeletePath);
		}

		void TaskAtlasServiceUpdateOfficialValidatorStatus(
			TSharedPtr<FJsonObject>& Manifest,
			bool bPassed,
			const TArray<FString>& Issues)
		{
			if (!Manifest.IsValid())
			{
				Manifest = MakeShared<FJsonObject>();
			}
			TSharedPtr<FJsonObject> ValidatorStatus;
			const TSharedPtr<FJsonObject>* ExistingValidatorStatus = nullptr;
			if (Manifest->TryGetObjectField(TEXT("validatorStatus"), ExistingValidatorStatus) && ExistingValidatorStatus && (*ExistingValidatorStatus).IsValid())
			{
				ValidatorStatus = *ExistingValidatorStatus;
			}
			else
			{
				ValidatorStatus = MakeShared<FJsonObject>();
				Manifest->SetObjectField(TEXT("validatorStatus"), ValidatorStatus);
			}
			ValidatorStatus->SetBoolField(TEXT("passed"), bPassed);
			ValidatorStatus->SetArrayField(TEXT("issues"), TaskAtlasServiceMakeIssueValues(Issues));
			ValidatorStatus->SetStringField(TEXT("validatedAt"), FDateTime::UtcNow().ToIso8601());
		}

		void TaskAtlasServiceUpdateOfficialRegistrationStatus(
			TSharedPtr<FJsonObject>& Manifest,
			const FString& State,
			const FString& LastError)
		{
			if (!Manifest.IsValid())
			{
				Manifest = MakeShared<FJsonObject>();
			}
			TSharedPtr<FJsonObject> RegistrationStatus;
			const TSharedPtr<FJsonObject>* ExistingRegistrationStatus = nullptr;
			if (Manifest->TryGetObjectField(TEXT("registrationStatus"), ExistingRegistrationStatus) && ExistingRegistrationStatus && (*ExistingRegistrationStatus).IsValid())
			{
				RegistrationStatus = *ExistingRegistrationStatus;
			}
			else
			{
				RegistrationStatus = MakeShared<FJsonObject>();
				Manifest->SetObjectField(TEXT("registrationStatus"), RegistrationStatus);
			}
			RegistrationStatus->SetStringField(TEXT("state"), State);
			RegistrationStatus->SetStringField(TEXT("lastError"), LastError);
			RegistrationStatus->SetStringField(TEXT("registeredAt"), State == TEXT("registered") ? FDateTime::UtcNow().ToIso8601() : FString());
		}

		IPythonScriptPlugin* TaskAtlasServiceLoadPythonScriptPlugin()
		{
			static const FName PythonScriptPluginModuleName(TEXT("PythonScriptPlugin"));
			if (IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName))
			{
				return PythonPlugin;
			}
			return FModuleManager::LoadModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName);
		}

		bool TaskAtlasServiceRunOfficialPythonJsonCommand(
			const FString& Command,
			TSharedPtr<FJsonObject>& OutResult,
			FString& OutFailureReason)
		{
			OutResult.Reset();
			OutFailureReason.Reset();

			IPythonScriptPlugin* PythonPlugin = TaskAtlasServiceLoadPythonScriptPlugin();
			if (!PythonPlugin)
			{
				OutFailureReason = TEXT("PythonScriptPlugin is not loaded.");
				return false;
			}
			if (!PythonPlugin->IsPythonInitialized())
			{
				PythonPlugin->ForceEnablePythonAtRuntime();
			}
			if (!PythonPlugin->IsPythonAvailable() || !PythonPlugin->IsPythonInitialized())
			{
				OutFailureReason = TEXT("Python support is not available in the current editor session.");
				return false;
			}

			FPythonCommandEx PythonCommand;
			PythonCommand.Command = Command;
			PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
			PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;
			PythonCommand.Flags = EPythonCommandFlags::Unattended;
			const bool bExecuted = PythonPlugin->ExecPythonCommandEx(PythonCommand);

			FString CapturedOutput = PythonCommand.CommandResult;
			for (const FPythonLogOutputEntry& LogEntry : PythonCommand.LogOutput)
			{
				if (!LogEntry.Output.IsEmpty())
				{
					CapturedOutput += TEXT("\n") + LogEntry.Output;
				}
			}

			const FString Begin = TEXT("__UEATELIER_OFFICIAL_JSON_BEGIN__");
			const FString End = TEXT("__UEATELIER_OFFICIAL_JSON_END__");
			const int32 BeginIndex = CapturedOutput.Find(Begin, ESearchCase::CaseSensitive);
			if (BeginIndex == INDEX_NONE)
			{
				OutFailureReason = bExecuted
					? FString(TEXT("Official toolset Python command did not emit a JSON sentinel."))
					: FString(TEXT("Official toolset Python command failed before emitting a JSON sentinel."));
				return false;
			}
			const int32 JsonStart = BeginIndex + Begin.Len();
			const int32 EndIndex = CapturedOutput.Find(End, ESearchCase::CaseSensitive, ESearchDir::FromStart, JsonStart);
			if (EndIndex == INDEX_NONE || EndIndex <= JsonStart)
			{
				OutFailureReason = TEXT("Official toolset Python command emitted an incomplete JSON sentinel.");
				return false;
			}

			const FString ResultJson = CapturedOutput.Mid(JsonStart, EndIndex - JsonStart).TrimStartAndEnd();
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultJson);
			if (!FJsonSerializer::Deserialize(Reader, OutResult) || !OutResult.IsValid())
			{
				OutFailureReason = TEXT("Official toolset Python command sentinel did not contain a JSON object.");
				return false;
			}
			return bExecuted;
		}

		FString TaskAtlasServiceBuildOfficialRegisterCommand(
			const FString& ModuleName,
			const FString& SourcePath,
			const FString& ClassName,
			const FString& ToolsetName)
		{
			return FString::Printf(
				TEXT("import importlib.util, json, sys, traceback\n")
				TEXT("import unreal\n")
				TEXT("_module_name = %s\n")
				TEXT("_source_path = %s\n")
				TEXT("_class_name = %s\n")
				TEXT("_toolset_name = %s\n")
				TEXT("_result = {\"ok\": False}\n")
				TEXT("try:\n")
				TEXT("    _old = sys.modules.get(_module_name)\n")
				TEXT("    if _old is not None and hasattr(_old, \"unregister\"):\n")
				TEXT("        _old.unregister()\n")
				TEXT("    sys.modules.pop(_module_name, None)\n")
				TEXT("    _spec = importlib.util.spec_from_file_location(_module_name, _source_path)\n")
				TEXT("    if _spec is None or _spec.loader is None:\n")
				TEXT("        raise RuntimeError(\"could not create module spec\")\n")
				TEXT("    _module = importlib.util.module_from_spec(_spec)\n")
				TEXT("    sys.modules[_module_name] = _module\n")
				TEXT("    _spec.loader.exec_module(_module)\n")
				TEXT("    _toolset_class = getattr(_module, _class_name, None)\n")
				TEXT("    if _toolset_class is None:\n")
				TEXT("        raise RuntimeError(\"generated toolset class not found\")\n")
				TEXT("    _schema_json = unreal.ToolsetRegistry.get_toolset_json_schema(_toolset_class)\n")
				TEXT("    _schema = json.loads(_schema_json) if _schema_json else {}\n")
				TEXT("    _actual_toolset_name = _schema.get(\"name\") or _toolset_name\n")
				TEXT("    _registered_ok = bool(_module.register())\n")
				TEXT("    _is_registered = bool(unreal.ToolsetRegistry.is_toolset_registered(_actual_toolset_name))\n")
				TEXT("    _result = {\"ok\": bool(_registered_ok and _is_registered), \"registered\": _is_registered, \"module\": _module_name, \"className\": _class_name, \"toolsetName\": _actual_toolset_name, \"expectedToolsetName\": _toolset_name}\n")
				TEXT("except BaseException as exc:\n")
				TEXT("    _result = {\"ok\": False, \"error\": str(exc), \"traceback\": traceback.format_exc(), \"module\": _module_name, \"className\": _class_name, \"toolsetName\": _toolset_name}\n")
				TEXT("print(\"__UEATELIER_OFFICIAL_JSON_BEGIN__\" + json.dumps(_result, ensure_ascii=False, default=str) + \"__UEATELIER_OFFICIAL_JSON_END__\")\n"),
				*TaskAtlasServicePythonStringLiteral(ModuleName),
				*TaskAtlasServicePythonStringLiteral(SourcePath),
				*TaskAtlasServicePythonStringLiteral(ClassName),
				*TaskAtlasServicePythonStringLiteral(ToolsetName));
		}

		FString TaskAtlasServiceBuildOfficialUnregisterCommand(
			const FString& ModuleName,
			const FString& SourcePath,
			const FString& ToolsetName)
		{
			return FString::Printf(
				TEXT("import importlib.util, json, sys, traceback\n")
				TEXT("import unreal\n")
				TEXT("_module_name = %s\n")
				TEXT("_source_path = %s\n")
				TEXT("_toolset_name = %s\n")
				TEXT("_result = {\"ok\": False}\n")
				TEXT("try:\n")
				TEXT("    _module = sys.modules.get(_module_name)\n")
				TEXT("    if _module is None:\n")
				TEXT("        _spec = importlib.util.spec_from_file_location(_module_name, _source_path)\n")
				TEXT("        if _spec is None or _spec.loader is None:\n")
				TEXT("            raise RuntimeError(\"could not create module spec\")\n")
				TEXT("        _module = importlib.util.module_from_spec(_spec)\n")
				TEXT("        sys.modules[_module_name] = _module\n")
				TEXT("        _spec.loader.exec_module(_module)\n")
				TEXT("    if hasattr(_module, \"unregister\"):\n")
				TEXT("        _module.unregister()\n")
				TEXT("    _is_registered = bool(unreal.ToolsetRegistry.is_toolset_registered(_toolset_name))\n")
				TEXT("    sys.modules.pop(_module_name, None)\n")
				TEXT("    _result = {\"ok\": not _is_registered, \"registered\": _is_registered, \"module\": _module_name, \"toolsetName\": _toolset_name}\n")
				TEXT("except BaseException as exc:\n")
				TEXT("    _result = {\"ok\": False, \"error\": str(exc), \"traceback\": traceback.format_exc(), \"module\": _module_name, \"toolsetName\": _toolset_name}\n")
				TEXT("print(\"__UEATELIER_OFFICIAL_JSON_BEGIN__\" + json.dumps(_result, ensure_ascii=False, default=str) + \"__UEATELIER_OFFICIAL_JSON_END__\")\n"),
				*TaskAtlasServicePythonStringLiteral(ModuleName),
				*TaskAtlasServicePythonStringLiteral(SourcePath),
				*TaskAtlasServicePythonStringLiteral(ToolsetName));
		}
#endif

		void TaskAtlasServiceCollectReloadRejections(
			const UserRegistry::FReloadResult& ReloadResult,
			const FString& TargetToolName,
			TArray<FString>& OutRejectedReasons,
			bool& bOutTargetRejected)
		{
			bOutTargetRejected = false;
			for (const UserRegistry::FReloadResult::FRejection& Rejection : ReloadResult.RejectedTools)
			{
				if (Rejection.ToolName == TargetToolName)
				{
					bOutTargetRejected = true;
					OutRejectedReasons.Add(FString::Printf(TEXT("target:%s"), *Rejection.Reason));
				}
				else
				{
					OutRejectedReasons.Add(FString::Printf(TEXT("unrelated:%s:%s"), *Rejection.ToolName, *Rejection.Reason));
				}
			}
		}

		UserRegistry::FReloadResult TaskAtlasServiceReloadUserRegistry(bool bBumpToolsListVersion)
		{
			UserToolLock::FExclusiveGuard Guard;
			UserRegistry::FReloadResult ReloadResult = UserRegistry::ReloadUserToolRegistry(true);
			if (bBumpToolsListVersion)
			{
				BumpUserToolListVersion();
			}
			return ReloadResult;
		}

		bool TaskAtlasServiceIsUserToolLoaded(const FString& ToolName)
		{
			UserToolLock::FSharedGuard Guard;
			return UserRegistry::FindUserTool(ToolName) != nullptr;
		}

		FString TaskAtlasServiceReadReplayStatus(const TSharedPtr<FJsonObject>& ToolJson)
		{
			FString ReplayStatus;
			if (ToolJson.IsValid())
			{
				ToolJson->TryGetStringField(TEXT("replayStatus"), ReplayStatus);
			}
			return ReplayStatus.TrimStartAndEnd();
		}

		bool TaskAtlasServiceReadSmokeArgs(const TSharedPtr<FJsonObject>& ToolJson, TSharedPtr<FJsonObject>& OutSmokeArgs)
		{
			OutSmokeArgs.Reset();
			const TSharedPtr<FJsonObject>* SmokeArgs = nullptr;
			if (!ToolJson.IsValid() || !ToolJson->TryGetObjectField(TEXT("smokeArgs"), SmokeArgs) || !SmokeArgs || !(*SmokeArgs).IsValid())
			{
				return false;
			}
			OutSmokeArgs = MakeShared<FJsonObject>();
			OutSmokeArgs->Values = (*SmokeArgs)->Values;
			return true;
		}

		bool TaskAtlasServiceSmokeResultHasBlockedSteps(const FUnrealMcpExecutionResult& SmokeResult)
		{
			if (SmokeResult.StructuredContent.IsValid())
			{
				bool bHasBlockedSteps = false;
				if (SmokeResult.StructuredContent->TryGetBoolField(TEXT("hasBlockedSteps"), bHasBlockedSteps) && bHasBlockedSteps)
				{
					return true;
				}

				FString Preview;
				if (SmokeResult.StructuredContent->TryGetStringField(TEXT("executionResultPreview"), Preview))
				{
					TSharedPtr<FJsonObject> PreviewObject;
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Preview);
					if (FJsonSerializer::Deserialize(Reader, PreviewObject) && PreviewObject.IsValid())
					{
						if (PreviewObject->TryGetBoolField(TEXT("hasBlockedSteps"), bHasBlockedSteps) && bHasBlockedSteps)
						{
							return true;
						}
					}
				}
			}
			return false;
		}

		bool TaskAtlasServiceSetToolJsonReplayStatus(
			const FResolvedMadeTool& Tool,
			const FString& ReplayStatus,
			const FString& FailureDiagnosticPath,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			TSharedPtr<FJsonObject> ToolJson = Tool.ToolJson;
			if (!ToolJson.IsValid() && !TaskAtlasServiceLoadJsonObject(Tool.ToolJsonPath, ToolJson))
			{
				OutFailureReason = TEXT("failed to reload tool.json");
				return false;
			}
			ToolJson->SetStringField(TEXT("replayStatus"), ReplayStatus);
			if (!FailureDiagnosticPath.IsEmpty())
			{
				ToolJson->SetStringField(TEXT("failureDiagnosticPath"), FailureDiagnosticPath);
			}
			return TaskAtlasServiceWriteJsonObject(ToolJson, Tool.ToolJsonPath);
		}

		bool TaskAtlasServiceWriteFailureMarker(const FResolvedMadeTool& Tool, const FString& Reason)
		{
			const FString MarkerPath = FPaths::Combine(Tool.TargetDir, TEXT("_failure.marker"));
			const FString MarkerText = FString::Printf(
				TEXT("timestampUtc=%s\nreason=%s\n"),
				*FDateTime::UtcNow().ToIso8601(),
				*Reason);
			return FFileHelper::SaveStringToFile(MarkerText, *MarkerPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString TaskAtlasServiceMarkdownValue(FString Value)
		{
			Value.ReplaceInline(TEXT("`"), TEXT("'"));
			return FString::Printf(TEXT("`%s`"), *Value);
		}

		void TaskAtlasServiceAppendMetadataLine(FString& Markdown, const FString& Key, const FString& Value, bool bSkipEmpty = true)
		{
			if (bSkipEmpty && Value.TrimStartAndEnd().IsEmpty())
			{
				return;
			}
			Markdown += FString::Printf(TEXT("- %s: %s\n"), *Key, *TaskAtlasServiceMarkdownValue(Value));
		}

		void TaskAtlasServiceAppendTextSection(FString& Markdown, const FString& Heading, const FString& Text)
		{
			if (Text.TrimStartAndEnd().IsEmpty())
			{
				return;
			}
			Markdown += FString::Printf(TEXT("## %s\n\n%s\n\n"), *Heading, *Text.TrimStartAndEnd());
		}

		FString TaskAtlasServiceBuildTaskKnowledgeMarkdown(const FLoadedTask& Task)
		{
			// Duplicated from STaskAtlasWindow.cpp; chunk 6 will consolidate the UI/service copy.
			FString Markdown;
			Markdown += TEXT("# Task Atlas Workflow\n\n");
			Markdown += TEXT("## Metadata\n\n");
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("taskId"), Task.Model.TaskId, false);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("label"), Task.Label);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("rating"), Task.Rating, false);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("pinned"), Task.bPinned ? FString(TEXT("true")) : FString(TEXT("false")), false);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("sessionId"), Task.SessionId, false);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("tStartUtc"), Task.TStartUtc, false);
			TaskAtlasServiceAppendMetadataLine(Markdown, TEXT("tEndUtc"), Task.TEndUtc, false);
			Markdown += TEXT("\n");

			TaskAtlasServiceAppendTextSection(Markdown, TEXT("User Intent"), Task.UserIntent);
			TaskAtlasServiceAppendTextSection(Markdown, TEXT("AI Summary"), Task.Description);

			Markdown += TEXT("## Critical Path Tools\n\n");
			if (Task.Model.CriticalPath.Num() == 0)
			{
				Markdown += TEXT("- (none)\n\n");
			}
			else
			{
				for (const FString& ToolName : Task.Model.CriticalPath)
				{
					Markdown += FString::Printf(TEXT("- %s\n"), *TaskAtlasServiceMarkdownValue(ToolName));
				}
				Markdown += TEXT("\n");
			}

			Markdown += TEXT("## Step References\n\n");
			if (Task.Model.StepRefs.Num() == 0)
			{
				Markdown += TEXT("- (none)\n\n");
			}
			else
			{
				for (int32 Index = 0; Index < Task.Model.StepRefs.Num(); ++Index)
				{
					const FTaskAtlasStepRef& Step = Task.Model.StepRefs[Index];
					Markdown += FString::Printf(
						TEXT("- step %d: tool=%s, eventId=%s, captureStatus=%s\n"),
						Index,
						*TaskAtlasServiceMarkdownValue(Step.ToolName),
						*TaskAtlasServiceMarkdownValue(Step.EventId),
						*TaskAtlasServiceMarkdownValue(Step.CaptureStatus));
				}
				Markdown += TEXT("\n");
			}
			return Markdown;
		}

		bool TaskAtlasServiceBuildTaskAtlasKnowledgeSourcePath(
			const FString& TaskId,
			bool bEnsureDirectory,
			FString& OutPath,
			FString& OutFailureReason)
		{
			OutPath.Reset();
			OutFailureReason.Reset();
			const FString SourceRoot = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(
				TaskAtlasServiceSavedRootDir(),
				TEXT("KnowledgeSources/TaskAtlas")));
			if (bEnsureDirectory && !IFileManager::Get().MakeDirectory(*SourceRoot, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create Task Atlas knowledge source directory: %s"), *SourceRoot);
				return false;
			}

			const FString SafeName = TaskAtlasServiceSanitizeTaskToken(TaskId);
			const FString SourcePath = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(SourceRoot, SafeName + TEXT(".md")));
			if (!TaskAtlasServicePathEqualsOrChild(SourcePath, SourceRoot))
			{
				OutFailureReason = TEXT("Refused to write Task Atlas knowledge source outside Saved/UnrealMcp/KnowledgeSources/TaskAtlas.");
				return false;
			}

			OutPath = SourcePath;
			return true;
		}
	}

	FString SanitizeToolIdPart(const FString& In)
	{
		FString Slug;
		for (const TCHAR Ch : In)
		{
			if (Ch >= 'A' && Ch <= 'Z')
			{
				Slug.AppendChar(static_cast<TCHAR>(Ch + ('a' - 'A')));
			}
			else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
			{
				Slug.AppendChar(Ch);
			}
			else
			{
				TaskAtlasServiceAppendNormalizedUnderscore(Slug);
			}
		}
		return TaskAtlasServiceTrimUnderscores(Slug);
	}

	FString MakeAtlasToolId(const FString& TaskLabel, const FString& TaskId)
	{
		FString ToolId = SanitizeToolIdPart(TaskLabel);
		if (ToolId.IsEmpty())
		{
			ToolId = SanitizeToolIdPart(TaskId);
		}
		if (ToolId.IsEmpty())
		{
			ToolId = TEXT("workflow");
		}
		ToolId = FString::Printf(TEXT("atlas_%s"), *ToolId);
		if (ToolId.Len() > 64)
		{
			ToolId = TaskAtlasServiceTrimUnderscores(ToolId.Left(64));
		}
		return ToolId.IsEmpty() ? FString(TEXT("atlas_workflow")) : ToolId;
	}

	FEligibilityResult ClassifyTask(const FTaskAtlasModel& Task)
	{
		FEligibilityResult Result;

		if (Task.StepRefs.Num() > 0)
		{
			Result.Steps.Reserve(Task.StepRefs.Num());
			for (int32 Index = 0; Index < Task.StepRefs.Num(); ++Index)
			{
				const FTaskAtlasStepRef& Ref = Task.StepRefs[Index];
				FStepPolicy Step;
				Step.Ordinal = Index;
				Step.ToolName = Ref.ToolName.TrimStartAndEnd();
				Step.CaptureEventId = Ref.EventId.TrimStartAndEnd();
				Step.CaptureSummary = TaskAtlasServiceCaptureSummary(Ref.CaptureStatus, Ref.CaptureSummary);
				Step.bHasCapturedArgs = TaskAtlasServiceHasCapturedArgs(Ref.CaptureStatus);

				const FCallToolPolicyResult PolicyResult = ClassifyCallToolTarget_Pure(TaskAtlasServiceMakePolicyFacts(Step.ToolName));
				Step.PolicyDecision = TaskAtlasServiceDecisionToString(PolicyResult.Decision);
				Step.bForceDryRun = PolicyResult.Decision == ECallToolDecision::ForceDryRun;
				if (PolicyResult.Decision == ECallToolDecision::Deny)
				{
					Step.DenyReason = PolicyResult.Reason.IsEmpty() ? TEXT("deny") : PolicyResult.Reason;
				}
				Result.Steps.Add(MoveTemp(Step));
			}
		}
		else
		{
			Result.Steps.Reserve(Task.CriticalPath.Num());
			for (int32 Index = 0; Index < Task.CriticalPath.Num(); ++Index)
			{
				FStepPolicy Step;
				Step.Ordinal = Index;
				Step.ToolName = Task.CriticalPath[Index].TrimStartAndEnd();
				Step.CaptureSummary = TEXT("missing");

				const FCallToolPolicyResult PolicyResult = ClassifyCallToolTarget_Pure(TaskAtlasServiceMakePolicyFacts(Step.ToolName));
				Step.PolicyDecision = TaskAtlasServiceDecisionToString(PolicyResult.Decision);
				Step.bForceDryRun = PolicyResult.Decision == ECallToolDecision::ForceDryRun;
				if (PolicyResult.Decision == ECallToolDecision::Deny)
				{
					Step.DenyReason = PolicyResult.Reason.IsEmpty() ? TEXT("deny") : PolicyResult.Reason;
				}
				Result.Steps.Add(MoveTemp(Step));
			}
		}

		Result.TotalStepCount = Result.Steps.Num();
		for (const FStepPolicy& Step : Result.Steps)
		{
			if (Step.PolicyDecision == TEXT("allow"))
			{
				++Result.AllowCount;
			}
			else if (Step.PolicyDecision == TEXT("force_dry_run"))
			{
				++Result.ForceDryRunCount;
			}
			else if (Step.PolicyDecision == TEXT("deny"))
			{
				// Skip not_visible denies: these are external-client lookup misses
				// (e.g. bare 'task_list' w/o 'unreal.' prefix) and represent
				// protocol noise, not user-meaningful policy denial. Other deny
				// reasons (user_tool_forbidden, workflow_run_forbidden,
				// dangerous_no_dryrun) still count.
				if (Step.DenyReason != TEXT("not_visible"))
				{
					++Result.DenyCount;
					if (Result.BlockedFirstStep < 0)
					{
						Result.BlockedFirstStep = Step.Ordinal;
						Result.BlockedFirstReason = Step.DenyReason;
					}
				}
			}

			if (Step.bHasCapturedArgs)
			{
				++Result.CapturedArgsCount;
			}
		}

		if (Result.TotalStepCount == 0)
		{
			Result.Eligibility = EEligibility::SkeletonPreCapture;
		}
		else if (Result.DenyCount > 0)
		{
			Result.Eligibility = EEligibility::Blocked;
		}
		else if (Result.CapturedArgsCount == Result.TotalStepCount)
		{
			Result.Eligibility = EEligibility::PreviewReady;
		}
		else if (Result.CapturedArgsCount > 0)
		{
			Result.Eligibility = EEligibility::Partial;
		}
		else
		{
			Result.Eligibility = EEligibility::SkeletonPreCapture;
		}
		return Result;
	}

	FMakeCompositeResult MakeComposite(const FMakeCompositeRequest& Req)
	{
		check(IsInGameThread());
		FMakeCompositeResult Result;

		FLoadedTask Task;
		FString FailureReason;
		if (!TaskAtlasServiceLoadTaskById(Req.TaskId, Task, FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("task_not_found");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		Result.Eligibility = ClassifyTask(Task.Model);
		Result.ReplayStatus = TaskAtlasServiceEligibilityToString(Result.Eligibility.Eligibility);
		Result.CompositeKind = Result.ReplayStatus;

		if (Result.Eligibility.Eligibility == EEligibility::Blocked && !Req.bForceWriteEvenIfBlocked)
		{
			if (TaskAtlasServiceWriteDocumentDraft(Task, Result.Eligibility, TEXT("blocked"), Result.DocumentPath, FailureReason))
			{
				Result.Outcome = EMakeCompositeOutcome::Blocked;
				Result.CompositeKind = TEXT("blocked");
				Result.ReplayStatus = TEXT("blocked");
			}
			else
			{
				Result.Outcome = EMakeCompositeOutcome::StagingFailed;
				Result.ErrorCode = TEXT("document_write_failed");
				Result.ErrorMessage = FailureReason;
			}
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (Req.bPreferDocumentOnly
			|| Result.Eligibility.Eligibility == EEligibility::Partial
			|| Result.Eligibility.Eligibility == EEligibility::SkeletonPreCapture)
		{
			const FString Kind = TaskAtlasServiceEligibilityToString(Result.Eligibility.Eligibility);
			if (TaskAtlasServiceWriteDocumentDraft(Task, Result.Eligibility, Kind, Result.DocumentPath, FailureReason))
			{
				Result.Outcome = EMakeCompositeOutcome::DocumentOnly;
				Result.CompositeKind = Kind;
				Result.ReplayStatus = Kind;
			}
			else
			{
				Result.Outcome = EMakeCompositeOutcome::StagingFailed;
				Result.ErrorCode = TEXT("document_write_failed");
				Result.ErrorMessage = FailureReason;
			}
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString ToolId = MakeAtlasToolId(Task.Label, Task.Model.TaskId);
		const FString NewToolName = FString::Printf(TEXT("user.%s"), *ToolId);
		Result.ToolName = NewToolName;
		Result.CompositeKind = TEXT("preview");
		Result.ReplayStatus = TEXT("preview_ready");

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		const FString PyToolsRoot = TaskAtlasServiceMadeToolsRootDir();
		const FString CanonicalPyToolsRoot = TaskAtlasServiceNormalizeAbsolutePath(PyToolsRoot);
		const FString TargetDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(CanonicalPyToolsRoot, ToolId));
		if (!TaskAtlasServicePathEqualsOrChild(TargetDir, CanonicalPyToolsRoot))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("invalid_name");
			Result.ErrorMessage = TEXT("Refused to write composite outside the user-tool registry root.");
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (IFileManager::Get().DirectoryExists(*TargetDir))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("collision_existing_target");
			Result.ErrorMessage = FString::Printf(TEXT("Composite target directory already exists: %s"), *TargetDir);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString StagingDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(
			CanonicalPyToolsRoot,
			FString::Printf(TEXT("__staging_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short))));
		Result.StagingDir = StagingDir;

		FString BuiltToolName;
		FString MainPy;
		FString MainPySha256;
		FString ToolJson;
		TSharedPtr<FJsonObject> SmokeArgs;
		TArray<FString> StepTools;
		const FString CreatedUtc = FDateTime::UtcNow().ToIso8601();
		const TArray<TSharedPtr<FJsonValue>> StepRefValues = TaskAtlasServiceMakeStepRefValues(Task.Model, Result.Eligibility);
		if (!TaskAtlasComposite::BuildCompositeUserToolFiles(
			ToolId,
			Task.Label.IsEmpty() ? ToolId : Task.Label,
			Task.Description,
			Task.Model.TaskId,
			TEXT("preview_ready"),
			FString(),
			Task.Model.CriticalPath,
			StepRefValues,
			TaskAtlasServiceVisibleCoreToolNames(),
			BuiltToolName,
			MainPy,
			MainPySha256,
			ToolJson,
			SmokeArgs,
			StepTools,
			FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("composite_build_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (BuiltToolName != NewToolName)
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("generated_name_mismatch");
			Result.ErrorMessage = TEXT("Generated composite tool name did not match the derived target name.");
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}
		(void)SmokeArgs;
		(void)StepTools;

		if (!TaskAtlasServiceNormalizeCompositeMetadata(NewToolName, MainPySha256, Task, CreatedUtc, ToolJson, FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("metadata_normalization_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString Readme = TaskAtlasServiceBuildReadme(Task, NewToolName, Result.Eligibility);
		if (!TaskAtlasServiceWriteStagingFiles(StagingDir, MainPy, ToolJson, Readme, FailureReason))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("staging_write_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceCorruptNextStagingShaForTests)
		{
			bTaskAtlasServiceCorruptNextStagingShaForTests = false;
			TSharedPtr<FJsonObject> CorruptToolJson;
			if (TaskAtlasServiceLoadJsonObject(FPaths::Combine(StagingDir, TEXT("tool.json")), CorruptToolJson))
			{
				CorruptToolJson->SetStringField(TEXT("pythonHandlerSha256"), FString::ChrN(64, TCHAR('0')));
				TaskAtlasServiceWriteJsonObject(CorruptToolJson, FPaths::Combine(StagingDir, TEXT("tool.json")));
			}
		}
#endif

		TSharedPtr<FJsonObject> ValidationDiagnostic;
		FString ValidationErrorCode;
		FString ValidationErrorMessage;
		if (!TaskAtlasServiceValidateStaging(StagingDir, NewToolName, MainPySha256, ValidationDiagnostic, ValidationErrorCode, ValidationErrorMessage))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = ValidationErrorCode.IsEmpty() ? FString(TEXT("staging_validation_failed")) : ValidationErrorCode;
			Result.ErrorMessage = ValidationErrorMessage;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			if (Result.StructuredContent.IsValid())
			{
				Result.StructuredContent->SetObjectField(TEXT("diagnostic"), ValidationDiagnostic.IsValid() ? ValidationDiagnostic : MakeShared<FJsonObject>());
			}
			return Result;
		}

		if (!IFileManager::Get().Move(*TargetDir, *StagingDir, false, true))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("staging_rename_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to rename staging directory '%s' to '%s'."), *StagingDir, *TargetDir);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		UserRegistry::FReloadResult ReloadResult = TaskAtlasServiceReloadUserRegistry(true);

#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceRejectNextTargetReloadForTests)
		{
			bTaskAtlasServiceRejectNextTargetReloadForTests = false;
			UserRegistry::FReloadResult::FRejection Rejection;
			Rejection.ToolName = NewToolName;
			Rejection.Reason = TEXT("test_forced_reload_rejection");
			ReloadResult.RejectedTools.Add(MoveTemp(Rejection));
		}
#endif

		bool bTargetRejected = false;
		for (const UserRegistry::FReloadResult::FRejection& Rejection : ReloadResult.RejectedTools)
		{
			if (Rejection.ToolName == NewToolName)
			{
				bTargetRejected = true;
				Result.RejectedReasons.Add(Rejection.Reason);
			}
			else
			{
				Result.RejectedReasons.Add(FString::Printf(TEXT("unrelated:%s:%s"), *Rejection.ToolName, *Rejection.Reason));
			}
		}

		if (bTargetRejected)
		{
			IFileManager::Get().DeleteDirectory(*TargetDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::ReloadRejected;
			Result.GeneratedDir = TargetDir;
			Result.StagingDir = StagingDir;
			Result.ErrorCode = TEXT("reload_rejected");
			Result.ErrorMessage = TEXT("Generated composite was rejected by the user tool registry reload and has been rolled back.");
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteReloadRejectedDiagnostic(
				NewToolName,
				Task.Model.TaskId,
				TEXT("preview_ready"),
				Result.RejectedReasons,
				StagingDir,
				TargetDir,
				ReloadResult.BeforeCount,
				ReloadResult.AfterCount,
				CreatedUtc);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			if (Result.StructuredContent.IsValid())
			{
				Result.StructuredContent->SetNumberField(TEXT("reloadBeforeCount"), ReloadResult.BeforeCount);
				Result.StructuredContent->SetNumberField(TEXT("reloadAfterCount"), ReloadResult.AfterCount);
			}
			return Result;
		}

		Result.Outcome = EMakeCompositeOutcome::CompositeWritten;
		Result.GeneratedDir = TargetDir;
		Result.StagingDir.Reset();
		Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->SetNumberField(TEXT("reloadBeforeCount"), ReloadResult.BeforeCount);
			Result.StructuredContent->SetNumberField(TEXT("reloadAfterCount"), ReloadResult.AfterCount);
		}
		return Result;
	}

	FDeleteMadeToolResult DeleteMadeTool(const FString& ToolName)
	{
		check(IsInGameThread());
		FDeleteMadeToolResult Result;
		Result.ToolName = ToolName.TrimStartAndEnd();

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		FResolvedMadeTool Tool;
		FString ErrorCode;
		FString ErrorMessage;
		if (!TaskAtlasServiceResolveMadeTool(Result.ToolName, false, Tool, ErrorCode, ErrorMessage))
		{
			Result.Outcome = ErrorCode == TEXT("not_found") ? EDeleteMadeToolOutcome::NotFound : EDeleteMadeToolOutcome::Refused;
			Result.ErrorCode = ErrorCode;
			Result.ErrorMessage = ErrorMessage;
			Result.StructuredContent = TaskAtlasServiceMakeDeleteResultContent(Result);
			return Result;
		}
		Result.ToolName = Tool.ToolName;

		const bool bLoadedBefore = TaskAtlasServiceIsUserToolLoaded(Tool.ToolName);
		if (!IFileManager::Get().DirectoryExists(*Tool.TargetDir))
		{
			if (bLoadedBefore)
			{
				Result.bWasStaleEntry = true;
				const UserRegistry::FReloadResult ReloadResult = TaskAtlasServiceReloadUserRegistry(true);
				Result.ReloadBeforeCount = ReloadResult.BeforeCount;
				Result.ReloadAfterCount = ReloadResult.AfterCount;
				Result.ReloadRemovedCount = ReloadResult.RemovedTools.Num();
				bool bTargetRejected = false;
				TaskAtlasServiceCollectReloadRejections(ReloadResult, Tool.ToolName, Result.RejectedReasons, bTargetRejected);
				(void)bTargetRejected;
			}
			Result.Outcome = Result.RejectedReasons.Num() > 0 ? EDeleteMadeToolOutcome::ReloadRejected : EDeleteMadeToolOutcome::NotFound;
			Result.ErrorCode = TEXT("not_found");
			Result.ErrorMessage = TEXT("Generated user tool directory was already absent.");
			Result.StructuredContent = TaskAtlasServiceMakeDeleteResultContent(Result);
			return Result;
		}

		if (!bLoadedBefore)
		{
			Result.bWasStaleEntry = true;
		}

		if (!IFileManager::Get().DeleteDirectory(*Tool.TargetDir, false, true))
		{
			Result.Outcome = EDeleteMadeToolOutcome::Failed;
			Result.ErrorCode = TEXT("filesystem_delete_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to delete generated user tool directory: %s"), *Tool.TargetDir);
			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolName"), Tool.ToolName);
			Diagnostic->SetStringField(TEXT("targetDir"), Tool.TargetDir);
			Diagnostic->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Diagnostic->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteOperationDiagnostic(TEXT("delete"), Tool.ToolId, Diagnostic);
			Result.StructuredContent = TaskAtlasServiceMakeDeleteResultContent(Result);
			return Result;
		}

		Result.RemovedDir = Tool.TargetDir;
		const UserRegistry::FReloadResult ReloadResult = TaskAtlasServiceReloadUserRegistry(true);
		Result.ReloadBeforeCount = ReloadResult.BeforeCount;
		Result.ReloadAfterCount = ReloadResult.AfterCount;
		Result.ReloadRemovedCount = ReloadResult.RemovedTools.Num();
		bool bTargetRejected = false;
		TaskAtlasServiceCollectReloadRejections(ReloadResult, Tool.ToolName, Result.RejectedReasons, bTargetRejected);

		if (Result.RejectedReasons.Num() > 0)
		{
			Result.Outcome = EDeleteMadeToolOutcome::ReloadRejected;
			Result.ErrorCode = bTargetRejected ? FString(TEXT("reload_rejected_deleted_target")) : FString(TEXT("reload_rejected"));
			Result.ErrorMessage = bTargetRejected
				? FString(TEXT("Registry reload rejected the deleted target tool unexpectedly."))
				: FString(TEXT("Registry reload reported unrelated rejected user tools after deletion."));
			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolName"), Tool.ToolName);
			Diagnostic->SetStringField(TEXT("removedDir"), Result.RemovedDir);
			Diagnostic->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(Result.RejectedReasons));
			Diagnostic->SetNumberField(TEXT("reloadBeforeCount"), ReloadResult.BeforeCount);
			Diagnostic->SetNumberField(TEXT("reloadAfterCount"), ReloadResult.AfterCount);
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteOperationDiagnostic(TEXT("delete"), Tool.ToolId, Diagnostic);
		}
		else
		{
			Result.Outcome = EDeleteMadeToolOutcome::Deleted;
		}

		Result.StructuredContent = TaskAtlasServiceMakeDeleteResultContent(Result);
		return Result;
	}

	TArray<FMadeToolEntry> ListMadeTools()
	{
		TArray<FMadeToolEntry> Entries;
		const FString RootDir = TaskAtlasServiceMadeToolsRootDir();
		if (!IFileManager::Get().DirectoryExists(*RootDir))
		{
			return Entries;
		}

		const FString CanonicalRootDir = TaskAtlasServiceCanonicalExistingPath(RootDir);
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(RootDir, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (!TaskAtlasServiceIsSafeDirectoryName(DirectoryName))
			{
				continue;
			}

			const FString CandidateDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(RootDir, DirectoryName));
			if (TaskAtlasServiceIsSymlink(CandidateDir))
			{
				continue;
			}

			const FString CanonicalCandidateDir = TaskAtlasServiceCanonicalExistingPath(CandidateDir);
			if (!TaskAtlasServicePathEqualsOrChild(CanonicalCandidateDir, CanonicalRootDir))
			{
				continue;
			}

			const FString ToolJsonPath = FPaths::Combine(CanonicalCandidateDir, TEXT("tool.json"));
			if (!FPaths::FileExists(ToolJsonPath) || TaskAtlasServiceIsSymlink(ToolJsonPath))
			{
				continue;
			}

			TSharedPtr<FJsonObject> ToolJson;
			if (!TaskAtlasServiceLoadJsonObject(ToolJsonPath, ToolJson))
			{
				continue;
			}

			FString Generator;
			if (!TaskAtlasServiceReadRequiredString(ToolJson, TEXT("generator"), Generator)
				|| Generator != TEXT("task_atlas_make_composite"))
			{
				continue;
			}

			FMadeToolEntry Entry;
			if (!TaskAtlasServiceReadRequiredString(ToolJson, TEXT("name"), Entry.ToolName)
				|| !Entry.ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("compositeKind"), Entry.CompositeKind)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("replayStatus"), Entry.ReplayStatus)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("sourceTaskId"), Entry.SourceTaskId)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("pythonHandlerSha256"), Entry.PythonHandlerSha256))
			{
				continue;
			}

			Entry.ScaffoldDir = CanonicalCandidateDir;
			Entry.Generator = Generator;
			Entry.CreatedUtc = TaskAtlasServiceReadCreatedUtc(ToolJson, ToolJsonPath);
			Entry.bHasFailureMarker = TaskAtlasServiceHasFailureMarker(CanonicalCandidateDir);
			Entry.FailureDiagnosticPath = TaskAtlasServiceReadFailureDiagnosticPath(ToolJson);
			{
				UserToolLock::FSharedGuard Guard;
				Entry.bLoadedInUserRegistry = UserRegistry::FindUserTool(Entry.ToolName) != nullptr;
			}
			Entries.Add(MoveTemp(Entry));
		}

		Entries.Sort(
			[](const FMadeToolEntry& Left, const FMadeToolEntry& Right)
			{
				if (Left.CreatedUtc == Right.CreatedUtc)
				{
					return Left.ToolName < Right.ToolName;
				}
				return Left.CreatedUtc > Right.CreatedUtc;
			});
		return Entries;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void SetMadeToolsRootDirForTests(const FString& RootDir)
	{
		GTaskAtlasServiceMadeToolsRootForTests = RootDir;
	}

	void ClearMadeToolsRootDirForTests()
	{
		GTaskAtlasServiceMadeToolsRootForTests.Reset();
	}

	void SetSavedRootDirForTests(const FString& RootDir)
	{
		GTaskAtlasServiceSavedRootForTests = RootDir;
	}

	void ClearSavedRootDirForTests()
	{
		GTaskAtlasServiceSavedRootForTests.Reset();
	}

	void CorruptNextMakeCompositeStagingShaForTests()
	{
		bTaskAtlasServiceCorruptNextStagingShaForTests = true;
	}

	void RejectNextMakeCompositeReloadForTests()
	{
		bTaskAtlasServiceRejectNextTargetReloadForTests = true;
	}

	void FailNextPromoteRefreshForTests()
	{
		bTaskAtlasServiceFailNextKnowledgeRefreshForTests = true;
	}
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	void SetOfficialCppBuildEditorRunningForTests(bool bIsRunning)
	{
		GTaskAtlasServiceOfficialCppEditorRunningForTests = bIsRunning;
	}

	void ClearOfficialCppBuildEditorRunningForTests()
	{
		GTaskAtlasServiceOfficialCppEditorRunningForTests.Reset();
	}

	void SetOfficialCppBuildExampleRootForTests(const FString& ExampleRootDir)
	{
		GTaskAtlasServiceOfficialCppExampleRootForTests = ExampleRootDir;
	}

	void ClearOfficialCppBuildExampleRootForTests()
	{
		GTaskAtlasServiceOfficialCppExampleRootForTests.Reset();
	}

	void FailNextOfficialCppBuildAfterCopyForTests()
	{
		bTaskAtlasServiceFailNextOfficialCppBuildAfterCopyForTests = true;
	}
#endif
#endif

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	FString OfficialToolsetDraftsRootDir()
	{
		return TaskAtlasServiceOfficialDraftsRootDir();
	}

	FOfficialCppToolsetBuildDraftResult BuildOfficialCppToolsetDraft(const FString& ToolId)
	{
		check(IsInGameThread());
		FOfficialCppToolsetBuildDraftResult Result;
		Result.ToolId = ToolId.TrimStartAndEnd();
		Result.bRestartRequired = true;

		if (TaskAtlasServiceIsOfficialCppEditorRunning())
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("buildBlockedByOpenEditor"),
				TEXT("editor_open"),
				TEXT("UnrealEditor is running; official C++ draft builds require the editor to be closed first."),
				TEXT("Close every UnrealEditor process, then run Build draft (UE5.8) again. This action never kills the editor automatically."));
			return Result;
		}

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		const FString DraftRoot = TaskAtlasServiceOfficialDraftsRootDir();
		const FString CanonicalDraftRoot = TaskAtlasServiceNormalizeAbsolutePath(DraftRoot);
		Result.DraftDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(CanonicalDraftRoot, Result.ToolId));
		if (!TaskAtlasServiceIsSafeDirectoryName(Result.ToolId) || !TaskAtlasServicePathEqualsOrChild(Result.DraftDir, CanonicalDraftRoot))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("invalid_name"),
				TEXT("Refused to build official C++ toolset draft outside Saved/UnrealMcp/OfficialToolsetDrafts."));
			return Result;
		}
		if (!IFileManager::Get().DirectoryExists(*Result.DraftDir))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("draft_not_found"),
				FString::Printf(TEXT("Official C++ toolset draft not found: %s"), *Result.DraftDir));
			return Result;
		}

		Result.ManifestPath = FPaths::Combine(Result.DraftDir, TEXT("manifest.json"));
		TSharedPtr<FJsonObject> Manifest;
		if (!TaskAtlasServiceLoadJsonObject(Result.ManifestPath, Manifest) || !Manifest.IsValid())
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("manifest_missing"),
				FString::Printf(TEXT("Official C++ toolset draft manifest missing or invalid: %s"), *Result.ManifestPath));
			return Result;
		}

		FString Variant;
		Manifest->TryGetStringField(TEXT("variant"), Variant);
		if (Variant != TEXT("cpp"))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("not_cpp_variant"),
				TEXT("Build draft (UE5.8) only accepts officialVariant=cpp drafts."));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}

		if (!Manifest->TryGetStringField(TEXT("pluginName"), Result.PluginName) || Result.PluginName.TrimStartAndEnd().IsEmpty())
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("manifest_missing_plugin_name"),
				TEXT("Official C++ toolset draft manifest is missing pluginName."));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}
		Result.PluginName = Result.PluginName.TrimStartAndEnd();
		if (!TaskAtlasServiceIsSafeDirectoryName(Result.PluginName))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("unsafe_plugin_name"),
				TEXT("Official C++ toolset draft pluginName is not a safe directory name."));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}

		Result.PluginSourceDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(Result.DraftDir, TEXT("cpp"), Result.PluginName));
		if (!TaskAtlasServicePathEqualsOrChild(Result.PluginSourceDir, Result.DraftDir)
			|| !IFileManager::Get().DirectoryExists(*Result.PluginSourceDir))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("plugin_source_missing"),
				FString::Printf(TEXT("Official C++ toolset draft plugin source directory missing: %s"), *Result.PluginSourceDir));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}

		FString ExampleRoot;
		FString ResolveFailure;
		if (!TaskAtlasServiceResolveOfficialCppExample58Project(ExampleRoot, Result.ProjectFilePath, ResolveFailure))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("example58_missing"),
				ResolveFailure);
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}

		const FString PluginsRoot = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(ExampleRoot, TEXT("Plugins")));
		Result.MountedPluginDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(PluginsRoot, Result.PluginName));
		if (!TaskAtlasServicePathEqualsOrChild(Result.MountedPluginDir, PluginsRoot))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("mount_path_unsafe"),
				TEXT("Refused to mount official C++ toolset draft outside Examples/UEvolveExample58/Plugins."));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}
		if (IFileManager::Get().DirectoryExists(*Result.MountedPluginDir))
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("mount_exists"),
				FString::Printf(TEXT("Temporary official C++ build mount already exists and will not be overwritten: %s"), *Result.MountedPluginDir));
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		}

		const FString LogName = FString::Printf(
			TEXT("%s-%s.log"),
			*TaskAtlasServiceMakeSafeTimestamp(FDateTime::UtcNow().ToIso8601()),
			*Result.ToolId);
		Result.BuildLogPath = FPaths::Combine(TaskAtlasServiceOfficialCppBuildLogsRootDir(), LogName);
		Result.BuildScriptPath = TaskAtlasServiceBuildScriptPath();
		const FString MountedPluginDescriptor = FPaths::Combine(Result.MountedPluginDir, Result.PluginName + TEXT(".uplugin"));

		bool bCleanupArmed = true;
		auto CleanupMountedCopy = [&Result]()
		{
			if (IFileManager::Get().DirectoryExists(*Result.MountedPluginDir))
			{
				IFileManager::Get().DeleteDirectory(*Result.MountedPluginDir, false, true);
			}
			Result.bMountedCopyRemoved = !IFileManager::Get().DirectoryExists(*Result.MountedPluginDir);
			if (!Result.bMountedCopyRemoved)
			{
				Result.bCleanupFailed = true;
				Result.CleanupErrorMessage = FString::Printf(TEXT("Failed to remove temporary official C++ build mount: %s"), *Result.MountedPluginDir);
			}
		};
		ON_SCOPE_EXIT
		{
			if (bCleanupArmed)
			{
				CleanupMountedCopy();
			}
		};
		auto FinalizeMountedBuildResult = [&]()
		{
			CleanupMountedCopy();
			bCleanupArmed = false;
			TaskAtlasServiceUpdateOfficialCppBuildManifest(Result);
			return Result;
		};

		const double StartSeconds = FPlatformTime::Seconds();
		IFileManager::Get().MakeDirectory(*PluginsRoot, true);
		if (!FPlatformFileManager::Get().GetPlatformFile().CopyDirectoryTree(*Result.MountedPluginDir, *Result.PluginSourceDir, true))
		{
			Result.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("mount_copy_failed"),
				FString::Printf(TEXT("Failed to copy official C++ toolset draft to temporary build mount: %s"), *Result.MountedPluginDir));
			return FinalizeMountedBuildResult();
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceFailNextOfficialCppBuildAfterCopyForTests)
		{
			bTaskAtlasServiceFailNextOfficialCppBuildAfterCopyForTests = false;
			Result.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			FFileHelper::SaveStringToFile(
				TEXT("Forced official C++ build failure after copy for automation.\n"),
				*Result.BuildLogPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("forced_build_failure_for_tests"),
				TEXT("Forced official C++ build failure after copy for automation."));
			return FinalizeMountedBuildResult();
		}
#endif

		if (!FPaths::FileExists(Result.BuildScriptPath))
		{
			Result.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("build_script_missing"),
				FString::Printf(TEXT("UE build script not found: %s"), *Result.BuildScriptPath));
			return FinalizeMountedBuildResult();
		}

		const FString Params = FString::Printf(
			TEXT("MyProjectEditor %s Development -Project=%s -WaitMutex -plugin=%s"),
			*TaskAtlasServiceHostPlatformName(),
			*TaskAtlasServiceQuoteCommandLineArgument(Result.ProjectFilePath),
			*TaskAtlasServiceQuoteCommandLineArgument(MountedPluginDescriptor));
		FString StdOut;
		FString StdErr;
		const bool bLaunched = FPlatformProcess::ExecProcess(
			*Result.BuildScriptPath,
			*Params,
			&Result.ReturnCode,
			&StdOut,
			&StdErr,
			*FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
		Result.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;

		const FString CombinedLog = StdOut + (StdErr.IsEmpty() ? FString() : FString::Printf(TEXT("\n\n[stderr]\n%s"), *StdErr));
		FFileHelper::SaveStringToFile(CombinedLog, *Result.BuildLogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		if (!bLaunched)
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("build_launch_failed"),
				TEXT("Failed to launch UE5.8 UBT build for official C++ toolset draft."));
			return FinalizeMountedBuildResult();
		}

		if (Result.ReturnCode != 0)
		{
			TaskAtlasServiceSetOfficialCppBuildStatus(
				Result,
				TEXT("failed"),
				TEXT("build_failed"),
				FString::Printf(TEXT("UE5.8 UBT build failed for official C++ toolset draft with exit code %d."), Result.ReturnCode));
			return FinalizeMountedBuildResult();
		}

		TaskAtlasServiceSetOfficialCppBuildStatus(Result, TEXT("succeeded"), FString(), FString());
		return FinalizeMountedBuildResult();
	}

	FOfficialToolsetDraftResult GenerateOfficialToolsetDraft(const FOfficialToolsetDraftRequest& Req)
	{
		check(IsInGameThread());
		FOfficialToolsetDraftResult Result;
		const FString ToolId = Req.ToolId.TrimStartAndEnd().IsEmpty()
			? MakeAtlasToolId(Req.Title, Req.TaskId)
			: Req.ToolId.TrimStartAndEnd();

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		const FString DraftRoot = TaskAtlasServiceOfficialDraftsRootDir();
		const FString CanonicalDraftRoot = TaskAtlasServiceNormalizeAbsolutePath(DraftRoot);
		const FString TargetDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(CanonicalDraftRoot, ToolId));
		if (!TaskAtlasServiceIsSafeDirectoryName(ToolId) || !TaskAtlasServicePathEqualsOrChild(TargetDir, CanonicalDraftRoot))
		{
			Result.ErrorCode = TEXT("invalid_name");
			Result.ErrorMessage = TEXT("Refused to write official toolset draft outside Saved/UnrealMcp/OfficialToolsetDrafts.");
			return Result;
		}
		if (IFileManager::Get().DirectoryExists(*TargetDir))
		{
			Result.ErrorCode = TEXT("collision_existing_target");
			Result.ErrorMessage = FString::Printf(TEXT("Official toolset draft target already exists: %s"), *TargetDir);
			return Result;
		}

		const FString StagingDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(
			CanonicalDraftRoot,
			FString::Printf(TEXT("__staging_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short))));
		Result.StagingDir = StagingDir;

		TaskAtlasComposite::FOfficialToolsetBuildProduct Product;
		FString FailureReason;
		const TSet<FString> VisibleCoreToolNames = Req.VisibleCoreToolNames.Num() > 0
			? Req.VisibleCoreToolNames
			: TaskAtlasServiceVisibleCoreToolNames();
		if (!TaskAtlasComposite::BuildOfficialToolsetFiles(
			ToolId,
			Req.Title,
			Req.Description,
			Req.TaskId,
			Req.ReplayEligibility,
			Req.ReplayUnavailableReason,
			Req.CriticalPath,
			Req.StepRefs,
			VisibleCoreToolNames,
			Product,
			FailureReason))
		{
			Result.ErrorCode = TEXT("official_build_failed");
			Result.ErrorMessage = FailureReason;
			return Result;
		}

		Result.ModuleName = Product.ModuleName;
		Result.ClassName = Product.ClassName;
		Result.ToolsetName = Product.ToolsetName;
		Result.MainPySha256 = Product.MainPySha256;
		for (const TaskAtlasComposite::FOfficialToolsetToolInfo& Tool : Product.Tools)
		{
			Result.ToolNames.Add(Tool.Name);
		}

		const FString StagedSourcePath = FPaths::Combine(StagingDir, Product.ModuleName + TEXT(".py"));
		const FString StagedManifestPath = FPaths::Combine(StagingDir, TEXT("manifest.json"));
		const FString FinalSourcePath = FPaths::Combine(TargetDir, Product.ModuleName + TEXT(".py"));
		const FString FinalManifestPath = FPaths::Combine(TargetDir, TEXT("manifest.json"));

		TSharedPtr<FJsonObject> Manifest;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Product.ManifestJson);
			if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
			{
				Result.ErrorCode = TEXT("manifest_parse_failed");
				Result.ErrorMessage = TEXT("Generated official toolset manifest did not parse.");
				return Result;
			}
		}
		TaskAtlasServiceUpdateOfficialManifestPaths(Manifest, FinalSourcePath, FinalSourcePath, TargetDir);

		if (!IFileManager::Get().MakeDirectory(*StagingDir, true)
			|| !FFileHelper::SaveStringToFile(Product.MainPy, *StagedSourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("staging_write_failed");
			Result.ErrorMessage = TEXT("Failed to write staged official toolset Python module.");
			return Result;
		}
		if (!TaskAtlasServiceWriteJsonObject(Manifest, StagedManifestPath))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("manifest_write_failed");
			Result.ErrorMessage = TEXT("Failed to write staged official toolset manifest.");
			return Result;
		}

		TArray<FString> ValidatorIssues;
		if (!TaskAtlasServiceRunOfficialValidator(StagedSourcePath, ValidatorIssues, FailureReason))
		{
			Result.ValidatorIssues = ValidatorIssues;
			TaskAtlasServiceUpdateOfficialValidatorStatus(Manifest, false, ValidatorIssues);
			TaskAtlasServiceWriteJsonObject(Manifest, StagedManifestPath);
			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolsetName"), Product.ToolsetName);
			Diagnostic->SetStringField(TEXT("stagingDir"), StagingDir);
			Diagnostic->SetStringField(TEXT("sourcePath"), StagedSourcePath);
			Diagnostic->SetArrayField(TEXT("validatorIssues"), TaskAtlasServiceMakeIssueValues(ValidatorIssues));
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteOperationDiagnostic(TEXT("official-validator"), ToolId, Diagnostic);
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("validator_rejected");
			Result.ErrorMessage = FailureReason;
			return Result;
		}
		TaskAtlasServiceUpdateOfficialValidatorStatus(Manifest, true, ValidatorIssues);
		if (!TaskAtlasServiceWriteJsonObject(Manifest, StagedManifestPath))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("manifest_write_failed");
			Result.ErrorMessage = TEXT("Failed to write validated official toolset manifest.");
			return Result;
		}

		if (!IFileManager::Get().Move(*TargetDir, *StagingDir, false, true))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("staging_rename_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to rename official staging directory '%s' to '%s'."), *StagingDir, *TargetDir);
			return Result;
		}

		Result.GeneratedDir = TargetDir;
		Result.ModulePath = FinalSourcePath;
		Result.ManifestPath = FinalManifestPath;
		Result.StagingDir.Reset();

		TSharedPtr<FJsonObject> RegisterResult;
		const bool bPythonCommandOk = TaskAtlasServiceRunOfficialPythonJsonCommand(
			TaskAtlasServiceBuildOfficialRegisterCommand(Product.ModuleName, FinalSourcePath, Product.ClassName, Product.ToolsetName),
			RegisterResult,
			FailureReason);
		bool bRegistered = false;
		if (RegisterResult.IsValid())
		{
			RegisterResult->TryGetBoolField(TEXT("ok"), bRegistered);
			FString ActualToolsetName;
			if (RegisterResult->TryGetStringField(TEXT("toolsetName"), ActualToolsetName) && !ActualToolsetName.TrimStartAndEnd().IsEmpty())
			{
				Product.ToolsetName = ActualToolsetName.TrimStartAndEnd();
				Result.ToolsetName = Product.ToolsetName;
				Manifest->SetStringField(TEXT("toolsetName"), Product.ToolsetName);
			}
		}
		if (!bPythonCommandOk || !bRegistered)
		{
			FString LastError = FailureReason;
			if (RegisterResult.IsValid())
			{
				RegisterResult->TryGetStringField(TEXT("error"), LastError);
			}
			TaskAtlasServiceUpdateOfficialRegistrationStatus(Manifest, TEXT("rejected"), LastError);
			TaskAtlasServiceWriteJsonObject(Manifest, FinalManifestPath);

			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolsetName"), Product.ToolsetName);
			Diagnostic->SetStringField(TEXT("generatedDir"), TargetDir);
			Diagnostic->SetStringField(TEXT("sourcePath"), FinalSourcePath);
			Diagnostic->SetStringField(TEXT("lastError"), LastError);
			if (RegisterResult.IsValid())
			{
				Diagnostic->SetObjectField(TEXT("registerResult"), RegisterResult);
			}
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteOperationDiagnostic(TEXT("official-register"), ToolId, Diagnostic);
			TSharedPtr<FJsonObject> UnregisterIgnored;
			FString UnregisterFailure;
			TaskAtlasServiceRunOfficialPythonJsonCommand(
				TaskAtlasServiceBuildOfficialUnregisterCommand(Product.ModuleName, FinalSourcePath, Product.ToolsetName),
				UnregisterIgnored,
				UnregisterFailure);
			IFileManager::Get().DeleteDirectory(*TargetDir, false, true);
			Result.ErrorCode = TEXT("registration_rejected");
			Result.ErrorMessage = LastError.IsEmpty() ? FString(TEXT("Official toolset registration failed.")) : LastError;
			return Result;
		}

		TaskAtlasServiceUpdateOfficialRegistrationStatus(Manifest, TEXT("registered"), FString());
		if (!TaskAtlasServiceWriteJsonObject(Manifest, FinalManifestPath))
		{
			Result.ErrorCode = TEXT("manifest_write_failed");
			Result.ErrorMessage = TEXT("Official toolset registered, but final manifest write failed.");
			return Result;
		}
		Result.ManifestJson = TaskAtlasServiceJsonToPrettyString(Manifest);
		Result.ValidatorIssues = ValidatorIssues;
		Result.bSucceeded = true;
		return Result;
	}

	FOfficialToolsetRollbackResult RollbackOfficialToolsetDraft(const FString& ToolsetName, const FString& ModuleName, const FString& GeneratedDir)
	{
		check(IsInGameThread());
		FOfficialToolsetRollbackResult Result;
		const FString NormalizedDir = TaskAtlasServiceNormalizeAbsolutePath(GeneratedDir);
		const FString DraftRoot = TaskAtlasServiceOfficialDraftsRootDir();
		if (!TaskAtlasServicePathEqualsOrChild(NormalizedDir, DraftRoot))
		{
			Result.ErrorCode = TEXT("path_unsafe");
			Result.ErrorMessage = TEXT("Refused to roll back official toolset draft outside Saved/UnrealMcp/OfficialToolsetDrafts.");
			return Result;
		}

		const FString SourcePath = FPaths::Combine(NormalizedDir, ModuleName + TEXT(".py"));
		const FString ManifestPath = FPaths::Combine(NormalizedDir, TEXT("manifest.json"));
		TSharedPtr<FJsonObject> PythonResult;
		FString FailureReason;
		const bool bPythonCommandOk = TaskAtlasServiceRunOfficialPythonJsonCommand(
			TaskAtlasServiceBuildOfficialUnregisterCommand(ModuleName, SourcePath, ToolsetName),
			PythonResult,
			FailureReason);
		bool bUnregistered = false;
		if (PythonResult.IsValid())
		{
			PythonResult->TryGetBoolField(TEXT("ok"), bUnregistered);
		}

		TSharedPtr<FJsonObject> Manifest;
		if (TaskAtlasServiceLoadJsonObject(ManifestPath, Manifest))
		{
			FString LastError = FailureReason;
			if (!bPythonCommandOk || !bUnregistered)
			{
				if (PythonResult.IsValid())
				{
					PythonResult->TryGetStringField(TEXT("error"), LastError);
				}
			}
			TaskAtlasServiceUpdateOfficialRegistrationStatus(
				Manifest,
				bPythonCommandOk && bUnregistered ? FString(TEXT("unregistered")) : FString(TEXT("unregister_failed")),
				LastError);
			Result.UpdatedManifestJson = TaskAtlasServiceJsonToPrettyString(Manifest);
			TaskAtlasServiceWriteJsonObject(Manifest, ManifestPath);
		}

		if (!bPythonCommandOk || !bUnregistered)
		{
			Result.ErrorCode = TEXT("unregister_failed");
			Result.ErrorMessage = FailureReason.IsEmpty() ? FString(TEXT("Official toolset unregister failed.")) : FailureReason;
			return Result;
		}
		if (!IFileManager::Get().DeleteDirectory(*NormalizedDir, false, true))
		{
			Result.ErrorCode = TEXT("filesystem_delete_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to delete official toolset draft directory: %s"), *NormalizedDir);
			return Result;
		}
		Result.bSucceeded = true;
		return Result;
	}
#endif

	FPromoteToRagResult PromoteToRag(const FPromoteToRagRequest& Req)
	{
		check(IsInGameThread());
		FPromoteToRagResult Result;
		Result.TaskId = Req.TaskId.TrimStartAndEnd();
		Result.bDryRun = Req.bDryRun;

		FLoadedTask Task;
		FString FailureReason;
		if (!TaskAtlasServiceLoadTaskById(Result.TaskId, Task, FailureReason))
		{
			Result.Outcome = EPromoteToRagOutcome::NotFound;
			Result.ErrorCode = TEXT("task_not_found");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
			return Result;
		}

		Result.TaskId = Task.Model.TaskId;
		Result.SourcePath = Task.TaskPath;
		const FString Markdown = TaskAtlasServiceBuildTaskKnowledgeMarkdown(Task);
		Result.MarkdownLength = Markdown.Len();
		if (!TaskAtlasServiceBuildTaskAtlasKnowledgeSourcePath(Task.Model.TaskId, !Req.bDryRun, Result.KnowledgeSourcePath, FailureReason))
		{
			Result.Outcome = EPromoteToRagOutcome::Refused;
			Result.ErrorCode = TEXT("path_unsafe");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
			return Result;
		}

		if (Req.bDryRun)
		{
			Result.Outcome = EPromoteToRagOutcome::DryRun;
			Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
			return Result;
		}

		if (!FFileHelper::SaveStringToFile(Markdown, *Result.KnowledgeSourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Result.Outcome = EPromoteToRagOutcome::Failed;
			Result.ErrorCode = TEXT("filesystem_write_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to write Task Atlas RAG source: %s"), *Result.KnowledgeSourcePath);
			Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
			return Result;
		}

		FJsonObject RefreshArguments;
		FUnrealMcpExecutionResult RefreshResult;
#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceFailNextKnowledgeRefreshForTests)
		{
			bTaskAtlasServiceFailNextKnowledgeRefreshForTests = false;
			RefreshResult.bIsError = true;
			RefreshResult.Text = TEXT("test_forced_refresh_failure");
			RefreshResult.StructuredContent = MakeShared<FJsonObject>();
			RefreshResult.StructuredContent->SetStringField(TEXT("action"), TEXT("knowledge_index_refresh"));
			RefreshResult.StructuredContent->SetStringField(TEXT("errorCode"), TEXT("test_forced_refresh_failure"));
		}
		else
#endif
		{
			RefreshResult = KnowledgeIndexRefresh(RefreshArguments);
		}

		Result.RefreshResultText = RefreshResult.Text;
		Result.RefreshResult = RefreshResult.StructuredContent;
		if (RefreshResult.bIsError)
		{
			Result.Outcome = EPromoteToRagOutcome::RefreshFailed;
			Result.ErrorCode = TEXT("knowledge_refresh_failed");
			Result.ErrorMessage = RefreshResult.Text.IsEmpty() ? FString(TEXT("unreal.knowledge_index_refresh failed.")) : RefreshResult.Text;
			Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
			return Result;
		}

		Result.Outcome = EPromoteToRagOutcome::Promoted;
		Result.StructuredContent = TaskAtlasServiceMakePromoteResultContent(Result);
		return Result;
	}

	FPromoteToRagResult PromoteToRag(const FString& TaskId)
	{
		FPromoteToRagRequest Req;
		Req.TaskId = TaskId;
		return PromoteToRag(Req);
	}

	FSmokeResult SmokeMadeTool(const FSmokeRequest& Req)
	{
		check(IsInGameThread());
		FSmokeResult Result;
		Result.ToolName = Req.ToolName.TrimStartAndEnd();
		Result.bDryRun = Req.bDryRun;

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		FResolvedMadeTool Tool;
		FString ErrorCode;
		FString ErrorMessage;
		if (!TaskAtlasServiceResolveMadeTool(Result.ToolName, true, Tool, ErrorCode, ErrorMessage))
		{
			Result.Outcome = ErrorCode == TEXT("not_found") ? ESmokeOutcome::NotFound : ESmokeOutcome::Refused;
			Result.ErrorCode = ErrorCode;
			Result.ErrorMessage = ErrorMessage;
			Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
			return Result;
		}
		Result.ToolName = Tool.ToolName;
		Result.ReplayStatusBefore = TaskAtlasServiceReadReplayStatus(Tool.ToolJson);
		Result.ReplayStatusAfter = Result.ReplayStatusBefore;

		TSharedPtr<FJsonObject> SmokeArgs;
		if (!TaskAtlasServiceReadSmokeArgs(Tool.ToolJson, SmokeArgs))
		{
			Result.Outcome = ESmokeOutcome::Refused;
			Result.ErrorCode = TEXT("no_smoke_args");
			Result.ErrorMessage = TEXT("Task Atlas generated tool is missing required smokeArgs in tool.json.");
			Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
			return Result;
		}

		const FString SmokeArgsText = TaskAtlasServiceJsonToCondensedString(SmokeArgs);
		if (Req.bDryRun)
		{
			FString ReplayEligibility;
			Tool.ToolJson->TryGetStringField(TEXT("replayEligibility"), ReplayEligibility);
			Result.Outcome = ESmokeOutcome::DryRun;
			Result.bVerdictMatchesEligibility = true;
			Result.SmokeText = FString::Printf(
				TEXT("Dry-run plan: would call %s with smokeArgs %s (replayEligibility=%s)."),
				*Tool.ToolName,
				*SmokeArgsText,
				*(ReplayEligibility.IsEmpty() ? FString(TEXT("unknown")) : ReplayEligibility));
			Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
			Result.StructuredContent->SetObjectField(TEXT("smokeArgs"), SmokeArgs);
			return Result;
		}

		if (!TaskAtlasServiceIsUserToolLoaded(Tool.ToolName))
		{
			const UserRegistry::FReloadResult ReloadResult = TaskAtlasServiceReloadUserRegistry(false);
			bool bTargetRejected = false;
			TArray<FString> RejectedReasons;
			TaskAtlasServiceCollectReloadRejections(ReloadResult, Tool.ToolName, RejectedReasons, bTargetRejected);
			if (bTargetRejected)
			{
				Result.Outcome = ESmokeOutcome::ReloadRejected;
				Result.ErrorCode = TEXT("reload_rejected");
				Result.ErrorMessage = TEXT("Generated user tool was rejected by registry reload before smoke.");
				Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
				Result.StructuredContent->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(RejectedReasons));
				return Result;
			}
		}

		FJsonObject SmokeArguments;
		SmokeArguments.SetStringField(TEXT("toolName"), Tool.ToolName);
		SmokeArguments.SetStringField(TEXT("dryRunArgs"), SmokeArgsText);
		SmokeArguments.SetNumberField(TEXT("timeoutSeconds"), 15.0);
		const FUnrealMcpExecutionResult SmokeExecutionResult = UnrealMcpUserToolSmokeTool::Execute(SmokeArguments);
		Result.SmokeText = SmokeExecutionResult.Text;

		const bool bHasBlockedSteps = TaskAtlasServiceSmokeResultHasBlockedSteps(SmokeExecutionResult);
		if (SmokeExecutionResult.bIsError || bHasBlockedSteps)
		{
			const FString Reason = SmokeExecutionResult.bIsError
				? (SmokeExecutionResult.Text.IsEmpty() ? FString(TEXT("mcp_user_tool_smoke failed")) : SmokeExecutionResult.Text)
				: FString(TEXT("mcp_user_tool_smoke reported blocked steps"));
			Result.Outcome = ESmokeOutcome::Failed;
			Result.ErrorCode = SmokeExecutionResult.bIsError ? FString(TEXT("smoke_failed")) : FString(TEXT("smoke_blocked_steps"));
			Result.ErrorMessage = Reason;

			TaskAtlasServiceWriteFailureMarker(Tool, Reason);
			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolName"), Tool.ToolName);
			Diagnostic->SetStringField(TEXT("targetDir"), Tool.TargetDir);
			Diagnostic->SetStringField(TEXT("reason"), Reason);
			Diagnostic->SetStringField(TEXT("smokeText"), SmokeExecutionResult.Text);
			if (SmokeExecutionResult.StructuredContent.IsValid())
			{
				Diagnostic->SetObjectField(TEXT("smokeStructuredContent"), SmokeExecutionResult.StructuredContent);
			}
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteOperationDiagnostic(TEXT("smoke"), Tool.ToolId, Diagnostic);

			FString MetadataFailure;
			if (!TaskAtlasServiceSetToolJsonReplayStatus(Tool, TEXT("generated_smoke_failed"), Result.FailureDiagnosticPath, MetadataFailure))
			{
				Result.ErrorMessage += FString::Printf(TEXT(" Metadata update failed: %s."), *MetadataFailure);
			}
			else
			{
				BumpUserToolListVersion();
			}
			Result.ReplayStatusAfter = TEXT("generated_smoke_failed");
			Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
			if (SmokeExecutionResult.StructuredContent.IsValid())
			{
				Result.StructuredContent->SetObjectField(TEXT("smokeStructuredContent"), SmokeExecutionResult.StructuredContent);
			}
			return Result;
		}

		Result.Outcome = ESmokeOutcome::Passed;
		Result.bVerdictMatchesEligibility = !Result.ReplayStatusBefore.Equals(TEXT("blocked"), ESearchCase::IgnoreCase)
			&& !Result.ReplayStatusBefore.Equals(TEXT("generated_smoke_failed"), ESearchCase::IgnoreCase);
		Result.StructuredContent = TaskAtlasServiceMakeSmokeResultContent(Result);
		if (SmokeExecutionResult.StructuredContent.IsValid())
		{
			Result.StructuredContent->SetObjectField(TEXT("smokeStructuredContent"), SmokeExecutionResult.StructuredContent);
		}
		return Result;
	}

	FSmokeResult SmokeMadeTool(const FString& ToolName)
	{
		FSmokeRequest Req;
		Req.ToolName = ToolName;
		return SmokeMadeTool(Req);
	}

	TArray<FUserToolView> IntrospectUserRegistry()
	{
		TArray<FUserToolView> Views;
		UserToolLock::FSharedGuard ReadGuard;
		for (const UserRegistry::FUserToolEntry* Entry : UserRegistry::GetAllUserTools())
		{
			if (!Entry)
			{
				continue;
			}

			FUserToolView View;
			View.ToolName = Entry->ToolName;
			View.ScaffoldDir = TaskAtlasServiceMakeDisplayPath(Entry->ScaffoldDir);
			View.PythonSha = Entry->PythonHandlerSha256;
			View.LifecycleState = Extension::LifecycleStateToString(Entry->LifecycleState);
			View.ToolJsonPath = TaskAtlasServiceMakeDisplayPath(FPaths::Combine(Entry->ScaffoldDir, TEXT("tool.json")));
			View.PythonPath = TaskAtlasServiceMakeDisplayPath(Entry->PythonHandlerPath);
			View.bLoaded = true;

			if (Entry->ToolJson.IsValid())
			{
				Entry->ToolJson->TryGetStringField(TEXT("generator"), View.Generator);
				Entry->ToolJson->TryGetStringField(TEXT("sourceKind"), View.SourceKind);
				Entry->ToolJson->TryGetStringField(TEXT("sourceTaskId"), View.SourceTaskId);
				if (View.SourceTaskId.IsEmpty())
				{
					Entry->ToolJson->TryGetStringField(TEXT("taskId"), View.SourceTaskId);
				}
			}
			if (View.Generator.IsEmpty())
			{
				View.Generator = TEXT("unknown");
			}
			if (View.SourceKind.IsEmpty())
			{
				View.SourceKind = View.Generator == TEXT("task_atlas_make_composite")
					? FString(TEXT("GeneratedComposite"))
					: FString(TEXT("UserRegistry"));
			}

			Views.Add(MoveTemp(View));
		}

		Views.Sort(
			[](const FUserToolView& Left, const FUserToolView& Right)
			{
				if (Left.bLoaded != Right.bLoaded)
				{
					return Left.bLoaded;
				}
				return Left.ToolName < Right.ToolName;
			});
		return Views;
	}
}
