#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "UnrealMcpSelfExtensionTools.h"

namespace UnrealMcpKnowledgeRetrievalTests
{
	FString MakeRoot(const FString& TestName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealMcp/Tests/KnowledgeRetrieval"),
			TestName));
	}

	FString SourceRoot(const FString& TestRoot)
	{
		return FPaths::Combine(TestRoot, TEXT("KnowledgeSources"));
	}

	FString IndexRoot(const FString& TestRoot)
	{
		return FPaths::Combine(TestRoot, TEXT("KnowledgeIndex"));
	}

	bool WriteSource(const FString& TestRoot, const FString& Filename, const FString& Markdown)
	{
		const FString Path = FPaths::Combine(SourceRoot(TestRoot), Filename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Markdown, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FUnrealMcpExecutionResult Refresh(const FString& TestRoot)
	{
		FJsonObject Args;
		Args.SetStringField(TEXT("sourceRoot"), SourceRoot(TestRoot));
		Args.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
		Args.SetBoolField(TEXT("includeOfficialDocs"), false);
		Args.SetBoolField(TEXT("includePromotedSources"), true);
		Args.SetBoolField(TEXT("includeVersionedDocs"), false);
		Args.SetBoolField(TEXT("includeToolRegistry"), false);
		Args.SetBoolField(TEXT("includeActivityLog"), false);
		Args.SetBoolField(TEXT("includeSkills"), false);
		Args.SetBoolField(TEXT("allowEmptyIndex"), false);
		Args.SetNumberField(TEXT("maxCards"), 50.0);
		return UnrealMcp::KnowledgeIndexRefresh(Args);
	}

	FUnrealMcpExecutionResult Search(const FString& TestRoot, const FString& Query, int32 Limit = 10)
	{
		FJsonObject Args;
		Args.SetStringField(TEXT("query"), Query);
		Args.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
		Args.SetNumberField(TEXT("limit"), Limit);
		return UnrealMcp::KnowledgeSearch(Args);
	}

	TArray<FString> ResultSourcePaths(const FUnrealMcpExecutionResult& Result)
	{
		TArray<FString> Paths;
		if (!Result.StructuredContent.IsValid())
		{
			return Paths;
		}

		const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
		if (!Result.StructuredContent->TryGetArrayField(TEXT("results"), Results) || !Results)
		{
			return Paths;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Results)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			FString SourcePath;
			if (Object.IsValid() && Object->TryGetStringField(TEXT("sourcePath"), SourcePath))
			{
				Paths.Add(SourcePath);
			}
		}
		return Paths;
	}

	bool ContainsFilename(const TArray<FString>& Paths, const FString& Filename)
	{
		for (const FString& Path : Paths)
		{
			if (FPaths::GetCleanFilename(Path).Equals(Filename, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeBoundaryMatchTest,
	"UnrealMcp.Knowledge.Retrieval.BoundaryMatchRejectsBuildForUi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeBoundaryMatchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("boundary_match"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	TestTrue(
		TEXT("UI source is written."),
		WriteSource(TestRoot, TEXT("ui.md"), TEXT("# UI widget reference\n\nUI HUD layout and UMG controls.\n")));
	TestTrue(
		TEXT("Build source is written."),
		WriteSource(TestRoot, TEXT("build.md"), TEXT("# Build reference\n\nBuild compile UBT pipeline.\n")));
	TestFalse(TEXT("Boundary fixture refresh succeeds."), Refresh(TestRoot).bIsError);

	const FUnrealMcpExecutionResult Result = Search(TestRoot, TEXT("ui"));
	TestFalse(TEXT("UI search succeeds."), Result.bIsError);
	const TArray<FString> SourcePaths = ResultSourcePaths(Result);
	TestTrue(TEXT("UI source is retrieved."), ContainsFilename(SourcePaths, TEXT("ui.md")));
	TestFalse(TEXT("Build source is not retrieved through an interior substring match."), ContainsFilename(SourcePaths, TEXT("build.md")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeEngineVersionRankingTest,
	"UnrealMcp.Knowledge.Retrieval.EngineVersionRanksExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeEngineVersionRankingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("engine_version"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString DocsRoot = FPaths::Combine(SourceRoot(TestRoot), TEXT("official"));
	TestTrue(TEXT("Engine-version fixture directory is created."), IFileManager::Get().MakeDirectory(*DocsRoot, true));
	TestTrue(TEXT("UE 5.7 source is written."), FFileHelper::SaveStringToFile(
		TEXT("# Unreal Engine 5.7 Python API\n\nReference for Unreal Engine version 5.7."),
		*FPaths::Combine(DocsRoot, TEXT("ue57.txt"))));
	TestTrue(TEXT("UE 5.8 source is written."), FFileHelper::SaveStringToFile(
		TEXT("# Unreal Engine 5.8 Python API\n\nReference for Unreal Engine version 5.8."),
		*FPaths::Combine(DocsRoot, TEXT("ue58.txt"))));
	const FString DocumentsJsonl =
		TEXT("{\"id\":\"ue57\",\"title\":\"UE 5.7 Python API\",\"category\":\"python\",\"engineVersion\":\"5.7\",\"textPath\":\"ue57.txt\"}\n")
		TEXT("{\"id\":\"ue58\",\"title\":\"UE 5.8 Python API\",\"category\":\"python\",\"engineVersion\":\"5.8\",\"textPath\":\"ue58.txt\"}\n");
	TestTrue(TEXT("Engine-version documents.jsonl is written."), FFileHelper::SaveStringToFile(
		DocumentsJsonl,
		*FPaths::Combine(DocsRoot, TEXT("documents.jsonl"))));
	FJsonObject RefreshArgs;
	RefreshArgs.SetStringField(TEXT("sourceRoot"), SourceRoot(TestRoot));
	RefreshArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	RefreshArgs.SetBoolField(TEXT("includeOfficialDocs"), true);
	RefreshArgs.SetBoolField(TEXT("includePromotedSources"), false);
	RefreshArgs.SetBoolField(TEXT("includeVersionedDocs"), false);
	RefreshArgs.SetBoolField(TEXT("includeToolRegistry"), false);
	RefreshArgs.SetBoolField(TEXT("includeActivityLog"), false);
	RefreshArgs.SetBoolField(TEXT("includeSkills"), false);
	TestFalse(TEXT("Engine-version fixture refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);

	const FUnrealMcpExecutionResult Result = Search(TestRoot, TEXT("UE5.8 Python API"), 2);
	TestFalse(TEXT("Engine-version search succeeds."), Result.bIsError);
	const TArray<FString> SourcePaths = ResultSourcePaths(Result);
	TestTrue(TEXT("Engine-version search returns at least one result."), !SourcePaths.IsEmpty());
	if (!SourcePaths.IsEmpty())
	{
		TestEqual(TEXT("Explicit UE5.8 query ranks the UE 5.8 source first."), FPaths::GetCleanFilename(SourcePaths[0]), FString(TEXT("ue58.txt")));
	}
	TestFalse(TEXT("Explicit UE5.8 query excludes the UE 5.7 card."), ContainsFilename(SourcePaths, TEXT("ue57.txt")));
	const TArray<FString> PlainVersionPaths = ResultSourcePaths(Search(TestRoot, TEXT("5.8 Python API"), 2));
	TestTrue(TEXT("Indexed plain 5.8 is recognized as an engine version."), ContainsFilename(PlainVersionPaths, TEXT("ue58.txt")));
	TestFalse(TEXT("Plain 5.8 excludes the UE 5.7 card."), ContainsFilename(PlainVersionPaths, TEXT("ue57.txt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeMultiEngineVersionQueryTest,
	"UnrealMcp.Knowledge.Retrieval.MultiEngineVersionQueryKeepsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeMultiEngineVersionQueryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("multi_engine_version"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString DocsRoot = FPaths::Combine(SourceRoot(TestRoot), TEXT("official"));
	TestTrue(TEXT("Official-doc fixture directory is created."), IFileManager::Get().MakeDirectory(*DocsRoot, true));
	TestTrue(
		TEXT("UE 5.7 official-doc text is written."),
		FFileHelper::SaveStringToFile(TEXT("# UE 5.7 Python API\n\nComparison reference."), *FPaths::Combine(DocsRoot, TEXT("ue57.txt"))));
	TestTrue(
		TEXT("UE 5.8 official-doc text is written."),
		FFileHelper::SaveStringToFile(TEXT("# UE 5.8 Python API\n\nComparison reference."), *FPaths::Combine(DocsRoot, TEXT("ue58.txt"))));
	const FString DocumentsJsonl =
		TEXT("{\"id\":\"ue57\",\"title\":\"UE 5.7 Python API\",\"category\":\"python\",\"engineVersion\":\"5.7\",\"textPath\":\"ue57.txt\"}\n")
		TEXT("{\"id\":\"ue58\",\"title\":\"UE 5.8 Python API\",\"category\":\"python\",\"engineVersion\":\"5.8\",\"textPath\":\"ue58.txt\"}\n");
	TestTrue(
		TEXT("Multi-engine documents.jsonl is written."),
		FFileHelper::SaveStringToFile(DocumentsJsonl, *FPaths::Combine(DocsRoot, TEXT("documents.jsonl"))));

	FJsonObject RefreshArgs;
	RefreshArgs.SetStringField(TEXT("sourceRoot"), SourceRoot(TestRoot));
	RefreshArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	RefreshArgs.SetBoolField(TEXT("includeOfficialDocs"), true);
	RefreshArgs.SetBoolField(TEXT("includePromotedSources"), false);
	RefreshArgs.SetBoolField(TEXT("includeVersionedDocs"), false);
	RefreshArgs.SetBoolField(TEXT("includeToolRegistry"), false);
	RefreshArgs.SetBoolField(TEXT("includeActivityLog"), false);
	RefreshArgs.SetBoolField(TEXT("includeSkills"), false);
	TestFalse(TEXT("Multi-engine fixture refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);

	const FUnrealMcpExecutionResult Result = Search(TestRoot, TEXT("compare UE5.7 and UE5.8 Python API"), 10);
	TestFalse(TEXT("Multi-engine comparison search succeeds."), Result.bIsError);
	const TArray<FString> SourcePaths = ResultSourcePaths(Result);
	TestTrue(TEXT("Multi-engine comparison keeps the UE 5.7 card."), ContainsFilename(SourcePaths, TEXT("ue57.txt")));
	TestTrue(TEXT("Multi-engine comparison keeps the UE 5.8 card."), ContainsFilename(SourcePaths, TEXT("ue58.txt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeNonEngineNumericQueryTest,
	"UnrealMcp.Knowledge.Retrieval.NonEngineNumericTokensKeepVersionedCards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeNonEngineNumericQueryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;
	const FString TestRoot = MakeRoot(TEXT("non_engine_numeric"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestRoot, false, true); };

	const FString DocsRoot = FPaths::Combine(SourceRoot(TestRoot), TEXT("official"));
	IFileManager::Get().MakeDirectory(*DocsRoot, true);
	TestTrue(TEXT("UE 5.7 numeric fixture is written."), FFileHelper::SaveStringToFile(
		TEXT("Python release workflow changes."), *FPaths::Combine(DocsRoot, TEXT("ue57.txt"))));
	TestTrue(TEXT("UE 5.8 numeric fixture is written."), FFileHelper::SaveStringToFile(
		TEXT("Python release workflow changes."), *FPaths::Combine(DocsRoot, TEXT("ue58.txt"))));
	const FString DocumentsJsonl =
		TEXT("{\"id\":\"numeric57\",\"title\":\"Python changes\",\"category\":\"python\",\"engineVersion\":\"5.7\",\"textPath\":\"ue57.txt\"}\n")
		TEXT("{\"id\":\"numeric58\",\"title\":\"Python changes\",\"category\":\"python\",\"engineVersion\":\"5.8\",\"textPath\":\"ue58.txt\"}\n");
	TestTrue(TEXT("Numeric documents.jsonl is written."), FFileHelper::SaveStringToFile(
		DocumentsJsonl, *FPaths::Combine(DocsRoot, TEXT("documents.jsonl"))));
	FJsonObject RefreshArgs;
	RefreshArgs.SetStringField(TEXT("sourceRoot"), SourceRoot(TestRoot));
	RefreshArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	RefreshArgs.SetBoolField(TEXT("includeOfficialDocs"), true);
	RefreshArgs.SetBoolField(TEXT("includePromotedSources"), false);
	RefreshArgs.SetBoolField(TEXT("includeVersionedDocs"), false);
	RefreshArgs.SetBoolField(TEXT("includeToolRegistry"), false);
	RefreshArgs.SetBoolField(TEXT("includeActivityLog"), false);
	RefreshArgs.SetBoolField(TEXT("includeSkills"), false);
	TestFalse(TEXT("Numeric fixture refresh succeeds."), UnrealMcp::KnowledgeIndexRefresh(RefreshArgs).bIsError);

	const TArray<FString> Paths = ResultSourcePaths(Search(TestRoot, TEXT("what changed in 0.35 for python 3.11"), 10));
	TestTrue(TEXT("Product/Python numeric query keeps the UE 5.7 card."), ContainsFilename(Paths, TEXT("ue57.txt")));
	TestTrue(TEXT("Product/Python numeric query keeps the UE 5.8 card."), ContainsFilename(Paths, TEXT("ue58.txt")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeCjkLatinTokenizationTest,
	"UnrealMcp.Knowledge.Retrieval.CjkBigramsAndEmbeddedLatin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeCjkLatinTokenizationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;
	const FString TestRoot = MakeRoot(TEXT("cjk_latin"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestRoot, false, true); };

	TestTrue(TEXT("CJK/Latin source is written."), WriteSource(
		TestRoot,
		TEXT("cjk_niagara.md"),
		TEXT("# 粒子说明\n\n霜火之间流动星尘，并用niagara系统控制效果。\n")));
	TestTrue(TEXT("CJK decoy source is written."), WriteSource(
		TestRoot,
		TEXT("build.md"),
		TEXT("# 构建说明\n\n编译构建流水线。\n")));
	TestFalse(TEXT("CJK fixture refresh succeeds."), Refresh(TestRoot).bIsError);

	const TArray<FString> BigramPaths = ResultSourcePaths(Search(TestRoot, TEXT("霜火星尘")));
	TestTrue(TEXT("Non-synonym overlapping CJK bigrams retrieve the intended card."), ContainsFilename(BigramPaths, TEXT("cjk_niagara.md")));
	const TArray<FString> NiagaraPaths = ResultSourcePaths(Search(TestRoot, TEXT("niagara")));
	TestTrue(TEXT("Non-synonym Latin token embedded directly in CJK prose remains retrievable."), ContainsFilename(NiagaraPaths, TEXT("cjk_niagara.md")));
	TestFalse(TEXT("Niagara does not retrieve the build decoy."), ContainsFilename(NiagaraPaths, TEXT("build.md")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeOriginalTokenRankingTest,
	"UnrealMcp.Knowledge.Retrieval.OriginalTokenOutranksSynonym",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeOriginalTokenRankingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;
	const FString TestRoot = MakeRoot(TEXT("original_token_rank"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*TestRoot, false, true); };

	TestTrue(TEXT("Exact widget source is written."), WriteSource(
		TestRoot, TEXT("widget.md"), TEXT("# Zulu widget reference\n\nrankanchor\n")));
	TestTrue(TEXT("Synonym-only UI source is written."), WriteSource(
		TestRoot, TEXT("ui.md"), TEXT("# Alpha ui reference\n\nrankanchor\n")));
	TestFalse(TEXT("Original-token fixture refresh succeeds."), Refresh(TestRoot).bIsError);
	const TArray<FString> Paths = ResultSourcePaths(Search(TestRoot, TEXT("widget rankanchor"), 2));
	TestTrue(TEXT("Original-token query returns results."), !Paths.IsEmpty());
	if (!Paths.IsEmpty())
	{
		TestEqual(TEXT("Original widget token outranks expanded UI synonym."), FPaths::GetCleanFilename(Paths[0]), FString(TEXT("widget.md")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeSourceDiversityTest,
	"UnrealMcp.Knowledge.Retrieval.SourceKindDiversitySurvivesGlobalCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeSourceDiversityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("source_diversity"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	const FString DiversitySentinel = TEXT("UEATELIER_RAG_SOURCE_DIVERSITY_SENTINEL");
	TestTrue(
		TEXT("Promoted source is written."),
		WriteSource(
			TestRoot,
			TEXT("promoted.md"),
			FString::Printf(TEXT("# Promoted workflow\n\n%s\n"), *DiversitySentinel)));

	FJsonObject RefreshArgs;
	RefreshArgs.SetStringField(TEXT("sourceRoot"), SourceRoot(TestRoot));
	RefreshArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	RefreshArgs.SetBoolField(TEXT("includeOfficialDocs"), false);
	RefreshArgs.SetBoolField(TEXT("includePromotedSources"), true);
	RefreshArgs.SetBoolField(TEXT("includeVersionedDocs"), false);
	RefreshArgs.SetBoolField(TEXT("includeToolRegistry"), true);
	RefreshArgs.SetBoolField(TEXT("includeActivityLog"), false);
	RefreshArgs.SetBoolField(TEXT("includeSkills"), false);
	RefreshArgs.SetNumberField(TEXT("maxCards"), 10.0);
	const FUnrealMcpExecutionResult RefreshResult = UnrealMcp::KnowledgeIndexRefresh(RefreshArgs);
	TestFalse(TEXT("Source-diversity fixture refresh succeeds."), RefreshResult.bIsError);

	const FUnrealMcpExecutionResult SearchResult = Search(TestRoot, DiversitySentinel);
	TestFalse(TEXT("Promoted source remains searchable after truncation."), SearchResult.bIsError);
	TestTrue(
		TEXT("At least one card from the promoted source kind survives the global cap."),
		ContainsFilename(ResultSourcePaths(SearchResult), TEXT("promoted.md")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeRankAwareEvalTest,
	"UnrealMcp.Knowledge.Retrieval.RankAwareEvalDetectsWrongTopResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeRankAwareEvalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("rank_aware_eval"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	TestTrue(
		TEXT("Rank eval UE 5.7 source is written."),
		WriteSource(TestRoot, TEXT("ue57.md"), TEXT("# Unreal Engine 5.7 Python API\n\nReference for Unreal Engine version 5.7.\n")));
	TestTrue(
		TEXT("Rank eval UE 5.8 source is written."),
		WriteSource(TestRoot, TEXT("ue58.md"), TEXT("# Unreal Engine 5.8 Python API\n\nReference for Unreal Engine version 5.8.\n")));
	TestFalse(TEXT("Rank eval fixture refresh succeeds."), Refresh(TestRoot).bIsError);

	const FString EvalPath = FPaths::Combine(TestRoot, TEXT("rank_eval.json"));
	const FString EvalJson = TEXT(R"JSON({
  "schema": "UEvolve.KnowledgeEval.v2",
  "cases": [
    {
      "name": "correct_top_result",
      "type": "search",
      "query": "UE5.8 Python API",
      "expectSourcePathsAtK": ["ue58.md"],
      "forbidTopSourcePathContains": ["ue57.md"]
    },
    {
      "name": "intentional_wrong_top_result",
      "type": "search",
      "query": "UE5.8 Python API",
      "expectSourcePathsAtK": ["ue57.md"]
    }
  ]
})JSON");
	TestTrue(
		TEXT("Rank-aware eval file is written."),
		FFileHelper::SaveStringToFile(EvalJson, *EvalPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FJsonObject EvalArgs;
	EvalArgs.SetStringField(TEXT("evalPath"), EvalPath);
	EvalArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	EvalArgs.SetBoolField(TEXT("refreshIndex"), false);
	EvalArgs.SetBoolField(TEXT("includeDetails"), false);
	EvalArgs.SetNumberField(TEXT("limit"), 2.0);
	TArray<TSharedPtr<FJsonValue>> EmptyTools;
	const FUnrealMcpExecutionResult EvalResult = UnrealMcp::KnowledgeEvalRun(EvalArgs, EmptyTools);
	TestTrue(TEXT("Intentional rank regression makes the eval run fail."), EvalResult.bIsError);
	TestTrue(TEXT("Rank eval returns structured counts."), EvalResult.StructuredContent.IsValid());
	if (EvalResult.StructuredContent.IsValid())
	{
		TestEqual(TEXT("Rank eval case count."), static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("caseCount"))), 2);
		TestEqual(TEXT("Correct rank case passes."), static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("passedCount"))), 1);
		TestEqual(TEXT("Wrong rank case fails."), static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("failedCount"))), 1);
		TestEqual(TEXT("Three rank assertions were evaluated."), static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("rankAssertionCount"))), 3);
		TestEqual(TEXT("Two of three rank assertions passed."), static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("rankAssertionPassedCount"))), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpKnowledgeSharedRepoEvalPathTest,
	"UnrealMcp.Knowledge.Eval.SharedRepoRelativePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpKnowledgeSharedRepoEvalPathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpKnowledgeRetrievalTests;

	const FString TestRoot = MakeRoot(TEXT("shared_repo_eval_path"));
	IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*TestRoot, false, true);
	};

	FJsonObject EvalArgs;
	EvalArgs.SetStringField(TEXT("evalPath"), TEXT("Tools/UnrealMcpKnowledge/Evals"));
	EvalArgs.SetStringField(TEXT("indexRoot"), IndexRoot(TestRoot));
	EvalArgs.SetBoolField(TEXT("refreshIndex"), true);
	EvalArgs.SetBoolField(TEXT("includeDetails"), false);
	EvalArgs.SetNumberField(TEXT("limit"), 6.0);
	TArray<TSharedPtr<FJsonValue>> EmptyTools;
	const FUnrealMcpExecutionResult EvalResult = UnrealMcp::KnowledgeEvalRun(EvalArgs, EmptyTools);
	TestFalse(TEXT("Repository-relative core eval path resolves from a nested project host."), EvalResult.bIsError);
	TestTrue(TEXT("Core eval returns structured counts."), EvalResult.StructuredContent.IsValid());
	if (EvalResult.StructuredContent.IsValid())
	{
		const int32 CaseCount = static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("caseCount")));
		const int32 FailedCount = static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("failedCount")));
		const int32 RankAssertionCount = static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("rankAssertionCount")));
		const int32 RankAssertionPassedCount = static_cast<int32>(EvalResult.StructuredContent->GetNumberField(TEXT("rankAssertionPassedCount")));
		TestTrue(TEXT("Core eval contains at least one case."), CaseCount > 0);
		TestEqual(TEXT("Core eval has no failed cases."), FailedCount, 0);
		TestTrue(TEXT("Core eval exercises rank assertions."), RankAssertionCount > 0);
		TestEqual(TEXT("All core rank assertions pass."), RankAssertionPassedCount, RankAssertionCount);
	}

	TArray<FString> CardLines;
	const FString CardsPath = FPaths::Combine(IndexRoot(TestRoot), TEXT("cards.jsonl"));
	TestTrue(TEXT("Shared-repo eval index cards are readable."), FFileHelper::LoadFileToStringArray(CardLines, *CardsPath));
	bool bFoundSharedReadmeCard = false;
	bool bSharedReadmeHasFreshness = false;
	for (const FString& CardLine : CardLines)
	{
		if (CardLine.Contains(TEXT("\"sourcePath\":\"../../README.md\""), ESearchCase::CaseSensitive))
		{
			bFoundSharedReadmeCard = true;
			bSharedReadmeHasFreshness = CardLine.Contains(TEXT("\"sourceUpdatedAt\":"), ESearchCase::CaseSensitive);
			break;
		}
	}
	TestTrue(TEXT("Nested example host indexes the shared repository README."), bFoundSharedReadmeCard);
	TestTrue(TEXT("Shared repository README contributes freshness metadata."), bSharedReadmeHasFreshness);
	return true;
}

#endif
