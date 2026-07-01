#include "UnrealMcpVettedToolset.h"

#include "UnrealMcpHashUtils.h"

namespace UnrealMcp
{
	namespace
	{
		static thread_local int32 GVettedToolsetScopeDepth = 0;

		const TCHAR* VettedToolsetMarkerField()
		{
			return TEXT("vettedMarker");
		}

		void ReadOptionalStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FString& OutValue)
		{
			if (Object.IsValid())
			{
				FString Value;
				if (Object->TryGetStringField(FieldName, Value))
				{
					OutValue = Value;
				}
			}
		}
	}

	TSharedPtr<FJsonObject> SerializeVettedToolsetMarker(const FVettedToolsetMarker& Marker)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("schemaVersion"), Marker.SchemaVersion);
		Object->SetBoolField(TEXT("vetted"), Marker.bVetted);
		Object->SetStringField(TEXT("mainPySha256"), Marker.MainPySha256);
		Object->SetStringField(TEXT("aiReviewVerdict"), Marker.AiReviewVerdict);
		Object->SetStringField(TEXT("aiReviewSummary"), Marker.AiReviewSummary);
		Object->SetStringField(TEXT("smokeStatus"), Marker.SmokeStatus);
		Object->SetStringField(TEXT("approver"), Marker.Approver);
		Object->SetStringField(TEXT("approvedAtUtc"), Marker.ApprovedAtUtc);
		return Object;
	}

	FVettedToolsetMarker DeserializeVettedToolsetMarker(const TSharedPtr<FJsonObject>& ManifestJson)
	{
		FVettedToolsetMarker Marker;
		const TSharedPtr<FJsonObject>* MarkerObject = nullptr;
		if (!ManifestJson.IsValid()
			|| !ManifestJson->TryGetObjectField(VettedToolsetMarkerField(), MarkerObject)
			|| MarkerObject == nullptr
			|| !MarkerObject->IsValid())
		{
			return Marker;
		}

		double SchemaVersion = 0.0;
		bool bVetted = false;
		FString MainPySha256;
		if (!(*MarkerObject)->TryGetNumberField(TEXT("schemaVersion"), SchemaVersion)
			|| !(*MarkerObject)->TryGetBoolField(TEXT("vetted"), bVetted)
			|| !(*MarkerObject)->TryGetStringField(TEXT("mainPySha256"), MainPySha256))
		{
			return FVettedToolsetMarker();
		}

		Marker.SchemaVersion = static_cast<int32>(SchemaVersion);
		Marker.bVetted = bVetted;
		Marker.MainPySha256 = MainPySha256.TrimStartAndEnd();
		ReadOptionalStringField(*MarkerObject, TEXT("aiReviewVerdict"), Marker.AiReviewVerdict);
		ReadOptionalStringField(*MarkerObject, TEXT("aiReviewSummary"), Marker.AiReviewSummary);
		ReadOptionalStringField(*MarkerObject, TEXT("smokeStatus"), Marker.SmokeStatus);
		ReadOptionalStringField(*MarkerObject, TEXT("approver"), Marker.Approver);
		ReadOptionalStringField(*MarkerObject, TEXT("approvedAtUtc"), Marker.ApprovedAtUtc);
		return Marker;
	}

	FVettedToolsetVerificationResult VerifyVettedToolset_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyUtf8)
	{
		if (!Marker.bVetted)
		{
			return { false, TEXT("not_marked") };
		}

		if (Marker.MainPySha256.IsEmpty())
		{
			return { false, TEXT("hash_missing") };
		}

		const FString LiveSha256 = HashUtils::Sha256LowerHexFromUtf8(LiveMainPyUtf8);
		if (!Marker.MainPySha256.Equals(LiveSha256, ESearchCase::CaseSensitive))
		{
			return { false, TEXT("hash_mismatch") };
		}

		return { true, TEXT("vetted") };
	}

	FVettedToolsetScope::FVettedToolsetScope(const FString& InToolId)
		: ToolId(InToolId)
	{
		++GVettedToolsetScopeDepth;
	}

	FVettedToolsetScope::~FVettedToolsetScope()
	{
		--GVettedToolsetScopeDepth;
	}

	bool FVettedToolsetScope::IsActive()
	{
		return GVettedToolsetScopeDepth > 0;
	}
}
