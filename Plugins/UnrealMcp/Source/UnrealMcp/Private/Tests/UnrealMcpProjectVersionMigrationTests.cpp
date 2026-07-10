#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMcpEditorTools.h"
#include "UnrealMcpModule.h"

namespace UnrealMcpProjectVersionMigrationTests
{
	FString MakeRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/Tests/ProjectVersionMigration")));
	}

	FString MakeProjectPath()
	{
		return FPaths::Combine(MakeRoot(), TEXT("MigrationFixture.uproject"));
	}

	bool WriteFixture(const FString& EngineAssociation)
	{
		IFileManager::Get().MakeDirectory(*MakeRoot(), true);
		const FString Json = FString::Printf(
			TEXT("{\n")
			TEXT("  \"FileVersion\": 3,\n")
			TEXT("  \"EngineAssociation\": \"%s\",\n")
			TEXT("  \"Description\": \"migration-sentinel\",\n")
			TEXT("  \"Modules\": [{\"Name\": \"MigrationFixture\", \"Type\": \"Runtime\"}]\n")
			TEXT("}\n"),
			*EngineAssociation);
		return FFileHelper::SaveStringToFile(Json, *MakeProjectPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FUnrealMcpExecutionResult RunMigration(const FString& TargetVersion, bool bDryRun)
	{
		FJsonObject Args;
		Args.SetStringField(TEXT("targetEngineVersion"), TargetVersion);
		Args.SetStringField(TEXT("projectFilePath"), MakeProjectPath());
		Args.SetBoolField(TEXT("dryRun"), bDryRun);
		FUnrealMcpExecutionResult Result;
		if (!UnrealMcp::TryExecuteEditorTool(TEXT("unreal.project_version_migration"), Args, Result))
		{
			Result.bIsError = true;
		}
		return Result;
	}

	FString StructuredString(const FUnrealMcpExecutionResult& Result, const TCHAR* FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool WarningContains(const FUnrealMcpExecutionResult& Result, const FString& Expected)
	{
		const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
		if (!Result.StructuredContent.IsValid()
			|| !Result.StructuredContent->TryGetArrayField(TEXT("compatibilityWarnings"), Warnings)
			|| !Warnings)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Warning : *Warnings)
		{
			FString Text;
			if (Warning.IsValid() && Warning->TryGetString(Text) && Text.Contains(Expected, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool LoadFixture(TSharedPtr<FJsonObject>& OutObject)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *MakeProjectPath()))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProjectVersionMigrationSupportTest,
	"UnrealMcp.Editor.ProjectVersionMigration.SupportContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProjectVersionMigrationSupportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpProjectVersionMigrationTests;

	IFileManager::Get().DeleteDirectory(*MakeRoot(), false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*MakeRoot(), false, true);
	};
	TestTrue(TEXT("Migration fixture is written."), WriteFixture(TEXT("5.7")));

	const FUnrealMcpExecutionResult DryRun58 = RunMigration(TEXT("5.8"), true);
	TestFalse(TEXT("UE 5.8 dry run succeeds."), DryRun58.bIsError);
	TestEqual(TEXT("UE 5.8 is a primary target."), StructuredString(DryRun58, TEXT("targetSupportTier")), FString(TEXT("primary")));

	const FUnrealMcpExecutionResult DryRun56 = RunMigration(TEXT("5.6"), true);
	TestFalse(TEXT("UE 5.6 maintenance dry run remains accepted."), DryRun56.bIsError);
	TestEqual(TEXT("UE 5.6 is maintenance tier."), StructuredString(DryRun56, TEXT("targetSupportTier")), FString(TEXT("maintenance")));
	TestTrue(TEXT("UE 5.6 returns a maintenance warning."), WarningContains(DryRun56, TEXT("maintenance")));

	const FUnrealMcpExecutionResult Invalid59 = RunMigration(TEXT("5.9"), true);
	TestTrue(TEXT("Unverified UE 5.9 is rejected."), Invalid59.bIsError);

	const FUnrealMcpExecutionResult Apply58 = RunMigration(TEXT("5.8"), false);
	TestFalse(TEXT("UE 5.8 migration write succeeds."), Apply58.bIsError);
	TSharedPtr<FJsonObject> WrittenProject;
	TestTrue(TEXT("Migrated fixture remains valid JSON."), LoadFixture(WrittenProject));
	if (WrittenProject.IsValid())
	{
		TestEqual(TEXT("Only requested EngineAssociation is present."), WrittenProject->GetStringField(TEXT("EngineAssociation")), FString(TEXT("5.8")));
		TestEqual(TEXT("Unrelated fixture content is preserved."), WrittenProject->GetStringField(TEXT("Description")), FString(TEXT("migration-sentinel")));
	}
	return true;
}

#endif
