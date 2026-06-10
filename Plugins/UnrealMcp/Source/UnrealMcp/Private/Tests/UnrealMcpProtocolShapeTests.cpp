#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpProtocolBuilders.h"

namespace
{
	TArray<FString> SortedKeys(const FJsonObject& Object)
	{
		TArray<FString> Keys;
		Object.Values.GetKeys(Keys);
		Keys.Sort();
		return Keys;
	}

	bool TestExactKeys(FAutomationTestBase& Test, const FString& Label, const FJsonObject& Object, TArray<FString> ExpectedKeys)
	{
		ExpectedKeys.Sort();
		const TArray<FString> ActualKeys = SortedKeys(Object);
		Test.TestEqual(
			*FString::Printf(TEXT("%s exact keys"), *Label),
			FString::Join(ActualKeys, TEXT(",")),
			FString::Join(ExpectedKeys, TEXT(",")));
		return ActualKeys == ExpectedKeys;
	}

	TSharedPtr<FJsonObject> GetObjectField(FAutomationTestBase& Test, const FJsonObject& Object, const FString& FieldName)
	{
		const TSharedPtr<FJsonObject>* FieldObject = nullptr;
		const bool bHasObject = Object.TryGetObjectField(FieldName, FieldObject) && FieldObject && FieldObject->IsValid();
		Test.TestTrue(*FString::Printf(TEXT("%s is object"), *FieldName), bHasObject);
		return bHasObject ? *FieldObject : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetArrayField(FAutomationTestBase& Test, const FJsonObject& Object, const FString& FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		Test.TestTrue(*FString::Printf(TEXT("%s is array"), *FieldName), Object.TryGetArrayField(FieldName, ArrayField) && ArrayField);
		return ArrayField;
	}

	void TestTextContentObject(FAutomationTestBase& Test, const FJsonObject& ResultObject, const FString& ExpectedText)
	{
		const TArray<TSharedPtr<FJsonValue>>* ContentArray = GetArrayField(Test, ResultObject, TEXT("content"));
		if (!ContentArray)
		{
			return;
		}

		Test.TestEqual(TEXT("content has one item"), ContentArray->Num(), 1);
		if (ContentArray->Num() != 1 || !(*ContentArray)[0].IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject> ContentObject = (*ContentArray)[0]->AsObject();
		Test.TestTrue(TEXT("content[0] is object"), ContentObject.IsValid());
		if (!ContentObject.IsValid())
		{
			return;
		}

		TestExactKeys(Test, TEXT("content[0]"), *ContentObject, { TEXT("text"), TEXT("type") });
		Test.TestEqual(TEXT("content[0].type"), ContentObject->GetStringField(TEXT("type")), TEXT("text"));
		Test.TestEqual(TEXT("content[0].text"), ContentObject->GetStringField(TEXT("text")), ExpectedText);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProtocolInitializeShapeTest,
	"UnrealMcp.Protocol.InitializeShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProtocolInitializeShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString LatestProtocolVersion = UnrealMcp::Protocol::GetLatestProtocolVersion();
	const TSharedPtr<FJsonObject> Result = UnrealMcp::Protocol::BuildInitializeResult(
		LatestProtocolVersion,
		TEXT("http://127.0.0.1:8765/mcp"));

	TestExactKeys(*this, TEXT("initialize result"), *Result, {
		TEXT("capabilities"),
		TEXT("instructions"),
		TEXT("protocolVersion"),
		TEXT("serverInfo")
	});
	TestEqual(TEXT("supported protocol version echoed"), Result->GetStringField(TEXT("protocolVersion")), LatestProtocolVersion);

	const TSharedPtr<FJsonObject> Capabilities = GetObjectField(*this, *Result, TEXT("capabilities"));
	if (Capabilities.IsValid())
	{
		TestExactKeys(*this, TEXT("capabilities"), *Capabilities, { TEXT("tools") });
		const TSharedPtr<FJsonObject> ToolsCapabilities = GetObjectField(*this, *Capabilities, TEXT("tools"));
		if (ToolsCapabilities.IsValid())
		{
			bool bListChanged = true;
			TestExactKeys(*this, TEXT("capabilities.tools"), *ToolsCapabilities, { TEXT("listChanged") });
			TestTrue(TEXT("capabilities.tools.listChanged present"), ToolsCapabilities->TryGetBoolField(TEXT("listChanged"), bListChanged));
			TestFalse(TEXT("capabilities.tools.listChanged false"), bListChanged);
		}
	}

	const TSharedPtr<FJsonObject> ServerInfo = GetObjectField(*this, *Result, TEXT("serverInfo"));
	if (ServerInfo.IsValid())
	{
		TestExactKeys(*this, TEXT("serverInfo"), *ServerInfo, { TEXT("name"), TEXT("version") });
	}

	const TSharedPtr<FJsonObject> LegacyResult = UnrealMcp::Protocol::BuildInitializeResult(
		TEXT("2025-03-26"),
		TEXT("http://127.0.0.1:8765/mcp"));
	TestEqual(TEXT("legacy supported protocol version echoed"), LegacyResult->GetStringField(TEXT("protocolVersion")), TEXT("2025-03-26"));

	const TSharedPtr<FJsonObject> GarbageResult = UnrealMcp::Protocol::BuildInitializeResult(
		TEXT("garbage"),
		TEXT("http://127.0.0.1:8765/mcp"));
	TestEqual(TEXT("garbage protocol version falls back to latest"), GarbageResult->GetStringField(TEXT("protocolVersion")), LatestProtocolVersion);

	const TSharedPtr<FJsonObject> EmptyResult = UnrealMcp::Protocol::BuildInitializeResult(
		FString(),
		TEXT("http://127.0.0.1:8765/mcp"));
	TestEqual(TEXT("empty protocol version falls back to latest"), EmptyResult->GetStringField(TEXT("protocolVersion")), LatestProtocolVersion);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProtocolToolsListShapeTest,
	"UnrealMcp.Protocol.ToolsListShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProtocolToolsListShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
	ToolObject->SetStringField(TEXT("name"), TEXT("unreal.test"));

	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObject));

	const TSharedPtr<FJsonObject> Result = UnrealMcp::Protocol::BuildToolsListResult(ToolsArray);
	TestExactKeys(*this, TEXT("tools/list result"), *Result, { TEXT("tools") });
	TestFalse(TEXT("tools/list result has no structuredContent"), Result->HasField(TEXT("structuredContent")));

	const TArray<TSharedPtr<FJsonValue>>* ResultTools = GetArrayField(*this, *Result, TEXT("tools"));
	TestTrue(TEXT("tools/list tools array non-empty"), ResultTools && ResultTools->Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProtocolToolCallShapeTest,
	"UnrealMcp.Protocol.ToolCallShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProtocolToolCallShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TSharedPtr<FJsonObject> TextOnlyResult = UnrealMcp::Protocol::BuildToolCallResult(TEXT("ok"), TSharedPtr<FJsonObject>(), false);
	TestExactKeys(*this, TEXT("tool-call text-only result"), *TextOnlyResult, { TEXT("content"), TEXT("isError") });
	TestTextContentObject(*this, *TextOnlyResult, TEXT("ok"));

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("message"), TEXT("ok"));
	const TSharedPtr<FJsonObject> StructuredResult = UnrealMcp::Protocol::BuildToolCallResult(TEXT("ok"), StructuredContent, false);
	TestExactKeys(*this, TEXT("tool-call structured result"), *StructuredResult, {
		TEXT("content"),
		TEXT("isError"),
		TEXT("structuredContent")
	});
	TestTextContentObject(*this, *StructuredResult, TEXT("ok"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProtocolPingShapeTest,
	"UnrealMcp.Protocol.PingShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProtocolPingShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TSharedPtr<FJsonObject> Result = UnrealMcp::Protocol::BuildPingResult();
	TestExactKeys(*this, TEXT("ping result"), *Result, TArray<FString>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProtocolEnvelopeShapeTest,
	"UnrealMcp.Protocol.EnvelopeShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProtocolEnvelopeShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("ok"), TEXT("yes"));

	const TSharedPtr<FJsonObject> SuccessEnvelope = UnrealMcp::Protocol::BuildJsonRpcResultEnvelope(
		MakeShared<FJsonValueString>(TEXT("abc")),
		ResultObject);
	TestExactKeys(*this, TEXT("success envelope"), *SuccessEnvelope, {
		TEXT("id"),
		TEXT("jsonrpc"),
		TEXT("result")
	});
	TestEqual(TEXT("success envelope jsonrpc"), SuccessEnvelope->GetStringField(TEXT("jsonrpc")), TEXT("2.0"));

	const TSharedPtr<FJsonObject> ErrorEnvelope = UnrealMcp::Protocol::BuildJsonRpcErrorEnvelope(
		MakeShared<FJsonValueString>(TEXT("abc")),
		-32600,
		TEXT("bad request"));
	TestExactKeys(*this, TEXT("error envelope"), *ErrorEnvelope, {
		TEXT("error"),
		TEXT("id"),
		TEXT("jsonrpc")
	});
	TestEqual(TEXT("error envelope jsonrpc"), ErrorEnvelope->GetStringField(TEXT("jsonrpc")), TEXT("2.0"));

	const TSharedPtr<FJsonObject> ErrorObject = GetObjectField(*this, *ErrorEnvelope, TEXT("error"));
	if (ErrorObject.IsValid())
	{
		TestExactKeys(*this, TEXT("error object"), *ErrorObject, { TEXT("code"), TEXT("message") });
		double ErrorCode = 0.0;
		TestTrue(TEXT("error code present"), ErrorObject->TryGetNumberField(TEXT("code"), ErrorCode));
		TestEqual(TEXT("error code"), static_cast<int32>(ErrorCode), -32600);
		TestEqual(TEXT("error message"), ErrorObject->GetStringField(TEXT("message")), TEXT("bad request"));
	}

	const TSharedPtr<FJsonObject> NullIdErrorEnvelope = UnrealMcp::Protocol::BuildJsonRpcErrorEnvelope(
		TSharedPtr<FJsonValue>(),
		-32603,
		TEXT("internal"));
	TestExactKeys(*this, TEXT("null-id error envelope"), *NullIdErrorEnvelope, {
		TEXT("error"),
		TEXT("id"),
		TEXT("jsonrpc")
	});
	const TSharedPtr<FJsonValue> NullIdField = NullIdErrorEnvelope->TryGetField(TEXT("id"));
	TestTrue(TEXT("null-id error envelope keeps id field"), NullIdField.IsValid());
	if (NullIdField.IsValid())
	{
		TestTrue(TEXT("null-id error envelope id is JSON null"), NullIdField->Type == EJson::Null);
	}

	return true;
}

#endif
