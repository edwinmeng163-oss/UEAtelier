#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/Function.h"
#include "UnrealMcpActivityLog.h"
#include "UnrealMcpCallToolLibrary.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"
#include "UnrealMcpVettedToolset.h"

namespace UnrealMcpCallToolLibraryLocal
{
	FString CallToolWithAuditWriter(
		const FString& ToolName,
		const FString& ArgumentsJson,
		TFunctionRef<bool(const UnrealMcp::FActivityLogEvent&, FString&)> AuditWriter);
}

namespace UnrealMcpCallToolLibraryTests
{
	const FString TestToolPrefix = TEXT("codex_call_tool_");
	const FString DemoToolId = TestToolPrefix + TEXT("demo");
	const FString DemoToolName = TEXT("user.") + DemoToolId;
	const FString DemoMainPy =
		TEXT("def execute(args):\n")
		TEXT("    status = call_tool(\"unreal.editor_status\")\n")
		TEXT("    actors = call_tool(\"unreal.list_level_actors\")\n")
		TEXT("    return {\n")
		TEXT("        \"isError\": False,\n")
		TEXT("        \"text\": \"call_tool re-entry ok\",\n")
		TEXT("        \"structuredContent\": {\n")
		TEXT("            \"reentry\": \"ok\",\n")
		TEXT("            \"editorStatusPolicy\": status[\"meta\"][\"policyDecision\"],\n")
		TEXT("            \"listActorsPolicy\": actors[\"meta\"][\"policyDecision\"],\n")
		TEXT("        },\n")
		TEXT("    }\n");
	const FString DemoMainSha = TEXT("40e795899b6aac815c8dbf6caa6abba2da5725beeb82599bc3f327cc98bb3228");

	bool ParseCallToolPayload(const FString& Json, TSharedPtr<FJsonObject>& OutPayload)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutPayload) && OutPayload.IsValid();
	}

	const TSharedPtr<FJsonObject>* GetObjectField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (Object.IsValid())
		{
			Object->TryGetObjectField(FieldName, Result);
		}
		return Result;
	}

	FString GetNestedString(const TSharedPtr<FJsonObject>& Object, const FString& ObjectField, const FString& StringField)
	{
		if (const TSharedPtr<FJsonObject>* Nested = GetObjectField(Object, ObjectField))
		{
			if (Nested && (*Nested).IsValid())
			{
				FString Value;
				(*Nested)->TryGetStringField(StringField, Value);
				return Value;
			}
		}
		return FString();
	}

	bool GetNestedBool(const TSharedPtr<FJsonObject>& Object, const FString& ObjectField, const FString& BoolField)
	{
		if (const TSharedPtr<FJsonObject>* Nested = GetObjectField(Object, ObjectField))
		{
			if (Nested && (*Nested).IsValid())
			{
				bool bValue = false;
				(*Nested)->TryGetBoolField(BoolField, bValue);
				return bValue;
			}
		}
		return false;
	}

	FString UserToolsRoot()
	{
		UnrealMcp::UserRegistry::InitializeUserToolRegistry();
		return UnrealMcp::UserRegistry::GetUserToolsRootDir();
	}

	FString DemoToolDir()
	{
		return FPaths::Combine(UserToolsRoot(), DemoToolId);
	}

	void DeleteDemoTool()
	{
		IFileManager::Get().DeleteDirectory(*DemoToolDir(), false, true);
	}

	bool WriteTextFile(const FString& Path, const FString& Content)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void ReloadUserTools(bool bAcceptChangedHashes)
	{
		UnrealMcp::UserToolLock::FExclusiveGuard Guard;
		UnrealMcp::UserRegistry::ReloadUserToolRegistry(bAcceptChangedHashes);
	}

	bool WriteDemoUserTool()
	{
		const FString ToolJson = FString::Printf(
			TEXT("{\n")
			TEXT("  \"name\": \"%s\",\n")
			TEXT("  \"title\": \"Call Tool Demo Test\",\n")
			TEXT("  \"description\": \"Automation fixture for call_tool re-entry.\",\n")
			TEXT("  \"pythonHandlerSha256\": \"%s\",\n")
			TEXT("  \"importAllowlist\": [],\n")
			TEXT("  \"inputSchema\": {\"type\":\"object\",\"additionalProperties\":true}\n")
			TEXT("}\n"),
			*DemoToolName,
			*DemoMainSha);
		return WriteTextFile(FPaths::Combine(DemoToolDir(), TEXT("main.py")), DemoMainPy)
			&& WriteTextFile(FPaths::Combine(DemoToolDir(), TEXT("tool.json")), ToolJson);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolReturnSchemaTest,
	"UnrealMcp.CallTool.ReturnSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolReturnSchemaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Payload;
	TestTrue(TEXT("payload parses"), UnrealMcpCallToolLibraryTests::ParseCallToolPayload(UUnrealMcpCallToolLibrary::CallTool(TEXT("unreal.editor_status"), TEXT("{}")), Payload));
	if (!Payload.IsValid())
	{
		return false;
	}

	bool bIsError = true;
	Payload->TryGetBoolField(TEXT("isError"), bIsError);
	TestFalse(TEXT("isError false"), bIsError);
	TestEqual(TEXT("policy allow"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("policyDecision")), TEXT("allow"));
	TestTrue(TEXT("structuredContent present"), Payload->HasTypedField<EJson::Object>(TEXT("structuredContent")));
	TestTrue(TEXT("engineVersion present"), Payload->GetObjectField(TEXT("structuredContent"))->HasField(TEXT("engineVersion")));
	TestTrue(TEXT("meta policyDecision present"), Payload->GetObjectField(TEXT("meta"))->HasField(TEXT("policyDecision")));
	TestTrue(TEXT("meta forcedDryRun present"), Payload->GetObjectField(TEXT("meta"))->HasField(TEXT("forcedDryRun")));
	TestTrue(TEXT("meta truncated present"), Payload->GetObjectField(TEXT("meta"))->HasField(TEXT("truncated")));
	TestTrue(TEXT("meta reason present"), Payload->GetObjectField(TEXT("meta"))->HasField(TEXT("reason")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolForceDryRunTest,
	"UnrealMcp.CallTool.ForceDryRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolForceDryRunTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Payload;
	TestTrue(TEXT("payload parses"), UnrealMcpCallToolLibraryTests::ParseCallToolPayload(UUnrealMcpCallToolLibrary::CallTool(TEXT("unreal.mcp_user_registry_reload"), TEXT("{}")), Payload));
	if (!Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("policy force dry run"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("policyDecision")), TEXT("force_dry_run"));
	TestTrue(TEXT("forced dry run true"), UnrealMcpCallToolLibraryTests::GetNestedBool(Payload, TEXT("meta"), TEXT("forcedDryRun")));
	TestTrue(TEXT("structured content present"), Payload->HasTypedField<EJson::Object>(TEXT("structuredContent")));
	if (Payload->HasTypedField<EJson::Object>(TEXT("structuredContent")))
	{
		TestTrue(TEXT("dryRun was injected into handler arguments"), Payload->GetObjectField(TEXT("structuredContent"))->GetBoolField(TEXT("dryRun")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolDenyTest,
	"UnrealMcp.CallTool.Deny",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolDenyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Payload;
	TestTrue(TEXT("payload parses"), UnrealMcpCallToolLibraryTests::ParseCallToolPayload(UUnrealMcpCallToolLibrary::CallTool(TEXT("unreal.execute_python"), TEXT("{}")), Payload));
	if (!Payload.IsValid())
	{
		return false;
	}

	bool bIsError = false;
	Payload->TryGetBoolField(TEXT("isError"), bIsError);
	TestTrue(TEXT("isError true"), bIsError);
	TestEqual(TEXT("policy deny"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("policyDecision")), TEXT("deny"));
	TestEqual(TEXT("deny reason"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("reason")), TEXT("dangerous_no_dryrun"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolTruncationTest,
	"UnrealMcp.CallTool.Truncation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolTruncationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AddInfo(TEXT("Skipped: no deterministic visible core tool currently returns more than 20000 text characters without mutating editor state."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolVettedRealAuditTest,
	"UnrealMcp.CallTool.VettedRealAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolVettedRealAuditTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString VerifiedSha = TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
	TArray<UnrealMcp::FActivityLogEvent> Events;
	TSharedPtr<FJsonObject> Payload;
	{
		UnrealMcp::FVettedToolsetScope Scope(TEXT("user.test_vetted"), VerifiedSha);
		TestTrue(
			TEXT("payload parses"),
			UnrealMcpCallToolLibraryTests::ParseCallToolPayload(
				UnrealMcpCallToolLibraryLocal::CallToolWithAuditWriter(
					TEXT("unreal.code_apply_change"),
					TEXT("{\"previewId\":\"missing_vetted_preview\",\"dryRun\":false,\"token\":\"secret-token-value\"}"),
					[&Events](const UnrealMcp::FActivityLogEvent& Event, FString& FailureReason)
					{
						FailureReason.Reset();
						Events.Add(Event);
						return true;
					}),
				Payload));
	}

	if (!Payload.IsValid())
	{
		return false;
	}

	bool bIsError = false;
	Payload->TryGetBoolField(TEXT("isError"), bIsError);
	TestTrue(TEXT("harmless real handler failure is surfaced"), bIsError);
	TestTrue(TEXT("handler attempted missing preview id"), Payload->GetStringField(TEXT("text")).Contains(TEXT("Preview artifact not found")));
	TestEqual(TEXT("policy allows vetted real"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("policyDecision")), TEXT("allow_vetted_real"));
	TestFalse(TEXT("forced dry run false"), UnrealMcpCallToolLibraryTests::GetNestedBool(Payload, TEXT("meta"), TEXT("forcedDryRun")));
	TestEqual(TEXT("one vetted audit event"), Events.Num(), 1);
	if (Events.Num() == 1)
	{
		TestEqual(TEXT("audit event kind"), Events[0].EventKind, TEXT("vetted_real_write"));
		TestTrue(TEXT("audit payload present"), Events[0].Payload.IsValid());
		if (Events[0].Payload.IsValid())
		{
			TestEqual(TEXT("audit tool name"), Events[0].Payload->GetStringField(TEXT("toolName")), TEXT("unreal.code_apply_change"));
			TestEqual(TEXT("audit vetted toolset id"), Events[0].Payload->GetStringField(TEXT("vettedToolsetId")), TEXT("user.test_vetted"));
			TestEqual(TEXT("audit vetted sha"), Events[0].Payload->GetStringField(TEXT("vettedMainPySha256")), VerifiedSha);
			TestEqual(TEXT("audit policy decision"), Events[0].Payload->GetStringField(TEXT("policyDecision")), TEXT("allow_vetted_real"));
			const TSharedPtr<FJsonObject>* SanitizedArgs = nullptr;
			TestTrue(TEXT("sanitized args present"), Events[0].Payload->TryGetObjectField(TEXT("sanitizedArguments"), SanitizedArgs) && SanitizedArgs && (*SanitizedArgs).IsValid());
			if (SanitizedArgs && (*SanitizedArgs).IsValid())
			{
				TestEqual(TEXT("sanitized preview id preserved"), (*SanitizedArgs)->GetStringField(TEXT("previewId")), TEXT("missing_vetted_preview"));
				TestFalse(TEXT("dryRun argument stayed false"), (*SanitizedArgs)->GetBoolField(TEXT("dryRun")));
				TestEqual(TEXT("secret token redacted"), (*SanitizedArgs)->GetStringField(TEXT("token")), TEXT("<redacted:secret>"));
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolVettedAuditFailClosedTest,
	"UnrealMcp.CallTool.VettedAuditFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolVettedAuditFailClosedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	int32 AuditAttempts = 0;
	TSharedPtr<FJsonObject> Payload;
	{
		UnrealMcp::FVettedToolsetScope Scope(TEXT("user.test_vetted"), TEXT("deadbeef"));
		TestTrue(
			TEXT("payload parses"),
			UnrealMcpCallToolLibraryTests::ParseCallToolPayload(
				UnrealMcpCallToolLibraryLocal::CallToolWithAuditWriter(
					TEXT("unreal.code_apply_change"),
					TEXT("{\"previewId\":\"missing_vetted_preview\",\"dryRun\":false}"),
					[&AuditAttempts](const UnrealMcp::FActivityLogEvent& Event, FString& FailureReason)
					{
						(void)Event;
						++AuditAttempts;
						FailureReason = TEXT("synthetic audit failure");
						return false;
					}),
				Payload));
	}

	if (!Payload.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("one audit write attempted"), AuditAttempts, 1);
	bool bIsError = false;
	Payload->TryGetBoolField(TEXT("isError"), bIsError);
	TestTrue(TEXT("audit failure returns error"), bIsError);
	TestTrue(TEXT("audit failure surfaced"), Payload->GetStringField(TEXT("text")).StartsWith(TEXT("vetted_audit_write_failed")));
	TestFalse(TEXT("underlying handler did not run"), Payload->GetStringField(TEXT("text")).Contains(TEXT("Preview artifact not found")));
	TestEqual(TEXT("fail closed policy deny"), UnrealMcpCallToolLibraryTests::GetNestedString(Payload, TEXT("meta"), TEXT("policyDecision")), TEXT("deny"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolVettedUserTargetStillDeniedTest,
	"UnrealMcp.CallTool.VettedUserTargetStillDenied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolVettedUserTargetStillDeniedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealMcpCallToolLibraryTests;
	DeleteDemoTool();
	ReloadUserTools(true);
	TestTrue(TEXT("demo user tool writes"), WriteDemoUserTool());
	ReloadUserTools(true);

	TSharedPtr<FJsonObject> Payload;
	{
		UnrealMcp::FVettedToolsetScope Scope(TEXT("user.test_vetted"), TEXT("deadbeef"));
		TestTrue(TEXT("payload parses"), ParseCallToolPayload(UUnrealMcpCallToolLibrary::CallTool(DemoToolName, TEXT("{}")), Payload));
	}

	DeleteDemoTool();
	ReloadUserTools(true);
	if (!Payload.IsValid())
	{
		return false;
	}

	bool bIsError = false;
	Payload->TryGetBoolField(TEXT("isError"), bIsError);
	TestTrue(TEXT("user target is denied"), bIsError);
	TestEqual(TEXT("user target reason"), GetNestedString(Payload, TEXT("meta"), TEXT("reason")), TEXT("user_tool_forbidden"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolPythonFullChainTest,
	"UnrealMcp.CallTool.PythonFullChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolPythonFullChainTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealMcpCallToolLibraryTests;
	DeleteDemoTool();
	ReloadUserTools(true);
	TestTrue(TEXT("demo user tool writes"), WriteDemoUserTool());
	ReloadUserTools(true);

	const UnrealMcp::FToolHandlerRegistryEntry* HandlerEntry = UnrealMcp::FindToolHandlerRegistryEntry(DemoToolName);
	TestNotNull(TEXT("demo handler registered"), HandlerEntry);
	if (!HandlerEntry)
	{
		DeleteDemoTool();
		ReloadUserTools(true);
		return false;
	}

	FJsonObject Arguments;
	const FUnrealMcpExecutionResult Result = UnrealMcp::UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(*HandlerEntry, Arguments);
	if (Result.bIsError
		&& (Result.Text.Contains(TEXT("PythonScriptPlugin"))
			|| Result.Text.Contains(TEXT("Python support is not available"))
			|| Result.Text.Contains(TEXT("Python is not initialized"))))
	{
		AddInfo(TEXT("Skipped: PythonScriptPlugin is unavailable in this automation environment."));
		DeleteDemoTool();
		ReloadUserTools(true);
		return true;
	}

	TestFalse(TEXT("python full chain succeeds"), Result.bIsError);
	TestTrue(TEXT("structured content present"), Result.StructuredContent.IsValid());
	if (Result.StructuredContent.IsValid())
	{
		FString Reentry;
		FString EditorStatusPolicy;
		FString ListActorsPolicy;
		const TSharedPtr<FJsonObject>* InnerContent = nullptr;
		if (Result.StructuredContent->TryGetObjectField(TEXT("structuredContent"), InnerContent) && InnerContent && (*InnerContent).IsValid())
		{
			(*InnerContent)->TryGetStringField(TEXT("reentry"), Reentry);
			(*InnerContent)->TryGetStringField(TEXT("editorStatusPolicy"), EditorStatusPolicy);
			(*InnerContent)->TryGetStringField(TEXT("listActorsPolicy"), ListActorsPolicy);
		}
		TestEqual(TEXT("reentry ok"), Reentry, TEXT("ok"));
		TestEqual(TEXT("editor status policy"), EditorStatusPolicy, TEXT("allow"));
		TestEqual(TEXT("list actors policy"), ListActorsPolicy, TEXT("allow"));
	}

	DeleteDemoTool();
	ReloadUserTools(true);
	return true;
}

#endif
