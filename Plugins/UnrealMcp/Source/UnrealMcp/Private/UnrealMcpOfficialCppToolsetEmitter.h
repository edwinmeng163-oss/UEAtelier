#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpEngineCompat.h"

class FJsonObject;
class FJsonValue;

namespace UnrealMcp::TaskAtlasOfficialCpp
{
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	struct FOfficialCppToolsetFile
	{
		FString RelativePath;
		FString Contents;
		FString Sha256;
		bool bSourceFile = false;
	};

	struct FOfficialCppToolsetToolInfo
	{
		FString FunctionName;
		FString GovernedToolId;
		FString ArgsJson;
	};

	struct FOfficialCppToolsetBuildProduct
	{
		FString ToolId;
		FString PluginName;
		FString ModuleName;
		FString ClassName;
		FString CppClassName;
		FString ToolsetName;
		FString PluginDescriptorRelativePath;
		FString ManifestJson;
		FString SchemaHash;
		TArray<FOfficialCppToolsetToolInfo> Tools;
		TArray<FOfficialCppToolsetFile> Files;
	};

	struct FOfficialCppToolsetDraftRequest
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

	struct FOfficialCppToolsetDraftResult
	{
		bool bSucceeded = false;
		FString ErrorCode;
		FString ErrorMessage;
		FString GeneratedDir;
		FString StagingDir;
		FString PluginDescriptorPath;
		FString ManifestPath;
		FString ModuleName;
		FString ClassName;
		FString ToolsetName;
		FString SchemaHash;
		FString ManifestJson;
		FString FailureDiagnosticPath;
		TArray<FString> SourceFiles;
		TArray<FString> ValidatorIssues;
		bool bBuildRequired = true;
		bool bRestartRequired = true;
		FString RegistrationState = TEXT("requires_build_restart");
	};

	bool BuildOfficialCppToolsetFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FOfficialCppToolsetBuildProduct& OutProduct,
		FString& OutFailureReason);

	bool ValidateOfficialCppToolsetFiles(
		const FOfficialCppToolsetBuildProduct& Product,
		TArray<FString>& OutIssues);

	FOfficialCppToolsetDraftResult GenerateOfficialCppToolsetDraft(const FOfficialCppToolsetDraftRequest& Req);

	struct FOfficialCppToolsetDriftResult
	{
		bool bClean = false;
		TArray<FString> Drifts;
	};

	// C4 drift detector: re-verify a PUBLISHED draft directory against its own
	// manifest (catches post-publish edits to generated files or the manifest).
	// Absent/unreadable manifest and hash mismatches are reported as drift
	// entries, never as hard errors.
	FOfficialCppToolsetDriftResult DetectOfficialCppToolsetDraftDrift(const FString& GeneratedDir);
#endif
}
