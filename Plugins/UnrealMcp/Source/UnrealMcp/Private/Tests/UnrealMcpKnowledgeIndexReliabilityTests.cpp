#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "UnrealMcpKnowledgeBridge.h"
#include "UnrealMcpSelfExtensionTools.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <unistd.h>
#endif

namespace UnrealMcpKnowledgeIndexReliabilityTests
{
	const FString Sentinel = TEXT("UEATELIER_RAG_LAST_KNOWN_GOOD_SENTINEL");

	FString MakeTestRoot(const FString& TestName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/Tests/KnowledgeIndexReliability"),
			TestName));
	}

	FString SourcePathForRoot(const FString& TestRoot)
	{
		return FPaths::Combine(TestRoot, TEXT("KnowledgeSources/TaskAtlas/reliability.md"));
	}

	FString IndexRootForRoot(const FString& TestRoot)
	{
		return FPaths::Combine(TestRoot, TEXT("KnowledgeIndex"));
	}

	void ConfigureIsolatedRefresh(FJsonObject& Args, const FString& TestRoot)
	{
		Args.SetStringField(TEXT("sourceRoot"), FPaths::GetPath(SourcePathForRoot(TestRoot)));
		Args.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
		Args.SetBoolField(TEXT("includeOfficialDocs"), false);
		Args.SetBoolField(TEXT("includePromotedSources"), true);
		Args.SetBoolField(TEXT("includeVersionedDocs"), false);
		Args.SetBoolField(TEXT("includeToolRegistry"), false);
		Args.SetBoolField(TEXT("includeActivityLog"), false);
		Args.SetBoolField(TEXT("includeSkills"), false);
		Args.SetBoolField(TEXT("allowEmptyIndex"), false);
		Args.SetNumberField(TEXT("maxCards"), 50.0);
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

	double StructuredNumber(const FUnrealMcpExecutionResult& Result, const TCHAR* FieldName, double DefaultValue = -1.0)
	{
		double Value = DefaultValue;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}

	bool CreateKnowledgeSymlink(const FString& Target, const FString& Link)
	{
#if PLATFORM_MAC || PLATFORM_LINUX
		FTCHARToUTF8 TargetUtf8(*Target);
		FTCHARToUTF8 LinkUtf8(*Link);
		return symlink(TargetUtf8.Get(), LinkUtf8.Get()) == 0;
#else
		(void)Target;
		(void)Link;
		return false;
#endif
	}

	void DeleteKnowledgeSymlink(const FString& Link)
	{
#if PLATFORM_MAC || PLATFORM_LINUX
		FTCHARToUTF8 LinkUtf8(*Link);
		unlink(LinkUtf8.Get());
#else
		(void)Link;
#endif
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeIndexPreservesLastKnownGoodTest,
	"UnrealMcp.Knowledge.IndexReliability.EmptyRefreshPreservesLastKnownGood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeIndexPreservesLastKnownGoodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;

	const FString TestRoot = MakeTestRoot(TEXT("preserve_last_known_good"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString SourcePath = SourcePathForRoot(TestRoot);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	const FString Markdown = FString::Printf(
		TEXT("# Reliability fixture\n\nSentinel: %s\n"),
		*Sentinel);
	TestTrue(
		TEXT("Synthetic knowledge source is written."),
		FFileHelper::SaveStringToFile(Markdown, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	const FUnrealMcpExecutionResult InitialRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestFalse(TEXT("Initial isolated refresh succeeds."), InitialRefresh.bIsError);
	TestEqual(TEXT("Initial index status is ready."), StructuredString(InitialRefresh, TEXT("indexStatus")), FString(TEXT("ready")));

	const FString CardsPath = FPaths::Combine(IndexRootForRoot(TestRoot), TEXT("cards.jsonl"));
	FString OriginalCards;
	TestTrue(TEXT("Initial cards.jsonl is readable."), FFileHelper::LoadFileToString(OriginalCards, *CardsPath));
	TestFalse(TEXT("Initial cards.jsonl is not empty."), OriginalCards.IsEmpty());

	TestTrue(TEXT("Synthetic source is deleted before the empty refresh."), IFileManager::Get().Delete(*SourcePath, false, true, true));
	const FUnrealMcpExecutionResult EmptyRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestTrue(TEXT("Empty refresh fails closed."), EmptyRefresh.bIsError);
	TestEqual(TEXT("Empty refresh reports an empty candidate index."), StructuredString(EmptyRefresh, TEXT("indexStatus")), FString(TEXT("empty")));
	TestTrue(
		TEXT("Empty refresh reports that the existing index was preserved."),
		EmptyRefresh.StructuredContent.IsValid()
			&& EmptyRefresh.StructuredContent->GetBoolField(TEXT("preservedExistingIndex")));

	FString PreservedCards;
	TestTrue(TEXT("Preserved cards.jsonl remains readable."), FFileHelper::LoadFileToString(PreservedCards, *CardsPath));
	TestEqual(TEXT("Preserved cards are byte-identical."), PreservedCards, OriginalCards);

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), Sentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	const FUnrealMcpExecutionResult SearchResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestFalse(TEXT("Last-known-good index remains searchable."), SearchResult.bIsError);
	TestEqual(TEXT("Deleted source makes the preserved index stale."), StructuredString(SearchResult, TEXT("indexStatus")), FString(TEXT("stale")));
	TestTrue(
		TEXT("Search still returns the preserved sentinel card."),
		SearchResult.StructuredContent.IsValid()
			&& SearchResult.StructuredContent->GetNumberField(TEXT("resultCount")) > 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeIndexMachineStatusTest,
	"UnrealMcp.Knowledge.IndexReliability.MachineStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeIndexMachineStatusTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;

	const FString TestRoot = MakeTestRoot(TEXT("machine_status"));
	const FString IndexRoot = IndexRootForRoot(TestRoot);
	const FString CardsPath = FPaths::Combine(IndexRoot, TEXT("cards.jsonl"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), Sentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexRoot);

	const FUnrealMcpExecutionResult MissingResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestTrue(TEXT("Missing index search fails."), MissingResult.bIsError);
	TestEqual(TEXT("Missing index has machine-readable status."), StructuredString(MissingResult, TEXT("indexStatus")), FString(TEXT("missing")));

	IFileManager::Get().MakeDirectory(*IndexRoot, true);
	TestTrue(
		TEXT("Empty cards.jsonl is written."),
		FFileHelper::SaveStringToFile(FString(), *CardsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const FUnrealMcpExecutionResult EmptyResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestTrue(TEXT("Empty index search fails."), EmptyResult.bIsError);
	TestEqual(TEXT("Empty index has machine-readable status."), StructuredString(EmptyResult, TEXT("indexStatus")), FString(TEXT("empty")));

	TestTrue(
		TEXT("Invalid cards.jsonl is written."),
		FFileHelper::SaveStringToFile(TEXT("not-json\n"), *CardsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const FUnrealMcpExecutionResult CorruptResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestTrue(TEXT("Corrupt index search fails."), CorruptResult.bIsError);
	TestEqual(TEXT("Corrupt index has machine-readable status."), StructuredString(CorruptResult, TEXT("indexStatus")), FString(TEXT("corrupt")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeIndexIntegrityTest,
	"UnrealMcp.Knowledge.IndexReliability.IntegrityHash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeIndexIntegrityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;

	const FString TestRoot = MakeTestRoot(TEXT("integrity_hash"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString SourcePath = SourcePathForRoot(TestRoot);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	TestTrue(
		TEXT("Integrity source is written."),
		FFileHelper::SaveStringToFile(
			FString::Printf(TEXT("# Integrity fixture\n\n%s\n"), *Sentinel),
			*SourcePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	const FUnrealMcpExecutionResult RefreshResult = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestFalse(TEXT("Integrity fixture refresh succeeds."), RefreshResult.bIsError);

	const FString CardsPath = FPaths::Combine(IndexRootForRoot(TestRoot), TEXT("cards.jsonl"));
	FString CardsText;
	TestTrue(TEXT("Integrity cards are readable."), FFileHelper::LoadFileToString(CardsText, *CardsPath));
	CardsText += LINE_TERMINATOR;
	TestTrue(
		TEXT("Integrity cards are tampered without updating the manifest."),
		FFileHelper::SaveStringToFile(CardsText, *CardsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), Sentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	const FUnrealMcpExecutionResult SearchResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestTrue(TEXT("Tampered cards fail closed."), SearchResult.bIsError);
	TestEqual(TEXT("Hash mismatch reports corrupt."), StructuredString(SearchResult, TEXT("indexStatus")), FString(TEXT("corrupt")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeOutcomeAppendIntegrityTest,
	"UnrealMcp.Knowledge.IndexReliability.OutcomeAppendPreservesManifestIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeOutcomeAppendIntegrityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;

	const FString TestRoot = MakeTestRoot(TEXT("outcome_append_integrity"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString SourcePath = SourcePathForRoot(TestRoot);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	TestTrue(
		TEXT("Outcome-append source is written."),
		FFileHelper::SaveStringToFile(
			TEXT("# Outcome append fixture\n\nBaseline source.\n"),
			*SourcePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	TestFalse(TEXT("Outcome-append fixture refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);

	const FString OutcomeSentinel = TEXT("UEATELIER_RAG_OUTCOME_APPEND_SENTINEL");
	FString FailureReason;
	TestTrue(
		TEXT("Outcome card append succeeds."),
		UnrealMcp::WriteOutcomeKnowledgeCard(
			TEXT("outcome-append-test"),
			TEXT("Verified outcome"),
			OutcomeSentinel,
			SourcePath,
			{ TEXT("outcome"), TEXT("verification") },
			FailureReason,
			IndexRootForRoot(TestRoot)));
	if (!FailureReason.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Outcome append failure: %s"), *FailureReason));
	}

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), OutcomeSentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	const FUnrealMcpExecutionResult SearchResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestFalse(TEXT("Outcome append leaves the index searchable."), SearchResult.bIsError);
	TestEqual(TEXT("Outcome append preserves a ready manifest."), StructuredString(SearchResult, TEXT("indexStatus")), FString(TEXT("ready")));
	TestTrue(
		TEXT("Outcome card is returned after the manifest hash check."),
		SearchResult.StructuredContent.IsValid()
			&& SearchResult.StructuredContent->GetNumberField(TEXT("resultCount")) > 0.0);

	const FString ManifestPath = FPaths::Combine(IndexRootForRoot(TestRoot), TEXT("index.json"));
	FString ManifestText;
	TestTrue(TEXT("Outcome-append manifest remains readable."), FFileHelper::LoadFileToString(ManifestText, *ManifestPath));
	TestTrue(TEXT("Outcome append records its mutation kind."), ManifestText.Contains(TEXT("\"lastMutation\":\"outcome_append\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeIndexInterruptedWriteRecoveryTest,
	"UnrealMcp.Knowledge.IndexReliability.InterruptedWriteRestoresVerifiedBackup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeIndexInterruptedWriteRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;
	const FString TestRoot = MakeTestRoot(TEXT("interrupted_write_recovery"));
	const FString IndexRoot = IndexRootForRoot(TestRoot);
	const FString CardsPath = FPaths::Combine(IndexRoot, TEXT("cards.jsonl"));
	const FString ManifestPath = FPaths::Combine(IndexRoot, TEXT("index.json"));
	const FString CardsBackupPath = CardsPath + TEXT(".bak");
	const FString ManifestBackupPath = ManifestPath + TEXT(".bak");
	const FString OrphanTempPath = CardsPath + TEXT(".tmp.orphan");
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		UnrealMcp::SetKnowledgeIndexWriteInterruptionStageForTests(0);
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString SourcePath = SourcePathForRoot(TestRoot);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	TestTrue(TEXT("Initial recovery source is written."), FFileHelper::SaveStringToFile(
		FString::Printf(TEXT("# Recovery fixture\n\n%s\n"), *Sentinel),
		*SourcePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	TestFalse(TEXT("Initial recovery refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);
	FString OriginalCards;
	FString OriginalManifest;
	TestTrue(TEXT("Original recovery cards are readable."), FFileHelper::LoadFileToString(OriginalCards, *CardsPath));
	TestTrue(TEXT("Original recovery manifest is readable."), FFileHelper::LoadFileToString(OriginalManifest, *ManifestPath));

	TestTrue(TEXT("Replacement recovery source is written."), FFileHelper::SaveStringToFile(
		TEXT("# Replacement fixture\n\nUEATELIER_RAG_REPLACEMENT_SENTINEL\n"),
		*SourcePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	UnrealMcp::SetKnowledgeIndexWriteInterruptionStageForTests(1);
	const FUnrealMcpExecutionResult InterruptedRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestTrue(TEXT("Injected interruption fails the refresh."), InterruptedRefresh.bIsError);
	TestTrue(TEXT("Interrupted refresh reports the simulated boundary."), InterruptedRefresh.Text.Contains(TEXT("Simulated knowledge index interruption")));
	TestTrue(TEXT("Cards backup survives the interruption."), FPaths::FileExists(CardsBackupPath));
	TestTrue(TEXT("Manifest backup survives the interruption."), FPaths::FileExists(ManifestBackupPath));
	TestTrue(TEXT("Orphan transaction temp is written."), FFileHelper::SaveStringToFile(TEXT("orphan"), *OrphanTempPath));

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), Sentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexRoot);
	const FUnrealMcpExecutionResult SearchResult = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestFalse(TEXT("Next load restores and searches the verified backup."), SearchResult.bIsError);
	TestEqual(TEXT("Changed source makes restored old index stale."), StructuredString(SearchResult, TEXT("indexStatus")), FString(TEXT("stale")));
	TestTrue(TEXT("Restored old sentinel is returned."), SearchResult.StructuredContent.IsValid()
		&& SearchResult.StructuredContent->GetNumberField(TEXT("resultCount")) > 0.0);

	FString RestoredCards;
	FString RestoredManifest;
	TestTrue(TEXT("Restored cards are readable."), FFileHelper::LoadFileToString(RestoredCards, *CardsPath));
	TestTrue(TEXT("Restored manifest is readable."), FFileHelper::LoadFileToString(RestoredManifest, *ManifestPath));
	TestEqual(TEXT("Restored cards are byte-identical to last-known-good."), RestoredCards, OriginalCards);
	TestEqual(TEXT("Restored manifest is byte-identical to last-known-good."), RestoredManifest, OriginalManifest);
	TestFalse(TEXT("Cards backup is removed after verified recovery."), FPaths::FileExists(CardsBackupPath));
	TestFalse(TEXT("Manifest backup is removed after verified recovery."), FPaths::FileExists(ManifestBackupPath));
	TestFalse(TEXT("Orphan transaction temp is cleaned on load."), FPaths::FileExists(OrphanTempPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeOutcomeAppendRefusalTest,
	"UnrealMcp.Knowledge.IndexReliability.OutcomeAppendRefusesStaleAndCorrupt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeOutcomeAppendRefusalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;
	const FString TestRoot = MakeTestRoot(TEXT("outcome_append_refusal"));
	const FString SourcePath = SourcePathForRoot(TestRoot);
	const FString IndexRoot = IndexRootForRoot(TestRoot);
	const FString CardsPath = FPaths::Combine(IndexRoot, TEXT("cards.jsonl"));
	const FString ManifestPath = FPaths::Combine(IndexRoot, TEXT("index.json"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestRoot, false, true); };

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);
	TestTrue(TEXT("Append-refusal source is written."), FFileHelper::SaveStringToFile(
		TEXT("# Append refusal fixture\n\nBaseline.\n"), *SourcePath));
	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	TestFalse(TEXT("Append-refusal refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);
	FString ReadyCards;
	FString ReadyManifest;
	FFileHelper::LoadFileToString(ReadyCards, *CardsPath);
	FFileHelper::LoadFileToString(ReadyManifest, *ManifestPath);

	TestTrue(TEXT("Source is changed to make the index stale."), FFileHelper::SaveStringToFile(
		TEXT("# Append refusal fixture\n\nChanged after indexing.\n"), *SourcePath));
	FString FailureReason;
	TestFalse(TEXT("Outcome append refuses a stale index."), UnrealMcp::WriteOutcomeKnowledgeCard(
		TEXT("stale-refusal"), TEXT("Stale refusal"), TEXT("Must not append"), SourcePath, {}, FailureReason, IndexRoot));
	TestTrue(TEXT("Stale refusal is explicit."), FailureReason.Contains(TEXT("stale knowledge index")));
	FString AfterStaleCards;
	FString AfterStaleManifest;
	FFileHelper::LoadFileToString(AfterStaleCards, *CardsPath);
	FFileHelper::LoadFileToString(AfterStaleManifest, *ManifestPath);
	TestEqual(TEXT("Stale refusal leaves cards unchanged."), AfterStaleCards, ReadyCards);
	TestEqual(TEXT("Stale refusal leaves manifest unchanged."), AfterStaleManifest, ReadyManifest);

	TestFalse(TEXT("Refresh after source change succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);
	FString CorruptCards;
	FString CorruptManifest;
	FFileHelper::LoadFileToString(CorruptCards, *CardsPath);
	FFileHelper::LoadFileToString(CorruptManifest, *ManifestPath);
	CorruptCards += TEXT("not-json\n");
	TestTrue(TEXT("Cards are corrupted without changing the manifest."), FFileHelper::SaveStringToFile(CorruptCards, *CardsPath));
	FailureReason.Reset();
	TestFalse(TEXT("Outcome append refuses a corrupt index."), UnrealMcp::WriteOutcomeKnowledgeCard(
		TEXT("corrupt-refusal"), TEXT("Corrupt refusal"), TEXT("Must not append"), SourcePath, {}, FailureReason, IndexRoot));
	TestTrue(TEXT("Corrupt refusal reports integrity failure."), !FailureReason.IsEmpty());
	FString AfterCorruptCards;
	FString AfterCorruptManifest;
	FFileHelper::LoadFileToString(AfterCorruptCards, *CardsPath);
	FFileHelper::LoadFileToString(AfterCorruptManifest, *ManifestPath);
	TestEqual(TEXT("Corrupt refusal leaves cards unchanged."), AfterCorruptCards, CorruptCards);
	TestEqual(TEXT("Corrupt refusal leaves manifest unchanged."), AfterCorruptManifest, CorruptManifest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeAllowEmptyIndexGateTest,
	"UnrealMcp.Knowledge.IndexReliability.AllowEmptyIndexRequiresIsolatedTestRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeAllowEmptyIndexGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;
	const FString TestRoot = MakeTestRoot(TEXT("allow_empty_gate"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestRoot, false, true); };

	auto ConfigureEmpty = [](FJsonObject& Args)
	{
		Args.SetBoolField(TEXT("includeOfficialDocs"), false);
		Args.SetBoolField(TEXT("includePromotedSources"), false);
		Args.SetBoolField(TEXT("includeVersionedDocs"), false);
		Args.SetBoolField(TEXT("includeToolRegistry"), false);
		Args.SetBoolField(TEXT("includeActivityLog"), false);
		Args.SetBoolField(TEXT("includeSkills"), false);
		Args.SetBoolField(TEXT("allowEmptyIndex"), true);
	};

	FJsonObject DefaultRootArgs;
	ConfigureEmpty(DefaultRootArgs);
	const FUnrealMcpExecutionResult DefaultRootResult = UnrealMcp::KnowledgeIndexRefresh(DefaultRootArgs);
	TestTrue(TEXT("allowEmptyIndex rejects an implicit production root."), DefaultRootResult.bIsError);
	TestEqual(TEXT("Implicit-root rejection identifies allowEmptyIndex."), StructuredString(DefaultRootResult, TEXT("rejectedField")), FString(TEXT("allowEmptyIndex")));

	FJsonObject ProductionRootArgs;
	ConfigureEmpty(ProductionRootArgs);
	ProductionRootArgs.SetStringField(TEXT("indexRoot"), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/KnowledgeIndex-empty-test")));
	const FUnrealMcpExecutionResult ProductionRootResult = UnrealMcp::KnowledgeIndexRefresh(ProductionRootArgs);
	TestTrue(TEXT("allowEmptyIndex rejects an explicit non-test root."), ProductionRootResult.bIsError);

	FJsonObject IsolatedArgs;
	ConfigureEmpty(IsolatedArgs);
	IsolatedArgs.SetStringField(TEXT("sourceRoot"), FPaths::Combine(TestRoot, TEXT("empty-source")));
	IsolatedArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	const FUnrealMcpExecutionResult IsolatedResult = UnrealMcp::KnowledgeIndexRefresh(IsolatedArgs);
	TestFalse(TEXT("allowEmptyIndex accepts an explicit root under Saved/UnrealMcp/Tests."), IsolatedResult.bIsError);
	TestEqual(TEXT("Intentional isolated empty index reports empty."), StructuredString(IsolatedResult, TEXT("indexStatus")), FString(TEXT("empty")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeSavedPathContainmentTest,
	"UnrealMcp.Knowledge.IndexReliability.ProjectSavedPathContainment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeSavedPathContainmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeIndexReliabilityTests;

	const FString TestRoot = MakeTestRoot(TEXT("saved_path_containment"));
	const FString IndexSymlinkRoot = FPaths::Combine(TestRoot, TEXT("leaf_symlink_index"));
	const FString CardsSymlinkPath = FPaths::Combine(IndexSymlinkRoot, TEXT("cards.jsonl"));
	const FString ManifestSymlinkPath = FPaths::Combine(IndexSymlinkRoot, TEXT("index.json"));
	const FString SourceRoot = FPaths::GetPath(SourcePathForRoot(TestRoot));
	const FString SourceDirectorySymlink = FPaths::Combine(SourceRoot, TEXT("linked_outside"));
	const FString DocumentsDirectory = FPaths::Combine(SourceRoot, TEXT("docs"));
	const FString InternalSourceSymlink = FPaths::Combine(SourceRoot, TEXT("linked_inside"));
	const FString TextDirectorySymlink = FPaths::Combine(DocumentsDirectory, TEXT("linked_outside"));
	const FString EvalRoot = FPaths::Combine(TestRoot, TEXT("evals"));
	const FString EvalDirectorySymlink = FPaths::Combine(EvalRoot, TEXT("linked_outside"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);

	const FString OutsideRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../UEAtelierKnowledgeOutside"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	IFileManager::Get().DeleteDirectory(*OutsideRoot, false, true);
	ON_SCOPE_EXIT
	{
		DeleteKnowledgeSymlink(CardsSymlinkPath);
		DeleteKnowledgeSymlink(ManifestSymlinkPath);
		DeleteKnowledgeSymlink(SourceDirectorySymlink);
		DeleteKnowledgeSymlink(InternalSourceSymlink);
		DeleteKnowledgeSymlink(TextDirectorySymlink);
		DeleteKnowledgeSymlink(EvalDirectorySymlink);
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
		IFileManager::Get().DeleteDirectory(*OutsideRoot, false, true);
	};
	TestFalse(TEXT("Outside containment fixture does not pre-exist."), FPaths::DirectoryExists(OutsideRoot));
	auto TestRejectedField = [this](const FString& Label, const FUnrealMcpExecutionResult& Result, const FString& ExpectedField)
	{
		TestTrue(Label + TEXT(" is an error."), Result.bIsError);
		TestEqual(Label + TEXT(" identifies the rejected field."), StructuredString(Result, TEXT("rejectedField")), ExpectedField);
	};

	FJsonObject RefreshArgs;
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	RefreshArgs.SetStringField(TEXT("indexRoot"), OutsideRoot);
	const FUnrealMcpExecutionResult OutsideIndexRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestRejectedField(TEXT("Refresh rejects an absolute indexRoot outside ProjectSavedDir"), OutsideIndexRefresh, TEXT("indexRoot"));
	TestFalse(TEXT("Rejected refresh does not create the outside indexRoot."), FPaths::DirectoryExists(OutsideRoot));

	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	RefreshArgs.SetStringField(TEXT("sourceRoot"), TEXT("../OutsideKnowledgeSources"));
	const FUnrealMcpExecutionResult OutsideSourceRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestRejectedField(TEXT("Refresh rejects a traversing sourceRoot outside ProjectSavedDir"), OutsideSourceRefresh, TEXT("sourceRoot"));

	FJsonObject SearchArgs;
	SearchArgs.SetStringField(TEXT("query"), Sentinel);
	SearchArgs.SetStringField(TEXT("indexRoot"), OutsideRoot);
	TestRejectedField(TEXT("Search rejects an indexRoot outside ProjectSavedDir"), UnrealMcp::KnowledgeSearch(SearchArgs), TEXT("indexRoot"));

	FJsonObject RecommendArgs;
	RecommendArgs.SetStringField(TEXT("task"), TEXT("knowledge containment test"));
	RecommendArgs.SetStringField(TEXT("indexRoot"), OutsideRoot);
	TArray<TSharedPtr<FJsonValue>> EmptyTools;
	TestRejectedField(TEXT("Tool recommendation rejects an indexRoot outside ProjectSavedDir"), UnrealMcp::ToolRecommend(RecommendArgs, EmptyTools), TEXT("indexRoot"));

	FJsonObject EvalArgs;
	EvalArgs.SetStringField(TEXT("evalPath"), TEXT("Tools/UnrealMcpKnowledge/Evals"));
	EvalArgs.SetStringField(TEXT("indexRoot"), OutsideRoot);
	EvalArgs.SetBoolField(TEXT("refreshIndex"), true);
	TestRejectedField(TEXT("Knowledge eval rejects an indexRoot outside ProjectSavedDir"), UnrealMcp::KnowledgeEvalRun(EvalArgs, EmptyTools), TEXT("indexRoot"));
	EvalArgs.SetStringField(TEXT("evalPath"), FPaths::Combine(OutsideRoot, TEXT("evals")));
	EvalArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	EvalArgs.SetBoolField(TEXT("refreshIndex"), false);
	TestRejectedField(TEXT("Knowledge eval rejects an evalPath outside the project/shared eval roots"), UnrealMcp::KnowledgeEvalRun(EvalArgs, EmptyTools), TEXT("evalPath"));

	FString OutcomeFailureReason;
	TestFalse(
		TEXT("Outcome append rejects an indexRoot override outside ProjectSavedDir."),
		UnrealMcp::WriteOutcomeKnowledgeCard(
			TEXT("outside-index-root"),
			TEXT("Outside index root"),
			TEXT("This must not be written."),
			TEXT("Saved/UnrealMcp/LastExtensionApply.json"),
			{ TEXT("containment") },
			OutcomeFailureReason,
			OutsideRoot));
	TestTrue(TEXT("Outcome rejection explains the Saved boundary."), OutcomeFailureReason.Contains(TEXT("Saved directory")));
	TestFalse(TEXT("No rejected path call creates the outside root."), FPaths::DirectoryExists(OutsideRoot));

	TestTrue(TEXT("Outside fixture directory is created for link tests."), IFileManager::Get().MakeDirectory(*OutsideRoot, true));
	const FString OutsideTextPath = FPaths::Combine(OutsideRoot, TEXT("outside.txt"));
	const FString OutsideCardsPath = FPaths::Combine(OutsideRoot, TEXT("cards.jsonl"));
	const FString OutsideManifestPath = FPaths::Combine(OutsideRoot, TEXT("index.json"));
	const FString OutsideEvalPath = FPaths::Combine(OutsideRoot, TEXT("outside_eval.json"));
	const FString OutsideDocumentsPath = FPaths::Combine(OutsideRoot, TEXT("documents.jsonl"));
	TestTrue(TEXT("Outside text fixture is written."), FFileHelper::SaveStringToFile(TEXT("UEATELIER_OUTSIDE_TEXT_MUST_NOT_BE_INDEXED"), *OutsideTextPath));
	TestTrue(TEXT("Outside cards fixture is written."), FFileHelper::SaveStringToFile(TEXT("{}\n"), *OutsideCardsPath));
	TestTrue(TEXT("Outside manifest fixture is written."), FFileHelper::SaveStringToFile(TEXT("{}"), *OutsideManifestPath));
	TestTrue(TEXT("Outside eval fixture is written."), FFileHelper::SaveStringToFile(TEXT("{\"name\":\"outside\",\"type\":\"unsupported\"}"), *OutsideEvalPath));
	TestTrue(TEXT("Outside documents fixture is written."), FFileHelper::SaveStringToFile(TEXT("{\"id\":\"outside\",\"title\":\"Outside\",\"category\":\"security\",\"textPath\":\"outside.txt\"}\n"), *OutsideDocumentsPath));

#if PLATFORM_MAC || PLATFORM_LINUX
	TestTrue(TEXT("Index leaf fixture directory is created."), IFileManager::Get().MakeDirectory(*IndexSymlinkRoot, true));
	TestTrue(TEXT("cards.jsonl symlink fixture is created."), CreateKnowledgeSymlink(OutsideCardsPath, CardsSymlinkPath));
	SearchArgs.SetStringField(TEXT("indexRoot"), IndexSymlinkRoot);
	const FUnrealMcpExecutionResult LinkedCardsSearch = UnrealMcp::KnowledgeSearch(SearchArgs);
	TestTrue(TEXT("Search rejects a symlinked cards.jsonl leaf."), LinkedCardsSearch.bIsError);
	TestEqual(TEXT("Symlinked cards leaf reports corrupt index status."), StructuredString(LinkedCardsSearch, TEXT("indexStatus")), FString(TEXT("corrupt")));
	DeleteKnowledgeSymlink(CardsSymlinkPath);
	TestTrue(TEXT("index.json symlink fixture is created."), CreateKnowledgeSymlink(OutsideManifestPath, ManifestSymlinkPath));
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	RefreshArgs.SetStringField(TEXT("indexRoot"), IndexSymlinkRoot);
	RefreshArgs.SetBoolField(TEXT("dryRun"), false);
	const FUnrealMcpExecutionResult LinkedManifestRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestTrue(TEXT("Refresh rejects a symlinked index.json leaf."), LinkedManifestRefresh.bIsError);
	TestTrue(TEXT("Symlinked index leaf rejection identifies the bounded leaf rule."), LinkedManifestRefresh.Text.Contains(TEXT("fixed leaf files")));
	DeleteKnowledgeSymlink(ManifestSymlinkPath);
#else
	AddInfo(TEXT("Symlink leaf checks are skipped on this platform; lexical and reparse-point checks remain active."));
#endif

	TestTrue(TEXT("Documents fixture directory is created."), IFileManager::Get().MakeDirectory(*DocumentsDirectory, true));
	FString TraversingTextPath = OutsideTextPath;
	TestTrue(TEXT("Outside text path is made relative to the documents directory."), FPaths::MakePathRelativeTo(TraversingTextPath, *(DocumentsDirectory + TEXT("/"))));
	FPaths::NormalizeFilename(TraversingTextPath);
	FString DocumentsJsonl = FString::Printf(
		TEXT("{\"id\":\"traversal\",\"title\":\"Traversal\",\"category\":\"security\",\"textPath\":\"%s\"}\n"),
		*TraversingTextPath);
	double ExpectedSkippedRows = 1.0;
#if PLATFORM_MAC || PLATFORM_LINUX
	TestTrue(TEXT("textPath directory symlink fixture is created."), CreateKnowledgeSymlink(OutsideRoot, TextDirectorySymlink));
	DocumentsJsonl += TEXT("{\"id\":\"linked\",\"title\":\"Linked\",\"category\":\"security\",\"textPath\":\"linked_outside/outside.txt\"}\n");
	ExpectedSkippedRows = 2.0;
	TestTrue(TEXT("source directory symlink fixture is created."), CreateKnowledgeSymlink(OutsideRoot, SourceDirectorySymlink));
	TestTrue(TEXT("in-root directory symlink fixture is created."), CreateKnowledgeSymlink(DocumentsDirectory, InternalSourceSymlink));
#endif
	const FString DocumentsJsonlPath = FPaths::Combine(DocumentsDirectory, TEXT("documents.jsonl"));
	TestTrue(TEXT("Traversal documents.jsonl fixture is written."), FFileHelper::SaveStringToFile(DocumentsJsonl, *DocumentsJsonlPath));
	ConfigureIsolatedRefresh(RefreshArgs, TestRoot);
	RefreshArgs.SetBoolField(TEXT("includeOfficialDocs"), true);
	RefreshArgs.SetBoolField(TEXT("includePromotedSources"), false);
	RefreshArgs.SetBoolField(TEXT("allowEmptyIndex"), true);
	RefreshArgs.SetBoolField(TEXT("dryRun"), true);
	const FUnrealMcpExecutionResult SafeSourceRefresh = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestFalse(TEXT("A legal Saved sourceRoot still supports a dry-run refresh."), SafeSourceRefresh.bIsError);
	TestEqual(TEXT("Only the in-root documents.jsonl is enumerated."), StructuredNumber(SafeSourceRefresh, TEXT("sourceDocumentsJsonlCount")), 1.0);
	TestEqual(TEXT("Escaping textPath rows are skipped."), StructuredNumber(SafeSourceRefresh, TEXT("skippedRows")), ExpectedSkippedRows);
	TestEqual(TEXT("No outside text becomes a KnowledgeCard."), StructuredNumber(SafeSourceRefresh, TEXT("cardCount")), 0.0);

#if PLATFORM_MAC || PLATFORM_LINUX
	TestTrue(TEXT("Eval fixture directory is created."), IFileManager::Get().MakeDirectory(*EvalRoot, true));
	TestTrue(TEXT("Eval directory symlink fixture is created."), CreateKnowledgeSymlink(OutsideRoot, EvalDirectorySymlink));
	EvalArgs.SetStringField(TEXT("evalPath"), EvalRoot);
	EvalArgs.SetStringField(TEXT("indexRoot"), IndexRootForRoot(TestRoot));
	EvalArgs.SetBoolField(TEXT("refreshIndex"), false);
	const FUnrealMcpExecutionResult LinkedEvalResult = UnrealMcp::KnowledgeEvalRun(EvalArgs, EmptyTools);
	TestTrue(TEXT("Eval with only an external linked directory has no runnable cases."), LinkedEvalResult.bIsError);
	TestEqual(TEXT("No-follow eval walker does not open the external eval file."), StructuredNumber(LinkedEvalResult, TEXT("fileCount")), 0.0);
#endif
	return true;
}

#endif
