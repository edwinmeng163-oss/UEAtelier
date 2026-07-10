#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UnrealMcpEngineCompat.h"
#include "UObject/NameTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpEngineCompatFStringOutputDeviceTest,
	"UnrealMcp.EngineCompat.FStringOutputDevice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpEngineCompatFStringOutputDeviceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FStringOutputDevice Output(TEXT("prefix:"));
	Output.Serialize(TEXT("payload"), ELogVerbosity::Display, FName(TEXT("UnrealMcpEngineCompat")));

	TestEqual(
		TEXT("FStringOutputDevice preserves its initial text and appends serialized text"),
		static_cast<const FString&>(Output),
		FString(TEXT("prefix:payload")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpEngineCompatJsonObjectTest,
	"UnrealMcp.EngineCompat.JsonObject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpEngineCompatJsonObjectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FJsonObject Object;
	Object.SetStringField(TEXT("existing"), TEXT("payload"));
	Object.SetStringField(TEXT("蓝图"), TEXT("节点"));

	const TSharedPtr<FJsonValue>* ExistingValue = UnrealMcp::Compat::FindJsonValue(Object, TEXT("existing"));
	TestTrue(TEXT("FindJsonValue returns an existing ASCII-keyed value"), ExistingValue && ExistingValue->IsValid());
	if (ExistingValue && ExistingValue->IsValid())
	{
		TestEqual(TEXT("Existing value is preserved"), (*ExistingValue)->AsString(), FString(TEXT("payload")));
	}

	TestTrue(
		TEXT("FindJsonValue returns null for a missing key"),
		UnrealMcp::Compat::FindJsonValue(Object, TEXT("missing")) == nullptr);

	const TSharedPtr<FJsonValue>* NonAsciiValue = UnrealMcp::Compat::FindJsonValue(Object, TEXT("蓝图"));
	TestTrue(TEXT("FindJsonValue supports a non-ASCII key"), NonAsciiValue && NonAsciiValue->IsValid());
	if (NonAsciiValue && NonAsciiValue->IsValid())
	{
		TestEqual(TEXT("Non-ASCII-keyed value is preserved"), (*NonAsciiValue)->AsString(), FString(TEXT("节点")));
	}

	TArray<FString> Keys;
	UnrealMcp::Compat::GetJsonObjectKeys(Object, Keys);
	Keys.Sort();
	TArray<FString> ExpectedKeys = { TEXT("existing"), TEXT("蓝图") };
	ExpectedKeys.Sort();
	TestEqual(
		TEXT("GetJsonObjectKeys enumerates every key exactly once"),
		FString::Join(Keys, TEXT(",")),
		FString::Join(ExpectedKeys, TEXT(",")));

	return true;
}

#endif
