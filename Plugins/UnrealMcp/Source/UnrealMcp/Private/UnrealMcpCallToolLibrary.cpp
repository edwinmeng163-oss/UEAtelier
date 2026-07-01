#include "UnrealMcpCallToolLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/Function.h"
#include "UnrealMcpActivityLog.h"
#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpCaptureRedaction.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpVettedToolset.h"

namespace UnrealMcpCallToolLibraryLocal
{
	constexpr int32 MaxTextChars = 20000;
	static thread_local int32 CallToolDepthCounter = 0;

	struct FScopedCallToolDepth
	{
		FScopedCallToolDepth()
		{
			++CallToolDepthCounter;
		}

		~FScopedCallToolDepth()
		{
			--CallToolDepthCounter;
		}

		static int32 Current()
		{
			return CallToolDepthCounter;
		}
	};

	FString SerializePayload(const TSharedRef<FJsonObject>& Payload)
	{
		FString JsonString;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(Payload, Writer);
		return JsonString;
	}

	const TCHAR* DecisionToString(UnrealMcp::ECallToolDecision Decision)
	{
		switch (Decision)
		{
		case UnrealMcp::ECallToolDecision::Allow:
			return TEXT("allow");
		case UnrealMcp::ECallToolDecision::ForceDryRun:
			return TEXT("force_dry_run");
		case UnrealMcp::ECallToolDecision::AllowVettedReal:
			return TEXT("allow_vetted_real");
		case UnrealMcp::ECallToolDecision::Deny:
		default:
			return TEXT("deny");
		}
	}

	TSharedPtr<FJsonObject> MakeMetaPayload(
		UnrealMcp::ECallToolDecision Decision,
		bool bForcedDryRun,
		bool bTruncated,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("policyDecision"), DecisionToString(Decision));
		Meta->SetBoolField(TEXT("forcedDryRun"), bForcedDryRun);
		Meta->SetBoolField(TEXT("truncated"), bTruncated);
		Meta->SetStringField(TEXT("reason"), Reason);
		return Meta;
	}

	FString MakeErrorPayload(
		const FString& ToolName,
		const FString& Reason,
		UnrealMcp::ECallToolDecision Decision = UnrealMcp::ECallToolDecision::Deny,
		bool bForcedDryRun = false)
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);
		Payload->SetStringField(TEXT("text"), Reason);
		Payload->SetBoolField(TEXT("isError"), true);
		Payload->SetField(TEXT("structuredContent"), MakeShared<FJsonValueNull>());
		Payload->SetObjectField(TEXT("meta"), MakeMetaPayload(Decision, bForcedDryRun, false, Reason));
		return SerializePayload(Payload);
	}

	FString MakeResultPayload(
		const FString& ToolName,
		const FUnrealMcpExecutionResult& Result,
		UnrealMcp::ECallToolDecision Decision,
		bool bForcedDryRun)
	{
		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);

		bool bTruncated = false;
		FString Text = Result.Text;
		if (Text.Len() > MaxTextChars)
		{
			Text = Text.Left(MaxTextChars);
			bTruncated = true;
		}
		Payload->SetStringField(TEXT("text"), Text);
		Payload->SetBoolField(TEXT("isError"), Result.bIsError);
		if (Result.StructuredContent.IsValid())
		{
			Payload->SetObjectField(TEXT("structuredContent"), Result.StructuredContent);
		}
		else
		{
			Payload->SetField(TEXT("structuredContent"), MakeShared<FJsonValueNull>());
		}
		Payload->SetObjectField(TEXT("meta"), MakeMetaPayload(Decision, bForcedDryRun, bTruncated, FString()));
		return SerializePayload(Payload);
	}

	bool TryWriteVettedRealAuditEvent(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		const UnrealMcp::FCallToolPolicyResult& PolicyResult,
		TFunctionRef<bool(const UnrealMcp::FActivityLogEvent&, FString&)> Writer,
		FString& OutFailureReason)
	{
		const FString VettedToolsetId = UnrealMcp::FVettedToolsetScope::ActiveToolId();
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);
		Payload->SetStringField(TEXT("vettedToolsetId"), VettedToolsetId);
		Payload->SetStringField(TEXT("vettedMainPySha256"), UnrealMcp::FVettedToolsetScope::ActiveVerifiedSha());
		Payload->SetStringField(TEXT("policyDecision"), DecisionToString(PolicyResult.Decision));
		Payload->SetStringField(TEXT("reason"), PolicyResult.Reason);

		const UnrealMcp::CaptureRedaction::FRedactionResult Redaction =
			UnrealMcp::CaptureRedaction::SanitizeToolArguments_Pure(
				ToolName,
				Arguments,
				UnrealMcp::CaptureRedaction::kDefaultMaxValueChars,
				UnrealMcp::CaptureRedaction::kDefaultMaxTotalChars);
		if (Redaction.SanitizedArguments.IsValid())
		{
			Payload->SetObjectField(TEXT("sanitizedArguments"), Redaction.SanitizedArguments);
		}
		else
		{
			Payload->SetField(TEXT("sanitizedArguments"), MakeShared<FJsonValueNull>());
		}
		Payload->SetStringField(TEXT("captureStatus"), Redaction.CaptureStatus);
		Payload->SetObjectField(TEXT("redactionSummaryPublic"), Redaction.RedactionSummaryPublic.IsValid() ? Redaction.RedactionSummaryPublic : MakeShared<FJsonObject>());

		UnrealMcp::FActivityLogEvent Event;
		Event.EventKind = TEXT("vetted_real_write");
		Event.Summary = FString::Printf(TEXT("Vetted toolset %s authorized real call_tool write to %s."), *VettedToolsetId, *ToolName).Left(2000);
		Event.Payload = Payload;
		return Writer(Event, OutFailureReason);
	}

	UnrealMcp::FCallToolTargetFacts GatherFacts(const FString& ToolName)
	{
		UnrealMcp::FCallToolTargetFacts Facts;
		Facts.SourceKind = UnrealMcp::ResolveToolSourceKind(ToolName);
		if (const UnrealMcp::FToolRegistryEntry* Entry = UnrealMcp::FindToolRegistryEntry(ToolName))
		{
			Facts.bVisible = Entry->Exposure == UnrealMcp::EToolExposure::Visible;
		}
		else if (Facts.SourceKind == UnrealMcp::Extension::ESourceKind::UserRegistry)
		{
			Facts.bVisible = true;
		}

		const UnrealMcp::FToolPolicy Policy = UnrealMcp::GetToolPolicy(ToolName);
		Facts.RiskLevel = Policy.RiskLevel;
		Facts.bRequiresLock = Policy.bRequiresLock;
		Facts.bRequiresWrite = Policy.bRequiresWrite;
		Facts.bRequiresRestart = Policy.bRequiresRestart;
		Facts.bRequiresExternalProcess = Policy.bRequiresExternalProcess;
		Facts.bRequiresBuild = Policy.bRequiresBuild;
		Facts.bDryRunSupport = Policy.bDryRunSupport;
		Facts.bIsWorkflowRun = ToolName == TEXT("unreal.workflow_run");
		Facts.Depth = FScopedCallToolDepth::Current();
		Facts.bInVettedToolsetContext = UnrealMcp::FVettedToolsetScope::IsActive();
		return Facts;
	}

	FString CallToolWithAuditWriter(
		const FString& ToolName,
		const FString& ArgumentsJson,
		TFunctionRef<bool(const UnrealMcp::FActivityLogEvent&, FString&)> AuditWriter)
	{
		TSharedPtr<FJsonObject> ParsedArguments;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, ParsedArguments) || !ParsedArguments.IsValid())
		{
			return MakeErrorPayload(ToolName, TEXT("invalid_arguments_json"));
		}

		const UnrealMcp::FCallToolTargetFacts Facts = GatherFacts(ToolName);
		const UnrealMcp::FCallToolPolicyResult PolicyResult = UnrealMcp::ClassifyCallToolTarget_Pure(Facts);
		if (PolicyResult.Decision == UnrealMcp::ECallToolDecision::Deny)
		{
			return MakeErrorPayload(
				ToolName,
				PolicyResult.Reason,
				PolicyResult.Decision,
				PolicyResult.bForcedDryRun);
		}

		if (PolicyResult.Decision == UnrealMcp::ECallToolDecision::ForceDryRun)
		{
			ParsedArguments->SetBoolField(TEXT("dryRun"), true);
		}

		if (PolicyResult.Decision == UnrealMcp::ECallToolDecision::AllowVettedReal)
		{
			FString AuditFailureReason;
			if (!TryWriteVettedRealAuditEvent(
				ToolName,
				ParsedArguments,
				PolicyResult,
				AuditWriter,
				AuditFailureReason))
			{
				return MakeErrorPayload(
					ToolName,
					TEXT("vetted_audit_write_failed: ") + AuditFailureReason,
					UnrealMcp::ECallToolDecision::Deny);
			}
		}

		FScopedCallToolDepth Guard;
		FUnrealMcpModule* Module = FModuleManager::GetModulePtr<FUnrealMcpModule>(FName(TEXT("UnrealMcp")));
		if (!Module)
		{
			return MakeErrorPayload(
				ToolName,
				TEXT("module_unavailable"),
				PolicyResult.Decision,
				PolicyResult.bForcedDryRun);
		}

		const FUnrealMcpExecutionResult Result = Module->ExecuteToolFromEditorUI(ToolName, *ParsedArguments);
		return MakeResultPayload(
			ToolName,
			Result,
			PolicyResult.Decision,
			PolicyResult.bForcedDryRun);
	}
}

FString UUnrealMcpCallToolLibrary::CallTool(const FString& ToolName, const FString& ArgumentsJson)
{
	return UnrealMcpCallToolLibraryLocal::CallToolWithAuditWriter(
		ToolName,
		ArgumentsJson,
		[](const UnrealMcp::FActivityLogEvent& Event, FString& FailureReason)
		{
			return UnrealMcp::TryWriteActivityEvent(Event, FailureReason);
		});
}
