#include "UnrealMcpEngineCompat.h"

#if WITH_DEV_AUTOMATION_TESTS && UNREALMCP_HAS_OFFICIAL_TOOLSETS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IPythonScriptPlugin.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ToolsetRegistry/ToolCallAsyncResultString.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealMcpCaptureRedaction.h"
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpSession.h"
#include "UnrealMcpSharedPathResolver.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpModule.h"

namespace UnrealMcpOfficialToolsetGenTests
{
	const FString ToolId = TEXT("codex_official_toolset_gen");
	const FString TaskId = TEXT("official-toolset-gen-test");
	const FString WiredTaskId = TEXT("official-toolset-wired-test");
	const FString WiredLabel = TEXT("Official Wired Toolset Gen");

	FString DraftDir()
	{
		return FPaths::Combine(UnrealMcp::TaskAtlasService::OfficialToolsetDraftsRootDir(), ToolId);
	}

	FString ExpectedToolsetName(const FString& ModuleName, const FString& ClassName)
	{
		return FString::Printf(
			TEXT("Temp.UnrealMcp.OfficialToolsetDrafts.%s.%s.%s"),
			*ToolId,
			*ModuleName,
			*ClassName);
	}

	void DeleteDraftDir()
	{
		IFileManager::Get().DeleteDirectory(*DraftDir(), false, true);
	}

	FString ProjectTaskPath(const FString& InTaskId)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/Tasks"), InTaskId + TEXT(".json")));
	}

	FString WiredTestRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/OfficialToolsetWiredTest")));
	}

	FString WiredToolId()
	{
		return UnrealMcp::TaskAtlasService::MakeAtlasToolId(WiredLabel, WiredTaskId);
	}

	FString WiredDraftDir()
	{
		return FPaths::Combine(UnrealMcp::TaskAtlasService::OfficialToolsetDraftsRootDir(), WiredToolId());
	}

	bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString JsonToString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		if (!Object.IsValid())
		{
			return Output;
		}
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	bool ParsePossiblyStringWrappedPayload(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		TSharedPtr<FJsonValue> RootValue;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
		{
			return false;
		}
		if (RootValue->Type == EJson::Object)
		{
			OutObject = RootValue->AsObject();
			const TSharedPtr<FJsonValue> ReturnValue = OutObject.IsValid() ? OutObject->TryGetField(TEXT("returnValue")) : nullptr;
			if (ReturnValue.IsValid() && ReturnValue->Type == EJson::String)
			{
				return ParseJsonObject(ReturnValue->AsString(), OutObject);
			}
			if (ReturnValue.IsValid() && ReturnValue->Type == EJson::Object)
			{
				OutObject = ReturnValue->AsObject();
			}
			return OutObject.IsValid();
		}
		if (RootValue->Type == EJson::String)
		{
			return ParseJsonObject(RootValue->AsString(), OutObject);
		}
		return false;
	}

	TSharedPtr<FJsonObject> MakeObject()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonValue> MakeStepRef(const FString& CaptureRef)
	{
		TSharedPtr<FJsonObject> StepRef = MakeShared<FJsonObject>();
		StepRef->SetNumberField(TEXT("ordinal"), 0);
		StepRef->SetStringField(TEXT("eventId"), TEXT("official-gen-event-0"));
		StepRef->SetStringField(TEXT("tool"), TEXT("unreal.editor_status"));
		StepRef->SetStringField(TEXT("captureStatus"), TEXT("captured"));
		StepRef->SetStringField(TEXT("captureRef"), CaptureRef);
		StepRef->SetStringField(TEXT("policyClassAtCapture"), TEXT("allow"));
		return MakeShared<FJsonValueObject>(StepRef);
	}

	bool WriteWiredTaskFixture(const FString& CaptureRef)
	{
		TSharedPtr<FJsonObject> Task = MakeObject();
		Task->SetStringField(TEXT("taskId"), WiredTaskId);
		Task->SetStringField(TEXT("label"), WiredLabel);
		Task->SetStringField(TEXT("rating"), TEXT("unrated"));
		Task->SetBoolField(TEXT("pinned"), false);
		Task->SetStringField(TEXT("tStartUtc"), TEXT("2026-06-30T00:00:00Z"));
		Task->SetStringField(TEXT("tEndUtc"), TEXT("2026-06-30T00:00:01Z"));
		Task->SetStringField(TEXT("userIntentText"), TEXT("Exercise make_composite emitOfficial wiring."));
		Task->SetStringField(TEXT("aiSummaryText"), TEXT("Wired official toolset generation from make_composite."));
		Task->SetStringField(TEXT("replayEligibility"), TEXT("preview_ready"));
		Task->SetStringField(TEXT("replayUnavailableReason"), FString());

		TArray<TSharedPtr<FJsonValue>> CriticalPath;
		CriticalPath.Add(MakeShared<FJsonValueString>(TEXT("unreal.editor_status")));
		Task->SetArrayField(TEXT("criticalPath"), CriticalPath);

		TArray<TSharedPtr<FJsonValue>> StepRefs;
		StepRefs.Add(MakeStepRef(CaptureRef));
		Task->SetArrayField(TEXT("stepRefs"), StepRefs);

		const FString TaskPath = ProjectTaskPath(WiredTaskId);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TaskPath), true);
		return FFileHelper::SaveStringToFile(JsonToString(Task), *TaskPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TSet<FString> VisibleCoreToolsForFixture()
	{
		TSet<FString> VisibleTools;
		for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
		{
			if (Entry.Exposure == UnrealMcp::EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
			{
				VisibleTools.Add(Entry.Name);
			}
		}
		return VisibleTools;
	}

	FUnrealMcpExecutionResult ExecuteMcpTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
	{
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.ExecuteToolFromEditorUI(ToolName, *Arguments);
	}

	FString MakeCaptureSessionId()
	{
		return TEXT("official-gen-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower();
	}

	void DeleteCaptureSession(const FString& SessionId)
	{
		IFileManager::Get().DeleteDirectory(
			*FPaths::ConvertRelativePathToFull(FPaths::Combine(UnrealMcp::CapturedArgsStore::GetCapturedArgsRoot(), SessionId)),
			false,
			true);
	}

	bool WriteCapturedArgsFixture(const FString& SessionId, const FString& ProbeValue, FString& OutCaptureRef)
	{
		(void)ProbeValue;
		TSharedPtr<FJsonObject> Args = MakeObject();
		const UnrealMcp::CaptureRedaction::FRedactionResult Redacted =
			UnrealMcp::CaptureRedaction::SanitizeToolArguments_Pure(TEXT("unreal.editor_status"), Args, 4096, 65536);
		FString CaptureSha256;
		return UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
			SessionId,
			TEXT("official-gen-event-0"),
			TEXT("unreal.editor_status"),
			TEXT("2026-06-30T00:00:00Z"),
			Redacted,
			OutCaptureRef,
			CaptureSha256);
	}

	FString QuoteCommandLineArgument(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
		return TEXT("\"") + Value + TEXT("\"");
	}

	bool ResolveValidatorPath(FString& OutValidatorPath, FString& OutFailureReason)
	{
		const UnrealMcp::FToolsReadResolution Resolution = UnrealMcp::ResolveToolsReadSubpath(
			TEXT("UnrealMcpOfficialToolsets/validate_official_toolset.py"),
			{ TEXT("validate_official_toolset.py") });
		if (!Resolution.bFound || !FPaths::FileExists(Resolution.Path))
		{
			OutFailureReason = Resolution.Warning.IsEmpty()
				? FString(TEXT("validator not found"))
				: Resolution.Warning;
			return false;
		}
		OutValidatorPath = Resolution.Path;
		return true;
	}

	bool RunValidator(const FString& SourcePath, FString& OutOutput, int32& OutReturnCode)
	{
		FString ValidatorPath;
		FString FailureReason;
		if (!ResolveValidatorPath(ValidatorPath, FailureReason))
		{
			OutOutput = FailureReason;
			OutReturnCode = -1;
			return false;
		}
		const FString Params = FString::Printf(TEXT("%s %s"), *QuoteCommandLineArgument(ValidatorPath), *QuoteCommandLineArgument(SourcePath));
		FString StdOut;
		FString StdErr;
		const bool bLaunched = FPlatformProcess::ExecProcess(
#if PLATFORM_MAC
			TEXT("/usr/bin/python3"),
#else
			TEXT("python3"),
#endif
			*Params,
			&OutReturnCode,
			&StdOut,
			&StdErr,
			*FPaths::ProjectDir());
		OutOutput = StdOut + StdErr;
		return bLaunched;
	}

	FString PythonStringLiteral(const FString& Value)
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
			else
			{
				Result.AppendChar(Character);
			}
		}
		Result += TEXT("\"");
		return Result;
	}

	IPythonScriptPlugin* LoadPythonScriptPlugin()
	{
		static const FName PythonScriptPluginModuleName(TEXT("PythonScriptPlugin"));
		if (IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName))
		{
			return PythonPlugin;
		}
		return FModuleManager::LoadModulePtr<IPythonScriptPlugin>(PythonScriptPluginModuleName);
	}

	bool RunPythonJsonCommand(const FString& Command, TSharedPtr<FJsonObject>& OutResult, FString& OutFailureReason)
	{
		OutResult.Reset();
		OutFailureReason.Reset();
		IPythonScriptPlugin* PythonPlugin = LoadPythonScriptPlugin();
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
			OutFailureReason = TEXT("Python is not available.");
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
			CapturedOutput += TEXT("\n") + LogEntry.Output;
		}

		const FString Begin = TEXT("__UEATELIER_OFFICIAL_TEST_JSON_BEGIN__");
		const FString End = TEXT("__UEATELIER_OFFICIAL_TEST_JSON_END__");
		const int32 BeginIndex = CapturedOutput.Find(Begin, ESearchCase::CaseSensitive);
		if (BeginIndex == INDEX_NONE)
		{
			OutFailureReason = TEXT("Python command did not emit JSON sentinel.");
			return false;
		}
		const int32 JsonStart = BeginIndex + Begin.Len();
		const int32 EndIndex = CapturedOutput.Find(End, ESearchCase::CaseSensitive, ESearchDir::FromStart, JsonStart);
		if (EndIndex == INDEX_NONE || EndIndex <= JsonStart)
		{
			OutFailureReason = TEXT("Python command emitted incomplete JSON sentinel.");
			return false;
		}

		const FString ResultJson = CapturedOutput.Mid(JsonStart, EndIndex - JsonStart).TrimStartAndEnd();
		return bExecuted && ParseJsonObject(ResultJson, OutResult);
	}

	bool QueryClassRegistered(const FString& ModuleName, const FString& ClassName, bool& bOutClassRegistered, FString& OutFailureReason)
	{
		const FString Command = FString::Printf(
			TEXT("import json, sys, unreal\n")
			TEXT("_module = sys.modules.get(%s)\n")
			TEXT("_cls = getattr(_module, %s, None) if _module else None\n")
			TEXT("_registered = bool(_cls is not None and unreal.ToolsetRegistry.is_toolset_class_registered(_cls))\n")
			TEXT("print(\"__UEATELIER_OFFICIAL_TEST_JSON_BEGIN__\" + json.dumps({\"classRegistered\": _registered}) + \"__UEATELIER_OFFICIAL_TEST_JSON_END__\")\n"),
			*PythonStringLiteral(ModuleName),
			*PythonStringLiteral(ClassName));
		TSharedPtr<FJsonObject> Result;
		if (!RunPythonJsonCommand(Command, Result, OutFailureReason) || !Result.IsValid())
		{
			return false;
		}
		Result->TryGetBoolField(TEXT("classRegistered"), bOutClassRegistered);
		return true;
	}

	FString LaunchActivityLogPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/ActivityLog"),
			UnrealMcp::GetLaunchSessionId() + TEXT(".jsonl")));
	}

	int32 LoadActivityLogLines(TArray<FString>& OutLines)
	{
		OutLines.Reset();
		FFileHelper::LoadFileToStringArray(OutLines, *LaunchActivityLogPath());
		return OutLines.Num();
	}

	bool PayloadHasArgumentKey(const TSharedPtr<FJsonObject>& Payload, const FString& RequiredArgumentKey)
	{
		if (RequiredArgumentKey.IsEmpty())
		{
			return Payload.IsValid();
		}
		const TArray<TSharedPtr<FJsonValue>>* ArgumentKeys = nullptr;
		if (!Payload.IsValid() || !Payload->TryGetArrayField(TEXT("argumentKeys"), ArgumentKeys) || !ArgumentKeys)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& ArgumentKeyValue : *ArgumentKeys)
		{
			FString ArgumentKey;
			if (ArgumentKeyValue.IsValid() && ArgumentKeyValue->TryGetString(ArgumentKey) && ArgumentKey == RequiredArgumentKey)
			{
				return true;
			}
		}
		return false;
	}

	TSharedPtr<FJsonObject> FindToolCallPayloadAfterLine(
		const TArray<FString>& Lines,
		int32 FirstLineIndex,
		const FString& ToolName,
		const FString& RequiredArgumentKey)
	{
		for (int32 Index = FMath::Max(0, FirstLineIndex); Index < Lines.Num(); ++Index)
		{
			TSharedPtr<FJsonObject> Record;
			if (!ParseJsonObject(Lines[Index], Record))
			{
				continue;
			}

			FString EventKind;
			Record->TryGetStringField(TEXT("eventKind"), EventKind);
			if (EventKind != TEXT("tool_call"))
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* Payload = nullptr;
			if (!Record->TryGetObjectField(TEXT("payload"), Payload) || !Payload || !(*Payload).IsValid())
			{
				continue;
			}

			FString PayloadToolName;
			(*Payload)->TryGetStringField(TEXT("toolName"), PayloadToolName);
			if (PayloadToolName == ToolName && PayloadHasArgumentKey(*Payload, RequiredArgumentKey))
			{
				return *Payload;
			}
		}
		return nullptr;
	}

	FString FirstGeneratedToolParamName(const TSharedPtr<FJsonObject>& Manifest)
	{
		const TArray<TSharedPtr<FJsonValue>>* ToolNames = nullptr;
		if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("toolNames"), ToolNames) || !ToolNames || ToolNames->Num() == 0)
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> Tool = (*ToolNames)[0].IsValid() && (*ToolNames)[0]->Type == EJson::Object ? (*ToolNames)[0]->AsObject() : nullptr;
		const TSharedPtr<FJsonObject>* ParamTypes = nullptr;
		if (!Tool.IsValid() || !Tool->TryGetObjectField(TEXT("paramSchemaTypes"), ParamTypes) || !ParamTypes || !(*ParamTypes).IsValid())
		{
			return FString();
		}
		TArray<FString> Keys;
		UnrealMcp::Compat::GetJsonObjectKeys(**ParamTypes, Keys);
		Keys.Sort();
		return Keys.Num() > 0 ? Keys[0] : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialToolsetGenTest,
	"UnrealMcp.OfficialToolset.Generation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialToolsetGenTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialToolsetGenTests;

	DeleteDraftDir();
	const FString CaptureSessionId = MakeCaptureSessionId();
	const FString ProbeValue = TEXT("official-probe-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UnrealMcp::TaskAtlasService::FOfficialToolsetDraftResult DraftResult;
	ON_SCOPE_EXIT
	{
		if (!DraftResult.ToolsetName.IsEmpty() && !DraftResult.ModuleName.IsEmpty() && !DraftResult.GeneratedDir.IsEmpty())
		{
			UnrealMcp::TaskAtlasService::RollbackOfficialToolsetDraft(DraftResult.ToolsetName, DraftResult.ModuleName, DraftResult.GeneratedDir);
		}
		DeleteDraftDir();
		DeleteCaptureSession(CaptureSessionId);
	};

	FString CaptureRef;
	TestTrue(TEXT("write official captured args"), WriteCapturedArgsFixture(CaptureSessionId, ProbeValue, CaptureRef));
	if (CaptureRef.IsEmpty())
	{
		return false;
	}

	TArray<FString> CriticalPath;
	CriticalPath.Add(TEXT("unreal.editor_status"));
	TArray<TSharedPtr<FJsonValue>> StepRefs;
	StepRefs.Add(MakeStepRef(CaptureRef));

	UnrealMcp::TaskAtlasComposite::FOfficialToolsetBuildProduct Product;
	FString FailureReason;
	const bool bBuilt = UnrealMcp::TaskAtlasComposite::BuildOfficialToolsetFiles(
		ToolId,
		TEXT("Official Toolset Gen Test"),
		TEXT("Governed official toolset generated from a fixed automation composite."),
		TaskId,
		TEXT("preview_ready"),
		FString(),
		CriticalPath,
		StepRefs,
		VisibleCoreToolsForFixture(),
		Product,
		FailureReason);
	TestTrue(TEXT("official files build"), bBuilt);
	if (!FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}
	if (!bBuilt)
	{
		return false;
	}

	TestEqual(TEXT("module name"), Product.ModuleName, TEXT("codex_official_toolset_gen_official_toolset"));
	TestEqual(TEXT("class name"), Product.ClassName, TEXT("UEAtelierCodexOfficialToolsetGenToolset"));
	TestEqual(TEXT("toolset name"), Product.ToolsetName, ExpectedToolsetName(Product.ModuleName, Product.ClassName));
	TestEqual(TEXT("sha matches module"), Product.MainPySha256, UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(Product.MainPy));
	TestTrue(TEXT("toolset derives ToolsetDefinition"), Product.MainPy.Contains(TEXT("class UEAtelierCodexOfficialToolsetGenToolset(unreal.ToolsetDefinition):")));
	TestTrue(TEXT("tool_call decorator before staticmethod"), Product.MainPy.Contains(TEXT("@toolset_registry.tool_call\n    @staticmethod")));
	TestTrue(TEXT("delegates through call_tool"), Product.MainPy.Contains(TEXT("return unreal.UnrealMcpCallTool.call_tool(\"unreal.editor_status\", json.dumps(")));
	TestFalse(TEXT("no direct editor mutation APIs"), Product.MainPy.Contains(TEXT("EditorLevelLibrary")) || Product.MainPy.Contains(TEXT("EditorAssetLibrary")));

	TSharedPtr<FJsonObject> InitialManifest;
	TestTrue(TEXT("initial manifest parses"), ParseJsonObject(Product.ManifestJson, InitialManifest));
	if (!InitialManifest.IsValid())
	{
		return false;
	}
	FString InitialToolsetName;
	FString InitialClassName;
	InitialManifest->TryGetStringField(TEXT("toolsetName"), InitialToolsetName);
	InitialManifest->TryGetStringField(TEXT("className"), InitialClassName);
	TestEqual(TEXT("manifest toolset name"), InitialToolsetName, Product.ToolsetName);
	TestEqual(TEXT("manifest class name"), InitialClassName, Product.ClassName);
	TestTrue(TEXT("manifest schema hash present"), InitialManifest->HasField(TEXT("schemaHash")));
	TestTrue(TEXT("manifest toolNames present"), InitialManifest->HasTypedField<EJson::Array>(TEXT("toolNames")));

	UnrealMcp::TaskAtlasService::FOfficialToolsetDraftRequest Request;
	Request.ToolId = ToolId;
	Request.Title = TEXT("Official Toolset Gen Test");
	Request.Description = TEXT("Governed official toolset generated from a fixed automation composite.");
	Request.TaskId = TaskId;
	Request.ReplayEligibility = TEXT("preview_ready");
	Request.CriticalPath = CriticalPath;
	Request.StepRefs = StepRefs;
	Request.VisibleCoreToolNames = VisibleCoreToolsForFixture();
	DraftResult = UnrealMcp::TaskAtlasService::GenerateOfficialToolsetDraft(Request);
	TestTrue(TEXT("official draft generated and registered"), DraftResult.bSucceeded);
	if (!DraftResult.ErrorMessage.IsEmpty())
	{
		AddError(DraftResult.ErrorMessage);
	}
	if (!DraftResult.bSucceeded)
	{
		return false;
	}

	TestTrue(TEXT("generated source exists"), FPaths::FileExists(DraftResult.ModulePath));
	TestTrue(TEXT("generated manifest exists"), FPaths::FileExists(DraftResult.ManifestPath));
	TestEqual(TEXT("draft toolset name matches pure build"), DraftResult.ToolsetName, Product.ToolsetName);
	TestEqual(TEXT("draft class name matches pure build"), DraftResult.ClassName, Product.ClassName);
	TestEqual(TEXT("draft module sha matches"), DraftResult.MainPySha256, Product.MainPySha256);
	TestTrue(TEXT("draft has one generated tool"), DraftResult.ToolNames.Num() == 1);

	FString ValidatorOutput;
	int32 ValidatorReturnCode = -1;
	TestTrue(TEXT("shell validator launched"), RunValidator(DraftResult.ModulePath, ValidatorOutput, ValidatorReturnCode));
	TestEqual(TEXT("shell validator exit 0"), ValidatorReturnCode, 0);
	TestTrue(TEXT("shell validator empty output"), ValidatorOutput.TrimStartAndEnd().IsEmpty());

	TSharedPtr<FJsonObject> Manifest;
	TestTrue(TEXT("registered manifest parses"), ParseJsonObject(DraftResult.ManifestJson, Manifest));
	if (!Manifest.IsValid())
	{
		return false;
	}
	FString FinalManifestToolsetName;
	Manifest->TryGetStringField(TEXT("toolsetName"), FinalManifestToolsetName);
	TestEqual(TEXT("final manifest toolset name"), FinalManifestToolsetName, DraftResult.ToolsetName);
	const TSharedPtr<FJsonObject>* ValidatorStatus = nullptr;
	TestTrue(TEXT("manifest validatorStatus object"), Manifest->TryGetObjectField(TEXT("validatorStatus"), ValidatorStatus) && ValidatorStatus && (*ValidatorStatus).IsValid());
	if (ValidatorStatus && (*ValidatorStatus).IsValid())
	{
		bool bValidatorPassed = false;
		(*ValidatorStatus)->TryGetBoolField(TEXT("passed"), bValidatorPassed);
		TestTrue(TEXT("manifest validator passed"), bValidatorPassed);
	}
	const TSharedPtr<FJsonObject>* RegistrationStatus = nullptr;
	TestTrue(TEXT("manifest registrationStatus object"), Manifest->TryGetObjectField(TEXT("registrationStatus"), RegistrationStatus) && RegistrationStatus && (*RegistrationStatus).IsValid());
	if (RegistrationStatus && (*RegistrationStatus).IsValid())
	{
		FString State;
		(*RegistrationStatus)->TryGetStringField(TEXT("state"), State);
		TestEqual(TEXT("manifest registered"), State, TEXT("registered"));
	}

	TestTrue(TEXT("toolset registered by name"), UToolsetRegistry::IsToolsetRegistered(DraftResult.ToolsetName));
	bool bClassRegistered = false;
	TestTrue(TEXT("query class registration"), QueryClassRegistered(DraftResult.ModuleName, DraftResult.ClassName, bClassRegistered, FailureReason));
	TestTrue(TEXT("toolset class registered"), bClassRegistered);

	TArray<FString> BeforeLines;
	const int32 BeforeCount = LoadActivityLogLines(BeforeLines);

	FString InputJson = TEXT("{}");
	const FString ParamName = FirstGeneratedToolParamName(Manifest);
	if (!ParamName.IsEmpty())
	{
		InputJson = FString::Printf(TEXT("{\"%s\":\"%s\"}"), *ParamName, *ProbeValue);
	}

	TStrongObjectPtr<UToolCallAsyncResultString> AsyncResult(UToolsetRegistry::ExecuteTool(DraftResult.ToolsetName, DraftResult.ToolNames[0], InputJson));
	TestTrue(TEXT("execute_tool returned result"), AsyncResult.IsValid());
	if (!AsyncResult.IsValid())
	{
		return false;
	}
	for (int32 Attempt = 0; Attempt < 10 && !AsyncResult->bIsComplete; ++Attempt)
	{
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("execute_tool completed"), AsyncResult->bIsComplete);
	TestTrue(TEXT("execute_tool error empty"), AsyncResult->Error.IsEmpty());

	TSharedPtr<FJsonObject> CallToolPayload;
	TestTrue(TEXT("execute_tool payload parses"), ParsePossiblyStringWrappedPayload(AsyncResult->Value, CallToolPayload));
	if (CallToolPayload.IsValid())
	{
		bool bIsError = true;
		CallToolPayload->TryGetBoolField(TEXT("isError"), bIsError);
		TestFalse(TEXT("delegated call_tool is not error"), bIsError);
		TestTrue(TEXT("delegated structuredContent present"), CallToolPayload->HasTypedField<EJson::Object>(TEXT("structuredContent")));
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		TestTrue(TEXT("delegated meta object"), CallToolPayload->TryGetObjectField(TEXT("meta"), Meta) && Meta && (*Meta).IsValid());
		if (Meta && (*Meta).IsValid())
		{
			FString PolicyDecision;
			(*Meta)->TryGetStringField(TEXT("policyDecision"), PolicyDecision);
			TestEqual(TEXT("delegated policy allow"), PolicyDecision, TEXT("allow"));
		}
	}

	TArray<FString> AfterLines;
	const int32 AfterCount = LoadActivityLogLines(AfterLines);
	const int32 FirstCandidateLine = AfterCount > BeforeCount ? BeforeCount : 0;
	const TSharedPtr<FJsonObject> ActivityPayload = FindToolCallPayloadAfterLine(AfterLines, FirstCandidateLine, TEXT("unreal.editor_status"), FString());
	TestTrue(TEXT("delegated call wrote ActivityLog tool_call"), ActivityPayload.IsValid());
	if (ActivityPayload.IsValid())
	{
		bool bIsError = true;
		bool bHasStructuredContent = false;
		ActivityPayload->TryGetBoolField(TEXT("isError"), bIsError);
		ActivityPayload->TryGetBoolField(TEXT("hasStructuredContent"), bHasStructuredContent);
		TestFalse(TEXT("activity isError false"), bIsError);
		TestTrue(TEXT("activity has structured content"), bHasStructuredContent);
		TestTrue(TEXT("activity capture metadata present"), ActivityPayload->HasField(TEXT("captureStatus")));
	}

	const UnrealMcp::TaskAtlasService::FOfficialToolsetRollbackResult RollbackResult =
		UnrealMcp::TaskAtlasService::RollbackOfficialToolsetDraft(DraftResult.ToolsetName, DraftResult.ModuleName, DraftResult.GeneratedDir);
	TestTrue(TEXT("rollback succeeds"), RollbackResult.bSucceeded);
	if (!RollbackResult.ErrorMessage.IsEmpty())
	{
		AddError(RollbackResult.ErrorMessage);
	}
	TSharedPtr<FJsonObject> RollbackManifest;
	TestTrue(TEXT("rollback manifest parses"), ParseJsonObject(RollbackResult.UpdatedManifestJson, RollbackManifest));
	if (RollbackManifest.IsValid())
	{
		const TSharedPtr<FJsonObject>* RollbackRegistrationStatus = nullptr;
		TestTrue(
			TEXT("rollback manifest registrationStatus object"),
			RollbackManifest->TryGetObjectField(TEXT("registrationStatus"), RollbackRegistrationStatus)
				&& RollbackRegistrationStatus
				&& (*RollbackRegistrationStatus).IsValid());
		if (RollbackRegistrationStatus && (*RollbackRegistrationStatus).IsValid())
		{
			FString State;
			(*RollbackRegistrationStatus)->TryGetStringField(TEXT("state"), State);
			TestEqual(TEXT("rollback manifest unregistered"), State, TEXT("unregistered"));
		}
	}
	TestFalse(TEXT("draft directory deleted"), IFileManager::Get().DirectoryExists(*DraftResult.GeneratedDir));
	TestFalse(TEXT("toolset unregistered by name"), UToolsetRegistry::IsToolsetRegistered(DraftResult.ToolsetName));
	bool bClassRegisteredAfterRollback = true;
	TestTrue(TEXT("query class registration after rollback"), QueryClassRegistered(DraftResult.ModuleName, DraftResult.ClassName, bClassRegisteredAfterRollback, FailureReason));
	TestFalse(TEXT("toolset class unregistered"), bClassRegisteredAfterRollback);
	DraftResult.GeneratedDir.Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialToolsetMakeCompositeWiredTest,
	"UnrealMcp.OfficialToolset.MakeCompositeWired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialToolsetMakeCompositeWiredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialToolsetGenTests;

	const FString CaptureSessionId = MakeCaptureSessionId();
	const FString Root = WiredTestRoot();
	const FString PyToolsRoot = FPaths::Combine(Root, TEXT("Tools/UnrealMcpPyTools"));
	FString OfficialToolsetName;
	FString OfficialModuleName;
	FString OfficialGeneratedDir;
	ON_SCOPE_EXIT
	{
		if (!OfficialToolsetName.IsEmpty() && !OfficialModuleName.IsEmpty() && !OfficialGeneratedDir.IsEmpty())
		{
			UnrealMcp::TaskAtlasService::RollbackOfficialToolsetDraft(OfficialToolsetName, OfficialModuleName, OfficialGeneratedDir);
		}
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		IFileManager::Get().Delete(*ProjectTaskPath(WiredTaskId), false, true, true);
		IFileManager::Get().DeleteDirectory(*Root, false, true);
		IFileManager::Get().DeleteDirectory(*WiredDraftDir(), false, true);
		DeleteCaptureSession(CaptureSessionId);
	};

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	IFileManager::Get().DeleteDirectory(*WiredDraftDir(), false, true);
	IFileManager::Get().MakeDirectory(*PyToolsRoot, true);
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(PyToolsRoot);

	FString CaptureRef;
	TestTrue(TEXT("write wired captured args"), WriteCapturedArgsFixture(CaptureSessionId, TEXT("official-wired-probe"), CaptureRef));
	if (CaptureRef.IsEmpty())
	{
		return false;
	}
	TestTrue(TEXT("write wired Task Atlas task"), WriteWiredTaskFixture(CaptureRef));

	TSharedPtr<FJsonObject> Args = MakeObject();
	Args->SetStringField(TEXT("taskId"), WiredTaskId);
	Args->SetBoolField(TEXT("emitOfficial"), true);
	const FUnrealMcpExecutionResult Result = ExecuteMcpTool(TEXT("unreal.task_atlas_make_composite"), Args);
	TestFalse(TEXT("wired make_composite is not bIsError"), Result.bIsError);
	TestTrue(TEXT("wired structured content"), Result.StructuredContent.IsValid());
	if (!Result.StructuredContent.IsValid())
	{
		return false;
	}

	FString Outcome;
	Result.StructuredContent->TryGetStringField(TEXT("outcome"), Outcome);
	TestEqual(TEXT("wired composite outcome"), Outcome, TEXT("CompositeWritten"));
	FString GeneratedDir;
	Result.StructuredContent->TryGetStringField(TEXT("generatedDir"), GeneratedDir);
	TestFalse(TEXT("wired composite generated dir non-empty"), GeneratedDir.IsEmpty());
	TestTrue(TEXT("wired composite generated dir exists"), IFileManager::Get().DirectoryExists(*GeneratedDir));

	const TSharedPtr<FJsonObject>* OfficialDraft = nullptr;
	TestTrue(
		TEXT("officialDraft object present"),
		Result.StructuredContent->TryGetObjectField(TEXT("officialDraft"), OfficialDraft) && OfficialDraft && (*OfficialDraft).IsValid());
	if (!OfficialDraft || !(*OfficialDraft).IsValid())
	{
		return false;
	}

	bool bSupported = false;
	bool bSucceeded = false;
	(*OfficialDraft)->TryGetBoolField(TEXT("supported"), bSupported);
	(*OfficialDraft)->TryGetBoolField(TEXT("succeeded"), bSucceeded);
	TestTrue(TEXT("official draft supported"), bSupported);
	TestTrue(TEXT("official draft succeeded"), bSucceeded);
	if (!bSucceeded)
	{
		FString ErrorMessage;
		(*OfficialDraft)->TryGetStringField(TEXT("errorMessage"), ErrorMessage);
		AddError(ErrorMessage);
		return false;
	}

	(*OfficialDraft)->TryGetStringField(TEXT("toolsetName"), OfficialToolsetName);
	(*OfficialDraft)->TryGetStringField(TEXT("moduleName"), OfficialModuleName);
	(*OfficialDraft)->TryGetStringField(TEXT("generatedDir"), OfficialGeneratedDir);
	FString OfficialModulePath;
	FString OfficialManifestPath;
	(*OfficialDraft)->TryGetStringField(TEXT("modulePath"), OfficialModulePath);
	(*OfficialDraft)->TryGetStringField(TEXT("manifestPath"), OfficialManifestPath);
	TestFalse(TEXT("official toolset name non-empty"), OfficialToolsetName.IsEmpty());
	TestFalse(TEXT("official module name non-empty"), OfficialModuleName.IsEmpty());
	TestTrue(TEXT("official generated dir exists"), IFileManager::Get().DirectoryExists(*OfficialGeneratedDir));
	TestTrue(TEXT("official module exists"), FPaths::FileExists(OfficialModulePath));
	TestTrue(TEXT("official manifest exists"), FPaths::FileExists(OfficialManifestPath));
	TestTrue(TEXT("official toolset registered"), UToolsetRegistry::IsToolsetRegistered(OfficialToolsetName));

	const TSharedPtr<FJsonObject>* RegistrationStatus = nullptr;
	TestTrue(
		TEXT("officialDraft registrationStatus object"),
		(*OfficialDraft)->TryGetObjectField(TEXT("registrationStatus"), RegistrationStatus)
			&& RegistrationStatus
			&& (*RegistrationStatus).IsValid());
	if (RegistrationStatus && (*RegistrationStatus).IsValid())
	{
		FString State;
		(*RegistrationStatus)->TryGetStringField(TEXT("state"), State);
		TestEqual(TEXT("officialDraft registration state"), State, TEXT("registered"));
	}

	const UnrealMcp::TaskAtlasService::FOfficialToolsetRollbackResult RollbackResult =
		UnrealMcp::TaskAtlasService::RollbackOfficialToolsetDraft(OfficialToolsetName, OfficialModuleName, OfficialGeneratedDir);
	TestTrue(TEXT("wired rollback succeeds"), RollbackResult.bSucceeded);
	if (!RollbackResult.ErrorMessage.IsEmpty())
	{
		AddError(RollbackResult.ErrorMessage);
	}
	TestFalse(TEXT("wired draft directory deleted"), IFileManager::Get().DirectoryExists(*OfficialGeneratedDir));
	TestFalse(TEXT("wired toolset unregistered"), UToolsetRegistry::IsToolsetRegistered(OfficialToolsetName));
	OfficialGeneratedDir.Reset();
	return true;
}

#endif
