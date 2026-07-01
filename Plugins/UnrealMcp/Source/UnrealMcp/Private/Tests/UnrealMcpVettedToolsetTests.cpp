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

#endif
