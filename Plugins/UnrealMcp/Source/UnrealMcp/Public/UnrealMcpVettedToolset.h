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

	UNREALMCP_API TSharedPtr<FJsonObject> SerializeVettedToolsetMarker(const FVettedToolsetMarker& Marker);
	UNREALMCP_API FVettedToolsetMarker DeserializeVettedToolsetMarker(const TSharedPtr<FJsonObject>& ManifestJson);
	UNREALMCP_API FVettedToolsetVerificationResult VerifyVettedToolset_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyUtf8);
	UNREALMCP_API FVettedToolsetVerificationResult VerifyVettedToolsetSha_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyShaLowerHex);

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
