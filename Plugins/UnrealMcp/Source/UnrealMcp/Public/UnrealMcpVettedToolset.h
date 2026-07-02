#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FJsonObject;

namespace UnrealMcp
{
	struct FVettedToolsetMarker
	{
		int32 SchemaVersion = 1;
		bool bVetted = false;
		FString MainPySha256;
		FString AiReviewVerdict;
		FString AiReviewSummary;
		FString SmokeStatus;
		FString Approver;
		FString ApprovedAtUtc;
	};

	struct FVettedToolsetVerificationResult
	{
		bool bVetted = false;
		FString Reason;
	};

	struct FCompositeSourcePolicyResult
	{
		bool bPassed = false;
		TArray<FString> Violations;
	};

	UNREALMCP_API TSharedPtr<FJsonObject> SerializeVettedToolsetMarker(const FVettedToolsetMarker& Marker);
	UNREALMCP_API FVettedToolsetMarker DeserializeVettedToolsetMarker(const TSharedPtr<FJsonObject>& ManifestJson);
	UNREALMCP_API FVettedToolsetVerificationResult VerifyVettedToolset_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyUtf8);
	UNREALMCP_API FVettedToolsetVerificationResult VerifyVettedToolsetSha_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyShaLowerHex);
	// Fail-closed heuristic scan for generated composite main.py files. This is
	// intentionally not a Python AST parser; runtime import hooks and human review
	// remain the final backstops, and over-blocking is preferable to under-blocking.
	// Dunder tokens are banned even inside strings; other forbidden code tokens are
	// scanned with ordinary string contents blanked to allow quoted tool names.
	UNREALMCP_API FCompositeSourcePolicyResult ValidateCompositeSourcePolicy_Pure(const FString& MainPyUtf8);
	UNREALMCP_API FCompositeSourcePolicyResult IsImportAllowlistVettable_Pure(const TArray<FString>& ImportAllowlist);

	class UNREALMCP_API FVettedToolsetScope
	{
	public:
		explicit FVettedToolsetScope(const FString& InToolId, const FString& InVerifiedSha = FString());
		~FVettedToolsetScope();

		FVettedToolsetScope(const FVettedToolsetScope&) = delete;
		FVettedToolsetScope& operator=(const FVettedToolsetScope&) = delete;

		static bool IsActive();
		static FString ActiveToolId();
		static FString ActiveVerifiedSha();

	private:
		FString ToolId;
		FString VerifiedSha;
	};
}
