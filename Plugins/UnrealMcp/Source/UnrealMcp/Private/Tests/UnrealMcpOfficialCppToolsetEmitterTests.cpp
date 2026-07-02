#include "UnrealMcpEngineCompat.h"

#if WITH_DEV_AUTOMATION_TESTS && UNREALMCP_HAS_OFFICIAL_TOOLSETS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpOfficialCppToolsetEmitter.h"
#include "UnrealMcpTaskAtlasService.h"
namespace UnrealMcpOfficialCppToolsetEmitterTests
{
	const FString BaseToolId = TEXT("codex_official_cpp_toolset");
	const FString TaskId = TEXT("official-cpp-toolset-test");

	TSet<FString> VisibleCoreToolsForFixture()
	{
		TSet<FString> VisibleTools;
		VisibleTools.Add(TEXT("unreal.editor_status"));
		VisibleTools.Add(TEXT("unreal.list_maps"));
		return VisibleTools;
	}

	TArray<FString> CriticalPathForFixture()
	{
		TArray<FString> CriticalPath;
		CriticalPath.Add(TEXT("unreal.editor_status"));
		return CriticalPath;
	}

	TArray<TSharedPtr<FJsonValue>> StepRefsForFixture()
	{
		TSharedPtr<FJsonObject> StepRef = MakeShared<FJsonObject>();
		StepRef->SetNumberField(TEXT("ordinal"), 0);
		StepRef->SetStringField(TEXT("eventId"), TEXT("official-cpp-event-0"));
		StepRef->SetStringField(TEXT("tool"), TEXT("unreal.editor_status"));
		StepRef->SetStringField(TEXT("captureStatus"), TEXT("missing"));
		TArray<TSharedPtr<FJsonValue>> StepRefs;
		StepRefs.Add(MakeShared<FJsonValueObject>(StepRef));
		return StepRefs;
	}

	bool ParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct BuildFixtureProduct(FAutomationTestBase& Test, const FString& ToolId = BaseToolId)
	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product;
		FString FailureReason;
		const bool bBuilt = UnrealMcp::TaskAtlasOfficialCpp::BuildOfficialCppToolsetFiles(
			ToolId,
			TEXT("Official C++ Toolset Test"),
			TEXT("Governed C++ official toolset generated from a fixed automation composite."),
			TaskId,
			TEXT("preview_ready"),
			FString(),
			CriticalPathForFixture(),
			StepRefsForFixture(),
			VisibleCoreToolsForFixture(),
			Product,
			FailureReason);
		Test.TestTrue(TEXT("fixture product builds"), bBuilt);
		if (!FailureReason.IsEmpty())
		{
			Test.AddError(FailureReason);
		}
		return Product;
	}

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile* FindFile(
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct& Product,
		const FString& Suffix)
	{
		for (UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile& File : Product.Files)
		{
			if (File.RelativePath.EndsWith(Suffix, ESearchCase::CaseSensitive))
			{
				return &File;
			}
		}
		return nullptr;
	}

	void RefreshFileHash(UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile& File)
	{
		File.Sha256 = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(File.Contents);
	}

	bool ContainsIssue(const TArray<FString>& Issues, const FString& Needle)
	{
		for (const FString& Issue : Issues)
		{
			if (Issue.Contains(Needle, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	FString DraftDir(const FString& ToolId)
	{
		return FPaths::Combine(UnrealMcp::TaskAtlasService::OfficialToolsetDraftsRootDir(), ToolId);
	}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetEmitValidDraftPureTest,
	"UnrealMcp.OfficialCppToolset.EmitValidDraftPure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialCppToolsetEmitValidDraftPureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this);
	if (Product.Files.Num() == 0)
	{
		return false;
	}

	TArray<FString> Issues;
	TestTrue(TEXT("good C++ product validates"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
	if (Issues.Num() > 0)
	{
		AddError(FString::Join(Issues, TEXT("\n")));
	}
	TestEqual(TEXT("generated file count"), Product.Files.Num(), 5);
	TestFalse(TEXT("schemaHash present"), Product.SchemaHash.IsEmpty());
	TestEqual(TEXT("plugin name"), Product.PluginName, TEXT("UEAtelierCodexOfficialCppToolsetCppToolset"));
	TestEqual(TEXT("module name"), Product.ModuleName, Product.PluginName);
	TestEqual(TEXT("class name"), Product.ClassName, TEXT("UEAtelierCodexOfficialCppToolsetCppToolsetDefinition"));
	TestEqual(TEXT("cpp class name"), Product.CppClassName, TEXT("UUEAtelierCodexOfficialCppToolsetCppToolsetDefinition"));
	TestTrue(TEXT("one generated function"), Product.Tools.Num() == 1);

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile* ClassCpp = FindFile(Product, Product.ClassName + TEXT(".cpp"));
	TestTrue(TEXT("class cpp emitted"), ClassCpp != nullptr);
	if (ClassCpp)
	{
		TestTrue(TEXT("delegates through CallTool"), ClassCpp->Contents.Contains(TEXT("UUnrealMcpCallToolLibrary::CallTool(TEXT(\"unreal.editor_status\"), ArgsJson)")));
		TestFalse(TEXT("no direct editor API"), ClassCpp->Contents.Contains(TEXT("GEditor")) || ClassCpp->Contents.Contains(TEXT("EditorLevelLibrary")));
	}

	TSharedPtr<FJsonObject> Manifest;
	TestTrue(TEXT("manifest parses"), ParseJsonObject(Product.ManifestJson, Manifest));
	if (Manifest.IsValid())
	{
		FString Variant;
		FString RegistrationState;
		bool bBuildRequired = false;
		bool bRestartRequired = false;
		Manifest->TryGetStringField(TEXT("variant"), Variant);
		Manifest->TryGetBoolField(TEXT("buildRequired"), bBuildRequired);
		Manifest->TryGetBoolField(TEXT("restartRequired"), bRestartRequired);
		const TSharedPtr<FJsonObject>* RegistrationStatus = nullptr;
		TestTrue(TEXT("registration status object"), Manifest->TryGetObjectField(TEXT("registrationStatus"), RegistrationStatus) && RegistrationStatus && (*RegistrationStatus).IsValid());
		if (RegistrationStatus && (*RegistrationStatus).IsValid())
		{
			(*RegistrationStatus)->TryGetStringField(TEXT("state"), RegistrationState);
		}
		TestEqual(TEXT("manifest variant"), Variant, TEXT("cpp"));
		TestTrue(TEXT("build required"), bBuildRequired);
		TestTrue(TEXT("restart required"), bRestartRequired);
		TestEqual(TEXT("requires build restart"), RegistrationState, TEXT("requires_build_restart"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetValidatorRejectsBadProductTest,
	"UnrealMcp.OfficialCppToolset.ValidatorRejectsBadProduct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpOfficialCppToolsetValidatorRejectsBadProductTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this, TEXT("codex_cpp_extra_file"));
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile Extra;
		Extra.RelativePath = FPaths::Combine(TEXT("cpp"), Product.PluginName, TEXT("Source"), Product.ModuleName, TEXT("Private"), TEXT("Unexpected.cpp"));
		Extra.Contents = TEXT("// unexpected\n");
		RefreshFileHash(Extra);
		Extra.bSourceFile = true;
		Product.Files.Add(Extra);
		TArray<FString> Issues;
		TestFalse(TEXT("extra file rejected"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
		TestTrue(TEXT("extra file issue"), ContainsIssue(Issues, TEXT("Unexpected generated file")));
	}
	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this, TEXT("codex_cpp_bad_descriptor"));
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile* Descriptor = FindFile(Product, Product.PluginName + TEXT(".uplugin"));
		TestTrue(TEXT("descriptor found"), Descriptor != nullptr);
		if (Descriptor)
		{
			Descriptor->Contents = Descriptor->Contents.Replace(TEXT("\"UnrealMcp\""), TEXT("\"PythonScriptPlugin\""));
			RefreshFileHash(*Descriptor);
		}
		TArray<FString> Issues;
		TestFalse(TEXT("descriptor dependency rejected"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
		TestTrue(TEXT("dependency issue"), ContainsIssue(Issues, TEXT("missing required plugin dependencies")));
	}
	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this, TEXT("codex_cpp_bad_ufunction"));
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile* Header = FindFile(Product, Product.ClassName + TEXT(".h"));
		TestTrue(TEXT("header found"), Header != nullptr);
		if (Header)
		{
			Header->Contents = Header->Contents.Replace(TEXT("UFUNCTION("), TEXT("/* missing reflected function */ ("));
			RefreshFileHash(*Header);
		}
		TArray<FString> Issues;
		TestFalse(TEXT("ufunction count rejected"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
		TestTrue(TEXT("ufunction issue"), ContainsIssue(Issues, TEXT("UFUNCTION count")));
	}
	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this, TEXT("codex_cpp_editor_api"));
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetFile* ClassCpp = FindFile(Product, Product.ClassName + TEXT(".cpp"));
		TestTrue(TEXT("class cpp found"), ClassCpp != nullptr);
		if (ClassCpp)
		{
			ClassCpp->Contents += TEXT("\nvoid BadEditorMutation() { GEditor->GetEditorWorldContext(); }\n");
			RefreshFileHash(*ClassCpp);
		}
		TArray<FString> Issues;
		TestFalse(TEXT("editor api rejected"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
		TestTrue(TEXT("editor api issue"), ContainsIssue(Issues, TEXT("GEditor")));
	}
	{
		UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product = BuildFixtureProduct(*this, TEXT("codex_cpp_manifest_hash"));
		Product.ManifestJson = Product.ManifestJson.Replace(*Product.SchemaHash, TEXT("bad-schema-hash"));
		TArray<FString> Issues;
		TestFalse(TEXT("manifest hash rejected"), UnrealMcp::TaskAtlasOfficialCpp::ValidateOfficialCppToolsetFiles(Product, Issues));
		TestTrue(TEXT("schema hash issue"), ContainsIssue(Issues, TEXT("schemaHash")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetWriterRejectsCollisionTest,
	"UnrealMcp.OfficialCppToolset.WriterRejectsCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialCppToolsetWriterRejectsCollisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	const FString ToolId = FString(TEXT("codex_cpp_collision_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
	IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
	};

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftRequest Request;
	Request.ToolId = ToolId;
	Request.Title = TEXT("Official C++ Collision Test");
	Request.Description = TEXT("Collision test draft.");
	Request.TaskId = TaskId;
	Request.CriticalPath = CriticalPathForFixture();
	Request.StepRefs = StepRefsForFixture();
	Request.VisibleCoreToolNames = VisibleCoreToolsForFixture();

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftResult FirstResult =
		UnrealMcp::TaskAtlasOfficialCpp::GenerateOfficialCppToolsetDraft(Request);
	TestTrue(TEXT("first draft succeeds"), FirstResult.bSucceeded);
	if (!FirstResult.ErrorMessage.IsEmpty())
	{
		AddError(FirstResult.ErrorMessage);
	}
	TestTrue(TEXT("plugin descriptor path written"), FPaths::FileExists(FirstResult.PluginDescriptorPath));
	TestTrue(TEXT("manifest path written"), FPaths::FileExists(FirstResult.ManifestPath));
	TestEqual(TEXT("registration state"), FirstResult.RegistrationState, TEXT("requires_build_restart"));
	TestTrue(TEXT("build required"), FirstResult.bBuildRequired);
	TestTrue(TEXT("restart required"), FirstResult.bRestartRequired);

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftResult SecondResult =
		UnrealMcp::TaskAtlasOfficialCpp::GenerateOfficialCppToolsetDraft(Request);
	TestFalse(TEXT("second draft rejected"), SecondResult.bSucceeded);
	TestEqual(TEXT("collision error"), SecondResult.ErrorCode, TEXT("collision_existing_target"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetDriftDetectorTest,
	"UnrealMcp.OfficialCppToolset.DriftDetector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialCppToolsetDriftDetectorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	const FString ToolId = FString(TEXT("codex_cpp_drift_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
	IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
	};

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftRequest Request;
	Request.ToolId = ToolId;
	Request.Title = TEXT("Official C++ Drift Test");
	Request.Description = TEXT("Drift detector test draft.");
	Request.TaskId = TaskId;
	Request.CriticalPath = CriticalPathForFixture();
	Request.StepRefs = StepRefsForFixture();
	Request.VisibleCoreToolNames = VisibleCoreToolsForFixture();

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftResult DraftResult =
		UnrealMcp::TaskAtlasOfficialCpp::GenerateOfficialCppToolsetDraft(Request);
	TestTrue(TEXT("draft succeeds"), DraftResult.bSucceeded);
	if (!DraftResult.bSucceeded)
	{
		AddError(DraftResult.ErrorMessage);
		return false;
	}

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDriftResult Clean =
		UnrealMcp::TaskAtlasOfficialCpp::DetectOfficialCppToolsetDraftDrift(DraftResult.GeneratedDir);
	TestTrue(TEXT("fresh draft is clean"), Clean.bClean);
	if (!Clean.bClean)
	{
		AddError(FString::Join(Clean.Drifts, TEXT("\n")));
	}

	TestTrue(TEXT("draft has source files"), DraftResult.SourceFiles.Num() > 0);
	if (DraftResult.SourceFiles.Num() == 0)
	{
		return false;
	}
	const FString TamperTarget = DraftResult.SourceFiles[0];
	FString TamperContents;
	TestTrue(TEXT("tamper target readable"), FFileHelper::LoadFileToString(TamperContents, *TamperTarget));
	TamperContents += TEXT("\n// post-publish tamper\n");
	TestTrue(TEXT("tamper write ok"), FFileHelper::SaveStringToFile(TamperContents, *TamperTarget));

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDriftResult Tampered =
		UnrealMcp::TaskAtlasOfficialCpp::DetectOfficialCppToolsetDraftDrift(DraftResult.GeneratedDir);
	TestFalse(TEXT("tampered draft is not clean"), Tampered.bClean);
	bool bSawHashDrift = false;
	for (const FString& Drift : Tampered.Drifts)
	{
		bSawHashDrift |= Drift.StartsWith(TEXT("file_hash_drift:"));
	}
	TestTrue(TEXT("hash drift reported"), bSawHashDrift);

	TestTrue(TEXT("manifest delete ok"), IFileManager::Get().Delete(*DraftResult.ManifestPath, false, true));
	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDriftResult NoManifest =
		UnrealMcp::TaskAtlasOfficialCpp::DetectOfficialCppToolsetDraftDrift(DraftResult.GeneratedDir);
	TestFalse(TEXT("missing manifest is not clean"), NoManifest.bClean);
	TestTrue(TEXT("missing manifest reported"), NoManifest.Drifts.Num() == 1 && NoManifest.Drifts[0].StartsWith(TEXT("manifest_missing:")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetInvalidToolIdRejectedTest,
	"UnrealMcp.OfficialCppToolset.InvalidToolIdRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpOfficialCppToolsetInvalidToolIdRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Product;
	FString FailureReason;
	TestFalse(
		TEXT("invalid build id rejected"),
		UnrealMcp::TaskAtlasOfficialCpp::BuildOfficialCppToolsetFiles(
			TEXT("../bad"),
			TEXT("Bad"),
			FString(),
			TaskId,
			TEXT("preview_ready"),
			FString(),
			CriticalPathForFixture(),
			StepRefsForFixture(),
			VisibleCoreToolsForFixture(),
			Product,
			FailureReason));
	TestFalse(TEXT("invalid build reason"), FailureReason.IsEmpty());

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftRequest Request;
	Request.ToolId = TEXT("bad/name");
	Request.Title = TEXT("Bad");
	Request.TaskId = TaskId;
	Request.CriticalPath = CriticalPathForFixture();
	Request.VisibleCoreToolNames = VisibleCoreToolsForFixture();
	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftResult Result =
		UnrealMcp::TaskAtlasOfficialCpp::GenerateOfficialCppToolsetDraft(Request);
	TestFalse(TEXT("invalid writer id rejected"), Result.bSucceeded);
	TestEqual(TEXT("invalid writer error"), Result.ErrorCode, TEXT("invalid_name"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetReemitHashDeterminismTest,
	"UnrealMcp.OfficialCppToolset.ReemitHashDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpOfficialCppToolsetReemitHashDeterminismTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct First = BuildFixtureProduct(*this, TEXT("codex_cpp_deterministic"));
	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetBuildProduct Second = BuildFixtureProduct(*this, TEXT("codex_cpp_deterministic"));
	TestEqual(TEXT("schema hash deterministic"), First.SchemaHash, Second.SchemaHash);
	TestFalse(TEXT("schema hash present"), First.SchemaHash.IsEmpty());
	TestEqual(TEXT("manifest deterministic"), First.ManifestJson, Second.ManifestJson);
	TestEqual(TEXT("file count deterministic"), First.Files.Num(), Second.Files.Num());
	if (First.Files.Num() != Second.Files.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < First.Files.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("relative path deterministic %d"), Index), First.Files[Index].RelativePath, Second.Files[Index].RelativePath);
		TestEqual(FString::Printf(TEXT("hash deterministic %d"), Index), First.Files[Index].Sha256, Second.Files[Index].Sha256);
		TestEqual(FString::Printf(TEXT("contents deterministic %d"), Index), First.Files[Index].Contents, Second.Files[Index].Contents);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetBuildBlockedByEditorTest,
	"UnrealMcp.OfficialCppToolset.BuildBlockedByEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialCppToolsetBuildBlockedByEditorTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UnrealMcp::TaskAtlasService::SetOfficialCppBuildEditorRunningForTests(true);
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearOfficialCppBuildEditorRunningForTests();
	};

	const UnrealMcp::TaskAtlasService::FOfficialCppToolsetBuildDraftResult Result =
		UnrealMcp::TaskAtlasService::BuildOfficialCppToolsetDraft(TEXT("codex_cpp_editor_gate"));
	TestFalse(TEXT("build blocked is not success"), Result.bSucceeded);
	TestEqual(TEXT("blocked status"), Result.BuildStatus, TEXT("buildBlockedByOpenEditor"));
	TestEqual(TEXT("blocked code"), Result.ErrorCode, TEXT("editor_open"));
	TestTrue(TEXT("blocked instructions mention close"), Result.Instructions.Contains(TEXT("Close every UnrealEditor process")));
	TestTrue(TEXT("no mount during blocked gate"), Result.MountedPluginDir.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpOfficialCppToolsetBuildCleanupFailSafeTest,
	"UnrealMcp.OfficialCppToolset.BuildCleanupFailSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpOfficialCppToolsetBuildCleanupFailSafeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpOfficialCppToolsetEmitterTests;

	const FString ToolId = FString(TEXT("codex_cpp_build_cleanup_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
	const FString ExampleRoot = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("UnrealMcpOfficialCppBuildTests"),
		ToolId,
		TEXT("UEvolveExample58"));
	IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
	IFileManager::Get().DeleteDirectory(*ExampleRoot, false, true);
	ON_SCOPE_EXIT
	{
		UnrealMcp::TaskAtlasService::ClearOfficialCppBuildEditorRunningForTests();
		UnrealMcp::TaskAtlasService::ClearOfficialCppBuildExampleRootForTests();
		IFileManager::Get().DeleteDirectory(*DraftDir(ToolId), false, true);
		IFileManager::Get().DeleteDirectory(*ExampleRoot, false, true);
	};

	UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftRequest Request;
	Request.ToolId = ToolId;
	Request.Title = TEXT("Official C++ Build Cleanup Test");
	Request.Description = TEXT("Build cleanup test draft.");
	Request.TaskId = TaskId;
	Request.CriticalPath = CriticalPathForFixture();
	Request.StepRefs = StepRefsForFixture();
	Request.VisibleCoreToolNames = VisibleCoreToolsForFixture();
	const UnrealMcp::TaskAtlasOfficialCpp::FOfficialCppToolsetDraftResult DraftResult =
		UnrealMcp::TaskAtlasOfficialCpp::GenerateOfficialCppToolsetDraft(Request);
	TestTrue(TEXT("draft succeeds"), DraftResult.bSucceeded);
	if (!DraftResult.bSucceeded)
	{
		AddError(DraftResult.ErrorMessage);
		return false;
	}
	TestTrue(TEXT("saved draft exists before build"), IFileManager::Get().DirectoryExists(*DraftResult.GeneratedDir));

	UnrealMcp::TaskAtlasService::SetOfficialCppBuildEditorRunningForTests(false);
	UnrealMcp::TaskAtlasService::SetOfficialCppBuildExampleRootForTests(ExampleRoot);
	UnrealMcp::TaskAtlasService::FailNextOfficialCppBuildAfterCopyForTests();
	const UnrealMcp::TaskAtlasService::FOfficialCppToolsetBuildDraftResult BuildResult =
		UnrealMcp::TaskAtlasService::BuildOfficialCppToolsetDraft(ToolId);
	TestFalse(TEXT("forced build failure is not success"), BuildResult.bSucceeded);
	TestEqual(TEXT("forced build status"), BuildResult.BuildStatus, TEXT("failed"));
	TestEqual(TEXT("forced build code"), BuildResult.ErrorCode, TEXT("forced_build_failure_for_tests"));
	TestTrue(TEXT("mounted copy removed"), BuildResult.bMountedCopyRemoved);
	TestFalse(TEXT("mounted copy gone"), IFileManager::Get().DirectoryExists(*BuildResult.MountedPluginDir));
	TestTrue(TEXT("saved draft remains canonical"), IFileManager::Get().DirectoryExists(*DraftResult.GeneratedDir));

	TSharedPtr<FJsonObject> Manifest;
	TestTrue(TEXT("updated manifest parses"), ParseJsonObject(BuildResult.ManifestJson, Manifest));
	if (Manifest.IsValid())
	{
		bool bRestartRequired = false;
		Manifest->TryGetBoolField(TEXT("restartRequired"), bRestartRequired);
		TestTrue(TEXT("restart still required"), bRestartRequired);
		const TSharedPtr<FJsonObject>* BuildStatus = nullptr;
		TestTrue(TEXT("buildStatus object"), Manifest->TryGetObjectField(TEXT("buildStatus"), BuildStatus) && BuildStatus && (*BuildStatus).IsValid());
		if (BuildStatus && (*BuildStatus).IsValid())
		{
			FString State;
			(*BuildStatus)->TryGetStringField(TEXT("state"), State);
			TestEqual(TEXT("manifest build failed"), State, TEXT("failed"));
		}
		const TSharedPtr<FJsonObject>* RegistrationStatus = nullptr;
		TestTrue(TEXT("registrationStatus object"), Manifest->TryGetObjectField(TEXT("registrationStatus"), RegistrationStatus) && RegistrationStatus && (*RegistrationStatus).IsValid());
		if (RegistrationStatus && (*RegistrationStatus).IsValid())
		{
			FString State;
			(*RegistrationStatus)->TryGetStringField(TEXT("state"), State);
			TestEqual(TEXT("registration remains build restart"), State, TEXT("requires_build_restart"));
		}
	}
	return true;
}

#endif
