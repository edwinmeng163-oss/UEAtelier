#pragma once

// Central Unreal Engine compatibility shim. This is the only plugin file that
// may contain #if ENGINE_*_VERSION; route version differences here. Let this
// file grow until ~200 lines before splitting it into a subdirectory.
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Runtime/Launch/Resources/Version.h"

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Misc/StringOutputDevice.h"
#else
#include "Containers/UnrealString.h"
#endif

namespace UnrealMcp::Compat
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
	inline const TSharedPtr<FJsonValue>* FindJsonValue(const FJsonObject& Object, const FString& Key)
	{
		const FStringView KeyView(Key);
		return Object.Values.FindByHash(GetTypeHash(KeyView), KeyView);
	}

	inline void GetJsonObjectKeys(const FJsonObject& Object, TArray<FString>& OutKeys)
	{
		OutKeys.Reset(Object.Values.Num());
		for (const auto& Pair : Object.Values)
		{
			OutKeys.Add(FString(Pair.Key.ToView()));
		}
	}
#else
	inline const TSharedPtr<FJsonValue>* FindJsonValue(const FJsonObject& Object, const FString& Key)
	{
		return Object.Values.Find(Key);
	}

	inline void GetJsonObjectKeys(const FJsonObject& Object, TArray<FString>& OutKeys)
	{
		Object.Values.GetKeys(OutKeys);
	}
#endif
}
