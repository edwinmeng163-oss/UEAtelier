#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

namespace UnrealMcp::TaskAtlasService
{
	void SetMadeToolsRootDirForTests(const FString& RootDir);
	void ClearMadeToolsRootDirForTests();
	void RejectNextVetReloadForTests();
	void FailNextVetAuditForTests();
}

namespace UnrealMcpVetMadeToolTests
{
	const FString Prefix = TEXT("atlas_vet_w2_");
	const FString PassPy =
		TEXT("def execute(args):\n")
		TEXT("    r = call_tool_raw(\"unreal.code_apply_change\", {\"previewId\": \"missing_vet_preview\", \"dryRun\": False})\n")
		TEXT("    return {\"isError\": False, \"text\": \"vet smoke ok\", \"structuredContent\": {\"steps\": [{\"tool\": \"unreal.code_apply_change\", \"policyDecision\": r.get(\"meta\", {}).get(\"policyDecision\", \"\"), \"isError\": False}], \"hasBlockedSteps\": False, \"blockedCount\": 0, \"rawIsError\": bool(r.get(\"isError\"))}}\n");
	const FString SafePy = TEXT("def execute(args):\n    return {\"isError\": False, \"text\": \"ok\", \"structuredContent\": {\"steps\": [], \"hasBlockedSteps\": False, \"blockedCount\": 0}}\n");
	const FString FailPy = TEXT("def execute(args):\n    raise RuntimeError(\"vet smoke boom\")\n");
	const FString ImportOsPy = TEXT("import os\n\ndef execute(args):\n    return {\"isError\": False, \"text\": \"bad\"}\n");

	FString Root()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return UnrealMcp::UserRegistry::GetUserToolsRootDir();
	}

	FString ToolName(const FString& ToolId)
	{
		return FString(TEXT("user.")) + ToolId;
	}

	void Reload()
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(true);
	}

	void Cleanup()
	{
		const FString PyRoot = Root();
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(PyRoot, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (DirectoryName.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				IFileManager::Get().DeleteDirectory(*FPaths::Combine(PyRoot, DirectoryName), false, true);
			}
		}
		UnrealMcp::TaskAtlasService::ClearMadeToolsRootDirForTests();
		Reload();
	}

	FString JsonString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	bool WriteFile(const FString& Path, const FString& Text)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool WriteTool(const FString& Suffix, const FString& MainPy, const TArray<FString>& Allowlist = TArray<FString>())
	{
		const FString ToolId = Prefix + Suffix;
		const FString ToolDir = FPaths::Combine(Root(), ToolId);
		const FString Sha = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy).ToLower();
		TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
		ToolJson->SetStringField(TEXT("name"), ToolName(ToolId));
		ToolJson->SetStringField(TEXT("title"), TEXT("Vet W2 Fixture"));
		ToolJson->SetStringField(TEXT("description"), TEXT("VetMadeTool automation fixture."));
		ToolJson->SetStringField(TEXT("generator"), TEXT("task_atlas_make_composite"));
		ToolJson->SetStringField(TEXT("compositeKind"), TEXT("preview"));
		ToolJson->SetStringField(TEXT("replayStatus"), TEXT("preview_ready"));
		ToolJson->SetStringField(TEXT("sourceTaskId"), TEXT("vet-w2-task"));
		ToolJson->SetStringField(TEXT("pythonHandlerSha256"), Sha);
		TArray<TSharedPtr<FJsonValue>> AllowValues;
		for (const FString& Entry : Allowlist)
		{
			AllowValues.Add(MakeShared<FJsonValueString>(Entry));
		}
		ToolJson->SetArrayField(TEXT("importAllowlist"), AllowValues);
		TArray<TSharedPtr<FJsonValue>> CriticalPathValues;
		CriticalPathValues.Add(MakeShared<FJsonValueString>(TEXT("unreal.code_apply_change")));
		ToolJson->SetArrayField(TEXT("criticalPath"), CriticalPathValues);
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetNumberField(TEXT("ordinal"), 0);
		Step->SetStringField(TEXT("tool"), TEXT("unreal.code_apply_change"));
		TArray<TSharedPtr<FJsonValue>> StepRefValues;
		StepRefValues.Add(MakeShared<FJsonValueObject>(Step));
		ToolJson->SetArrayField(TEXT("stepRefs"), StepRefValues);
		TSharedPtr<FJsonObject> SmokeArgs = MakeShared<FJsonObject>();
		ToolJson->SetObjectField(TEXT("smokeArgs"), SmokeArgs);
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetBoolField(TEXT("additionalProperties"), false);
		ToolJson->SetObjectField(TEXT("inputSchema"), InputSchema);
		return WriteFile(FPaths::Combine(ToolDir, TEXT("main.py")), MainPy)
			&& WriteFile(FPaths::Combine(ToolDir, TEXT("tool.json")), JsonString(ToolJson));
	}

	UnrealMcp::TaskAtlasService::FVetMadeToolResult Vet(const FString& Suffix, const FString& Verdict = TEXT("pass"))
	{
		UnrealMcp::TaskAtlasService::FVetMadeToolRequest Req;
		Req.ToolName = ToolName(Prefix + Suffix);
		Req.AiReviewVerdict = Verdict;
		Req.AiReviewSummary = TEXT("AI review passed for the exact generated source.");
		Req.Approver = TEXT("automation");
		return UnrealMcp::TaskAtlasService::VetMadeTool(Req);
	}

	bool IsPythonUnavailable(const UnrealMcp::TaskAtlasService::FVetMadeToolResult& Result)
	{
		return Result.FailureStage == TEXT("smoke")
			&& (Result.FailureDetail.Contains(TEXT("PythonScriptPlugin"))
				|| Result.FailureDetail.Contains(TEXT("Python support is not available"))
				|| Result.FailureDetail.Contains(TEXT("Python is not initialized")));
	}

	bool ReadToolJson(const FString& Suffix, TSharedPtr<FJsonObject>& OutObject)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root(), Prefix + Suffix, TEXT("tool.json"))))
		{
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	bool MarkerVetted(const FString& Suffix)
	{
		TSharedPtr<FJsonObject> ToolJson;
		const TSharedPtr<FJsonObject>* Marker = nullptr;
		bool bVetted = false;
		return ReadToolJson(Suffix, ToolJson)
			&& ToolJson->TryGetObjectField(TEXT("vettedMarker"), Marker)
			&& Marker
			&& (*Marker).IsValid()
			&& (*Marker)->TryGetBoolField(TEXT("vetted"), bVetted)
			&& bVetted;
	}

	bool EnsurePythonVetAvailable(FAutomationTestBase& Test, const FString& Suffix)
	{
		Test.TestTrue(TEXT("availability fixture writes"), WriteTool(Suffix, PassPy));
		Reload();
		const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(Suffix);
		if (Result.bVetted)
		{
			return true;
		}
		if (IsPythonUnavailable(Result))
		{
			Test.AddInfo(TEXT("Skipped Python-dependent vet assertion: PythonScriptPlugin unavailable in this automation environment."));
			return false;
		}
		Test.AddError(FString::Printf(TEXT("Unexpected vet failure at %s: %s"), *Result.FailureStage, *Result.FailureDetail));
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolHappyPathTest, "UnrealMcp.VetMadeTool.HappyPath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolHappyPathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };

	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("happy"), PassPy));
	Reload();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("happy"));
	if (IsPythonUnavailable(Result))
	{
		AddInfo(TEXT("Skipped: PythonScriptPlugin unavailable in this automation environment."));
		return true;
	}
	TestTrue(TEXT("vet passes"), Result.bVetted);
	TestTrue(TEXT("marker present on disk"), MarkerVetted(TEXT("happy")));
	TestEqual(TEXT("result sha matches file hash"), Result.MainPySha256, UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(PassPy).ToLower());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseNonMadeTest, "UnrealMcp.VetMadeTool.RefuseNonMade", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseNonMadeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::TaskAtlasService::FVetMadeToolRequest Req;
	Req.ToolName = TEXT("user.not_atlas_fixture");
	Req.AiReviewVerdict = TEXT("pass");
	Req.AiReviewSummary = TEXT("summary");
	Req.Approver = TEXT("automation");
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = UnrealMcp::TaskAtlasService::VetMadeTool(Req);
	TestFalse(TEXT("not vetted"), Result.bVetted);
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("resolve"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseBadRequestTest, "UnrealMcp.VetMadeTool.RefuseBadRequest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseBadRequestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("bad_request"), SafePy));
	Reload();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("bad_request"), TEXT("fail"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("request"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseAllowlistTest, "UnrealMcp.VetMadeTool.RefuseAllowlist", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseAllowlistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("allowlist"), SafePy, { TEXT("os") }));
	Reload();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("allowlist"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("source_policy"));
	TestTrue(TEXT("allowlist violation"), Result.FailureDetail.Contains(TEXT("allowlist")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseSourceImportTest, "UnrealMcp.VetMadeTool.RefuseSourceImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseSourceImportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("source"), ImportOsPy));
	Reload();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("source"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("source_policy"));
	TestTrue(TEXT("source violation"), Result.FailureDetail.Contains(TEXT("forbidden_import")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseHashDriftTest, "UnrealMcp.VetMadeTool.RefuseHashDrift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseHashDriftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("drift"), SafePy));
	Reload();
	TestTrue(TEXT("main drift write"), WriteFile(FPaths::Combine(Root(), Prefix + TEXT("drift"), TEXT("main.py")), SafePy + TEXT("\n# drift\n")));
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("drift"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("hash_baseline"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRefuseSmokeFailureTest, "UnrealMcp.VetMadeTool.RefuseSmokeFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRefuseSmokeFailureTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("smoke_fail"), FailPy));
	Reload();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("smoke_fail"));
	if (IsPythonUnavailable(Result))
	{
		AddInfo(TEXT("Skipped: PythonScriptPlugin unavailable in this automation environment."));
		return true;
	}
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("smoke"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRollbackReloadTest, "UnrealMcp.VetMadeTool.RollbackReload", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRollbackReloadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	if (!EnsurePythonVetAvailable(*this, TEXT("availability_reload")))
	{
		return true;
	}
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("reload"), PassPy));
	Reload();
	UnrealMcp::TaskAtlasService::RejectNextVetReloadForTests();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("reload"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("reload"));
	TestFalse(TEXT("marker rolled back"), MarkerVetted(TEXT("reload")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRollbackAuditTest, "UnrealMcp.VetMadeTool.RollbackAudit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRollbackAuditTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	if (!EnsurePythonVetAvailable(*this, TEXT("availability_audit")))
	{
		return true;
	}
	TestTrue(TEXT("fixture writes"), WriteTool(TEXT("audit"), PassPy));
	Reload();
	UnrealMcp::TaskAtlasService::FailNextVetAuditForTests();
	const UnrealMcp::TaskAtlasService::FVetMadeToolResult Result = Vet(TEXT("audit"));
	TestEqual(TEXT("stage"), Result.FailureStage, TEXT("audit_write_failed"));
	TestFalse(TEXT("marker rolled back"), MarkerVetted(TEXT("audit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolRevokeFlowTest, "UnrealMcp.VetMadeTool.RevokeFlow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolRevokeFlowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	if (!EnsurePythonVetAvailable(*this, TEXT("revoke")))
	{
		return true;
	}
	const UnrealMcp::TaskAtlasService::FRevokeMadeToolResult Result = UnrealMcp::TaskAtlasService::RevokeMadeTool(ToolName(Prefix + TEXT("revoke")), TEXT("automation"), TEXT("test revoke"));
	TestTrue(TEXT("revoked"), Result.bRevoked);
	TestFalse(TEXT("not vetted"), MarkerVetted(TEXT("revoke")));
	TSharedPtr<FJsonObject> ToolJson;
	const TSharedPtr<FJsonObject>* Marker = nullptr;
	TestTrue(TEXT("tool json reads"), ReadToolJson(TEXT("revoke"), ToolJson));
	TestTrue(TEXT("marker exists"), ToolJson->TryGetObjectField(TEXT("vettedMarker"), Marker) && Marker && (*Marker).IsValid());
	if (Marker && (*Marker).IsValid())
	{
		TestTrue(TEXT("approval history kept"), (*Marker)->HasField(TEXT("approvedAtUtc")));
		TestTrue(TEXT("revocation field"), (*Marker)->HasField(TEXT("revokedAtUtc")));
	}
	AddExpectedError(TEXT("Failed to audit made-tool revocation"), EAutomationExpectedErrorFlags::Contains, 1);
	UnrealMcp::TaskAtlasService::FailNextVetAuditForTests();
	const UnrealMcp::TaskAtlasService::FRevokeMadeToolResult AuditFailResult = UnrealMcp::TaskAtlasService::RevokeMadeTool(ToolName(Prefix + TEXT("revoke")), TEXT("automation"), TEXT("audit fail still revoked"));
	TestTrue(TEXT("audit-fail revoke still succeeds"), AuditFailResult.bRevoked);
	TestFalse(TEXT("still not vetted"), MarkerVetted(TEXT("revoke")));
	bool bIntrospectionRevoked = false;
	for (const UnrealMcp::TaskAtlasService::FUserToolView& View : UnrealMcp::TaskAtlasService::IntrospectUserRegistry())
	{
		if (View.ToolName == ToolName(Prefix + TEXT("revoke")))
		{
			bIntrospectionRevoked = !View.bVetted && !View.RevokedAtUtc.IsEmpty();
		}
	}
	TestTrue(TEXT("introspection shows revoked"), bIntrospectionRevoked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMcpVetMadeToolIntrospectionDriftTest, "UnrealMcp.VetMadeTool.IntrospectionDrift", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMcpVetMadeToolIntrospectionDriftTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpVetMadeToolTests;
	Cleanup();
	UnrealMcp::TaskAtlasService::SetMadeToolsRootDirForTests(Root());
	ON_SCOPE_EXIT { Cleanup(); };
	if (!EnsurePythonVetAvailable(*this, TEXT("introspect")))
	{
		return true;
	}
	bool bFoundBefore = false;
	for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
	{
		if (Entry.ToolName == ToolName(Prefix + TEXT("introspect")))
		{
			bFoundBefore = true;
			TestTrue(TEXT("list field marker present"), Entry.bVettedMarkerPresent);
			TestTrue(TEXT("list field live match"), Entry.bLiveShaMatches);
		}
	}
	TestTrue(TEXT("found before drift"), bFoundBefore);
	TestTrue(TEXT("main drift write"), WriteFile(FPaths::Combine(Root(), Prefix + TEXT("introspect"), TEXT("main.py")), PassPy + TEXT("\n# drift\n")));
	bool bFoundAfter = false;
	for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
	{
		if (Entry.ToolName == ToolName(Prefix + TEXT("introspect")))
		{
			bFoundAfter = true;
			TestFalse(TEXT("list field live mismatch"), Entry.bLiveShaMatches);
		}
	}
	TestTrue(TEXT("found after drift"), bFoundAfter);
	bool bViewDrift = false;
	for (const UnrealMcp::TaskAtlasService::FUserToolView& View : UnrealMcp::TaskAtlasService::IntrospectUserRegistry())
	{
		if (View.ToolName == ToolName(Prefix + TEXT("introspect")))
		{
			bViewDrift = View.bVettedMarkerPresent && !View.bLiveShaMatches;
		}
	}
	TestTrue(TEXT("introspection live mismatch"), bViewDrift);
	return true;
}

#endif
