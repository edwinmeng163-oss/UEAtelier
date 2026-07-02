#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpVettedToolset.h"

namespace UnrealMcpVettedToolsetTests
{
	TSharedPtr<FJsonObject> MakeManifestWithMarker(const UnrealMcp::FVettedToolsetMarker& Marker)
	{
		TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetObjectField(TEXT("vettedMarker"), UnrealMcp::SerializeVettedToolsetMarker(Marker));
		return Manifest;
	}

	TSharedPtr<FJsonObject> RoundTripJsonObject(const TSharedPtr<FJsonObject>& Object)
	{
		FString JsonText;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		FJsonSerializer::Deserialize(Reader, Parsed);
		return Parsed;
	}

	bool HasViolationCode(const UnrealMcp::FCompositeSourcePolicyResult& Result, const FString& Code)
	{
		const FString Prefix = Code + TEXT(":");
		for (const FString& Violation : Result.Violations)
		{
			if (Violation.StartsWith(Prefix))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpVettedToolsetMarkerRoundTripTest,
	"UnrealMcp.VettedToolset.MarkerRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpVettedToolsetMarkerRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString MainPy = TEXT("print('ok')\n");
	UnrealMcp::FVettedToolsetMarker Marker;
	Marker.SchemaVersion = 1;
	Marker.bVetted = true;
	Marker.MainPySha256 = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy);
	Marker.AiReviewVerdict = TEXT("pass");
	Marker.AiReviewSummary = TEXT("delegates side effects through call_tool");
	Marker.SmokeStatus = TEXT("passed");
	Marker.Approver = TEXT("director");
	Marker.ApprovedAtUtc = TEXT("2026-07-02T00:00:00Z");

	const TSharedPtr<FJsonObject> ParsedManifest = UnrealMcpVettedToolsetTests::RoundTripJsonObject(
		UnrealMcpVettedToolsetTests::MakeManifestWithMarker(Marker));
	const UnrealMcp::FVettedToolsetMarker Parsed = UnrealMcp::DeserializeVettedToolsetMarker(ParsedManifest);
	TestEqual(TEXT("schema version roundtrips"), Parsed.SchemaVersion, Marker.SchemaVersion);
	TestTrue(TEXT("vetted flag roundtrips"), Parsed.bVetted);
	TestEqual(TEXT("main.py sha roundtrips"), Parsed.MainPySha256, Marker.MainPySha256);
	TestEqual(TEXT("ai review verdict roundtrips"), Parsed.AiReviewVerdict, Marker.AiReviewVerdict);
	TestEqual(TEXT("ai review summary roundtrips"), Parsed.AiReviewSummary, Marker.AiReviewSummary);
	TestEqual(TEXT("smoke status roundtrips"), Parsed.SmokeStatus, Marker.SmokeStatus);
	TestEqual(TEXT("approver roundtrips"), Parsed.Approver, Marker.Approver);
	TestEqual(TEXT("approved timestamp roundtrips"), Parsed.ApprovedAtUtc, Marker.ApprovedAtUtc);

	const UnrealMcp::FVettedToolsetMarker Absent = UnrealMcp::DeserializeVettedToolsetMarker(MakeShared<FJsonObject>());
	const UnrealMcp::FVettedToolsetVerificationResult AbsentResult = UnrealMcp::VerifyVettedToolset_Pure(Absent, MainPy);
	TestFalse(TEXT("absent marker is not vetted"), AbsentResult.bVetted);
	TestEqual(TEXT("absent marker reason"), AbsentResult.Reason, TEXT("not_marked"));

	TSharedPtr<FJsonObject> MalformedManifest = MakeShared<FJsonObject>();
	MalformedManifest->SetStringField(TEXT("vettedMarker"), TEXT("not an object"));
	const UnrealMcp::FVettedToolsetMarker Malformed = UnrealMcp::DeserializeVettedToolsetMarker(MalformedManifest);
	const UnrealMcp::FVettedToolsetVerificationResult MalformedResult = UnrealMcp::VerifyVettedToolset_Pure(Malformed, MainPy);
	TestFalse(TEXT("malformed marker is not vetted"), MalformedResult.bVetted);
	TestEqual(TEXT("malformed marker reason"), MalformedResult.Reason, TEXT("not_marked"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpVettedToolsetVerificationTest,
	"UnrealMcp.VettedToolset.Verify",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpVettedToolsetVerificationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString MainPy = TEXT("print('ok')\n");
	const FString MainPySha = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy);
	UnrealMcp::FVettedToolsetMarker Marker;
	Marker.bVetted = true;
	Marker.MainPySha256 = MainPySha;

	const UnrealMcp::FVettedToolsetVerificationResult Match = UnrealMcp::VerifyVettedToolset_Pure(Marker, MainPy);
	TestTrue(TEXT("exact hash match is vetted"), Match.bVetted);
	TestEqual(TEXT("exact hash match reason"), Match.Reason, TEXT("vetted"));

	const UnrealMcp::FVettedToolsetVerificationResult ShaMatch = UnrealMcp::VerifyVettedToolsetSha_Pure(Marker, MainPySha);
	TestTrue(TEXT("precomputed sha match is vetted"), ShaMatch.bVetted);
	TestEqual(TEXT("precomputed sha match reason"), ShaMatch.Reason, TEXT("vetted"));

	const UnrealMcp::FVettedToolsetVerificationResult Mismatch = UnrealMcp::VerifyVettedToolset_Pure(Marker, TEXT("print('changed')\n"));
	TestFalse(TEXT("changed content is not vetted"), Mismatch.bVetted);
	TestEqual(TEXT("changed content reason"), Mismatch.Reason, TEXT("hash_mismatch"));

	const UnrealMcp::FVettedToolsetVerificationResult ShaMismatch = UnrealMcp::VerifyVettedToolsetSha_Pure(Marker, UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(TEXT("print('changed')\n")));
	TestFalse(TEXT("changed sha is not vetted"), ShaMismatch.bVetted);
	TestEqual(TEXT("changed sha reason"), ShaMismatch.Reason, TEXT("hash_mismatch"));

	Marker.MainPySha256.Reset();
	const UnrealMcp::FVettedToolsetVerificationResult MissingHash = UnrealMcp::VerifyVettedToolset_Pure(Marker, MainPy);
	TestFalse(TEXT("empty stored hash is not vetted"), MissingHash.bVetted);
	TestEqual(TEXT("empty stored hash reason"), MissingHash.Reason, TEXT("hash_missing"));

	const UnrealMcp::FVettedToolsetVerificationResult ShaMissingHash = UnrealMcp::VerifyVettedToolsetSha_Pure(Marker, MainPySha);
	TestFalse(TEXT("empty stored hash is not vetted with sha"), ShaMissingHash.bVetted);
	TestEqual(TEXT("empty stored hash sha reason"), ShaMissingHash.Reason, TEXT("hash_missing"));

	Marker.bVetted = false;
	Marker.MainPySha256 = MainPySha;
	const UnrealMcp::FVettedToolsetVerificationResult NotMarked = UnrealMcp::VerifyVettedToolsetSha_Pure(Marker, MainPySha);
	TestFalse(TEXT("not-marked sha is not vetted"), NotMarked.bVetted);
	TestEqual(TEXT("not-marked sha reason"), NotMarked.Reason, TEXT("not_marked"));

	Marker.bVetted = true;
	const UnrealMcp::FVettedToolsetVerificationResult DelegatedString = UnrealMcp::VerifyVettedToolset_Pure(Marker, MainPy);
	const UnrealMcp::FVettedToolsetVerificationResult DelegatedSha = UnrealMcp::VerifyVettedToolsetSha_Pure(Marker, MainPySha);
	TestEqual(TEXT("string verifier delegates to sha verifier vetted flag"), DelegatedString.bVetted, DelegatedSha.bVetted);
	TestEqual(TEXT("string verifier delegates to sha verifier reason"), DelegatedString.Reason, DelegatedSha.Reason);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpVettedToolsetScopeTest,
	"UnrealMcp.VettedToolset.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpVettedToolsetScopeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestFalse(TEXT("scope starts inactive"), UnrealMcp::FVettedToolsetScope::IsActive());
	TestEqual(TEXT("inactive tool id empty"), UnrealMcp::FVettedToolsetScope::ActiveToolId(), FString());
	TestEqual(TEXT("inactive verified sha empty"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha(), FString());
	{
		UnrealMcp::FVettedToolsetScope Outer(TEXT("user.outer"), TEXT("outer-sha"));
		TestTrue(TEXT("scope active inside outer"), UnrealMcp::FVettedToolsetScope::IsActive());
		TestEqual(TEXT("outer tool id active"), UnrealMcp::FVettedToolsetScope::ActiveToolId(), TEXT("user.outer"));
		TestEqual(TEXT("outer sha active"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha(), TEXT("outer-sha"));
		{
			UnrealMcp::FVettedToolsetScope Inner(TEXT("user.inner"), TEXT("inner-sha"));
			TestTrue(TEXT("scope remains active inside nested"), UnrealMcp::FVettedToolsetScope::IsActive());
			TestEqual(TEXT("inner tool id active"), UnrealMcp::FVettedToolsetScope::ActiveToolId(), TEXT("user.inner"));
			TestEqual(TEXT("inner sha active"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha(), TEXT("inner-sha"));
		}
		TestTrue(TEXT("scope remains active after nested exits"), UnrealMcp::FVettedToolsetScope::IsActive());
		TestEqual(TEXT("outer tool id restored"), UnrealMcp::FVettedToolsetScope::ActiveToolId(), TEXT("user.outer"));
		TestEqual(TEXT("outer sha restored"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha(), TEXT("outer-sha"));
	}
	TestFalse(TEXT("scope inactive after destruction"), UnrealMcp::FVettedToolsetScope::IsActive());
	TestEqual(TEXT("destroyed scope tool id empty"), UnrealMcp::FVettedToolsetScope::ActiveToolId(), FString());
	TestEqual(TEXT("destroyed scope sha empty"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha(), FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpVettedToolsetSourcePolicyTest,
	"UnrealMcp.VettedToolset.SourcePolicy.Matrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpVettedToolsetSourcePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString TemplateMainPy =
		TEXT("# Generated by Task Atlas as a preview composite user tool.\n")
		TEXT("import json\n\n")
		TEXT("_CAPTURED_JSON = \"{}\"\n")
		TEXT("_CAPTURED = json.loads(_CAPTURED_JSON)\n")
		TEXT("_MISSING = object()\n\n")
		TEXT("def _json_copy(value):\n")
		TEXT("    return json.loads(json.dumps(value or {}))\n\n")
		TEXT("def _step_args(args, override_key, captured_key):\n")
		TEXT("    value = args.get(override_key) if override_key in args else _CAPTURED.get(captured_key, {})\n")
		TEXT("    if not isinstance(value, dict):\n")
		TEXT("        value = {}\n")
		TEXT("    return _json_copy(value)\n\n")
		TEXT("def execute(args):\n")
		TEXT("    args = args or {}\n")
		TEXT("    step0_requested = _step_args(args, \"step0_args\", \"step0\")\n")
		TEXT("    r0 = call_tool_raw(\"unreal.editor_status\", step0_requested)\n")
		TEXT("    steps = [{\"tool\": \"unreal.editor_status\", \"isError\": bool(r0.get(\"isError\", False))}]\n")
		TEXT("    return {\"isError\": False, \"text\": \"preview composite executed 1 steps\", \"structuredContent\": {\"steps\": steps}}\n");

	const UnrealMcp::FCompositeSourcePolicyResult TemplateResult =
		UnrealMcp::ValidateCompositeSourcePolicy_Pure(TemplateMainPy);
	TestTrue(TEXT("template-shaped fixture passes"), TemplateResult.bPassed);
	TestEqual(TEXT("template-shaped fixture has no violations"), TemplateResult.Violations.Num(), 0);

	struct FBadSourceCase
	{
		const TCHAR* Name;
		const TCHAR* Source;
		const TCHAR* Code;
	};

	const FBadSourceCase BadCases[] = {
		{ TEXT("import os"), TEXT("import os\n"), TEXT("forbidden_import") },
		{ TEXT("from subprocess"), TEXT("from subprocess import run\n"), TEXT("forbidden_import") },
		{ TEXT("import unreal"), TEXT("import unreal\n"), TEXT("forbidden_import") },
		{ TEXT("bare unreal"), TEXT("value = unreal.EditorLevelLibrary\n"), TEXT("unreal_direct") },
		{ TEXT("builtins dunder"), TEXT("value = __builtins__\n"), TEXT("dunder_token") },
		{ TEXT("globals dunder split string"), TEXT("def f():\n    pass\nvalue = f.__globals__[\"__im\" + \"port__\"]\n"), TEXT("dunder_token") },
		{ TEXT("globals import lookup"), TEXT("value = globals()[\"__import__\"]\n"), TEXT("dynamic_access") },
		{ TEXT("getattr chain"), TEXT("value = getattr(obj, \"name\")()\n"), TEXT("dynamic_access") },
		{ TEXT("eval call"), TEXT("value = eval(\"1 + 1\")\n"), TEXT("forbidden_call") },
		{ TEXT("chr call"), TEXT("value = chr(119)\n"), TEXT("forbidden_call") },
		{ TEXT("open call"), TEXT("value = open(\"data.json\", \"r\")\n"), TEXT("file_io") },
		{ TEXT("backslash import"), TEXT("imp\\\nort os\n"), TEXT("forbidden_import") },
		{ TEXT("empty source"), TEXT("  \n\t"), TEXT("empty_source") },
	};
	for (const FBadSourceCase& TestCase : BadCases)
	{
		const UnrealMcp::FCompositeSourcePolicyResult Result =
			UnrealMcp::ValidateCompositeSourcePolicy_Pure(TestCase.Source);
		TestFalse(*FString::Printf(TEXT("%s is blocked"), TestCase.Name), Result.bPassed);
		TestTrue(
			*FString::Printf(TEXT("%s contains %s"), TestCase.Name, TestCase.Code),
			UnrealMcpVettedToolsetTests::HasViolationCode(Result, TestCase.Code));
	}

	const FString AllowedImports =
		TEXT("# __builtins__ in a comment is ignored\n")
		TEXT("\"\"\"\n")
		TEXT("__globals__ in a docstring is ignored\n")
		TEXT("\"\"\"\n")
		TEXT("import json\n")
		TEXT("from datetime import datetime\n")
		TEXT("import re\n")
		TEXT("import math\n")
		TEXT("import uuid\n")
		TEXT("def execute(args):\n")
		TEXT("    return {\"isError\": False, \"now\": str(datetime.utcnow())}\n");
	const UnrealMcp::FCompositeSourcePolicyResult AllowedResult =
		UnrealMcp::ValidateCompositeSourcePolicy_Pure(AllowedImports);
	TestTrue(TEXT("comments docstrings and allowed imports pass"), AllowedResult.bPassed);
	TestEqual(TEXT("comments docstrings and allowed imports have no violations"), AllowedResult.Violations.Num(), 0);

	const UnrealMcp::FCompositeSourcePolicyResult StringTokenResult =
		UnrealMcp::ValidateCompositeSourcePolicy_Pure(TEXT("path = \"os.path in a string\"\n"));
	TestTrue(TEXT("forbidden module token inside a string passes"), StringTokenResult.bPassed);
	TestEqual(TEXT("forbidden module token string has no violations"), StringTokenResult.Violations.Num(), 0);

	const UnrealMcp::FCompositeSourcePolicyResult StringDunderResult =
		UnrealMcp::ValidateCompositeSourcePolicy_Pure(TEXT("s = \"__import__\"\n"));
	TestFalse(TEXT("dunder token inside a string fails"), StringDunderResult.bPassed);
	TestTrue(
		TEXT("dunder token inside a string reports dunder_token"),
		UnrealMcpVettedToolsetTests::HasViolationCode(StringDunderResult, TEXT("dunder_token")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpVettedToolsetAllowlistPolicyTest,
	"UnrealMcp.VettedToolset.SourcePolicy.Allowlist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpVettedToolsetAllowlistPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FString> EmptyAllowlist;
	const UnrealMcp::FCompositeSourcePolicyResult Empty =
		UnrealMcp::IsImportAllowlistVettable_Pure(EmptyAllowlist);
	TestTrue(TEXT("empty allowlist passes"), Empty.bPassed);
	TestEqual(TEXT("empty allowlist has no violations"), Empty.Violations.Num(), 0);

	const TArray<FString> AllowedAllowlist = { TEXT("json"), TEXT("math") };
	const UnrealMcp::FCompositeSourcePolicyResult Allowed =
		UnrealMcp::IsImportAllowlistVettable_Pure(AllowedAllowlist);
	TestTrue(TEXT("json math allowlist passes"), Allowed.bPassed);
	TestEqual(TEXT("json math allowlist has no violations"), Allowed.Violations.Num(), 0);

	const TArray<FString> BlockedAllowlist = { TEXT("json"), TEXT("subprocess") };
	const UnrealMcp::FCompositeSourcePolicyResult Blocked =
		UnrealMcp::IsImportAllowlistVettable_Pure(BlockedAllowlist);
	TestFalse(TEXT("subprocess allowlist fails"), Blocked.bPassed);
	TestTrue(
		TEXT("subprocess allowlist reports allowlist_not_vettable"),
		UnrealMcpVettedToolsetTests::HasViolationCode(Blocked, TEXT("allowlist_not_vettable")));

	return true;
}

#endif
