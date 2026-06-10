#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealMcp
{
namespace Protocol
{
	const FString& GetLatestProtocolVersion();
	FString NegotiateProtocolVersion(const FString& RequestedProtocolVersion);
	TSharedPtr<FJsonObject> BuildInitializeResult(const FString& RequestedProtocolVersion, const FString& EndpointUrl);
	TSharedPtr<FJsonObject> BuildPingResult();
	TSharedPtr<FJsonObject> BuildToolsListResult(const TArray<TSharedPtr<FJsonValue>>& ToolsArray);
	TSharedPtr<FJsonObject> BuildToolCallResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	TSharedPtr<FJsonObject> BuildJsonRpcResultEnvelope(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
	TSharedPtr<FJsonObject> BuildJsonRpcErrorEnvelope(const TSharedPtr<FJsonValue>& Id, int32 ErrorCode, const FString& ErrorMessage);
}
}
