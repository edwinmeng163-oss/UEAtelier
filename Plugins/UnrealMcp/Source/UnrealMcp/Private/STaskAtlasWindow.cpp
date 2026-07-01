#include "STaskAtlasWindow.h"

#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TaskAtlasWindow"

namespace UnrealMcp::TaskAtlasComposite
{
	FString ComputePythonHandlerSha256(const FString& MainPy);

	namespace
	{
		struct FCompositeStep
		{
			FString ToolName;
			int32 Ordinal = 0;
			FString EventId;
			FString CaptureRef;
			FString CaptureStatus;
			FString PolicyClassAtCapture;
			FString CaptureReadStatus = TEXT("not_applicable");
			FString CaptureReadError;
			TSharedPtr<FJsonObject> CapturedArgs;
			bool bFromStepRef = false;
			bool bHasCapturedArgs = false;
		};

		bool TaskAtlasCompositeIsValidToolId(const FString& ToolId, FString& OutReason)
		{
			if (ToolId.IsEmpty())
			{
				OutReason = TEXT("Tool id is required.");
				return false;
			}
			if (ToolId.Len() > 64)
			{
				OutReason = TEXT("Tool id must be 64 characters or fewer.");
				return false;
			}
			if (ToolId.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("//"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("\\\\"), ESearchCase::CaseSensitive)
				|| FPaths::IsRelative(ToolId) == false)
			{
				OutReason = TEXT("Tool id must be a safe single directory name without slashes, traversal, drives, or UNC paths.");
				return false;
			}
			for (const TCHAR Character : ToolId)
			{
				const bool bLowerAlpha = Character >= TEXT('a') && Character <= TEXT('z');
				const bool bUpperAlpha = Character >= TEXT('A') && Character <= TEXT('Z');
				const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
				if (!bLowerAlpha && !bUpperAlpha && !bDigit && Character != TEXT('_'))
				{
					OutReason = TEXT("Tool id may contain only alphanumeric characters and underscores.");
					return false;
				}
			}
			return true;
		}

		FString TaskAtlasCompositePythonQuote(const FString& Value)
		{
			FString Result = TEXT("\"");
			for (const TCHAR Character : Value)
			{
				if (Character == TEXT('\\'))
				{
					Result += TEXT("\\\\");
				}
				else if (Character == TEXT('"'))
				{
					Result += TEXT("\\\"");
				}
				else if (Character == TEXT('\n'))
				{
					Result += TEXT("\\n");
				}
				else if (Character == TEXT('\r'))
				{
					Result += TEXT("\\r");
				}
				else if (Character == TEXT('\t'))
				{
					Result += TEXT("\\t");
				}
				else
				{
					Result.AppendChar(Character);
				}
			}
			Result += TEXT("\"");
			return Result;
		}

		FString TaskAtlasCompositeJsonToString(const TSharedPtr<FJsonObject>& Object)
		{
			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Output;
		}

		bool TaskAtlasCompositeIsReplayableCaptureStatus(const FString& CaptureStatus)
		{
			const FString Normalized = CaptureStatus.TrimStartAndEnd().ToLower();
			return Normalized == TEXT("captured") || Normalized == TEXT("redacted");
		}

		void TaskAtlasCompositeReadCapturedArgs(FCompositeStep& Step)
		{
			if (Step.CaptureRef.TrimStartAndEnd().IsEmpty()
				|| !TaskAtlasCompositeIsReplayableCaptureStatus(Step.CaptureStatus))
			{
				Step.CaptureReadStatus = TEXT("placeholder");
				return;
			}

			TSharedPtr<FJsonObject> CapturedContent;
			FString ReadError;
			if (!UnrealMcp::CapturedArgsStore::ReadCapturedArgs(Step.CaptureRef, CapturedContent, ReadError))
			{
				Step.CaptureReadStatus = TEXT("read_failed");
				Step.CaptureReadError = ReadError.TrimStartAndEnd();
				return;
			}

			const TSharedPtr<FJsonObject>* SanitizedArgs = nullptr;
			if (!CapturedContent.IsValid()
				|| !CapturedContent->TryGetObjectField(TEXT("sanitizedArguments"), SanitizedArgs)
				|| !SanitizedArgs
				|| !(*SanitizedArgs).IsValid())
			{
				Step.CaptureReadStatus = TEXT("read_failed");
				Step.CaptureReadError = TEXT("Captured args file is missing object field 'sanitizedArguments'.");
				return;
			}

			Step.CapturedArgs = *SanitizedArgs;
			Step.bHasCapturedArgs = true;
			Step.CaptureReadStatus = TEXT("captured");
		}

		FString TaskAtlasCompositeGetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
		}

		int32 TaskAtlasCompositeGetOrdinalField(const TSharedPtr<FJsonObject>& Object, int32 DefaultOrdinal)
		{
			double Ordinal = static_cast<double>(DefaultOrdinal);
			if (Object.IsValid())
			{
				Object->TryGetNumberField(TEXT("ordinal"), Ordinal);
			}
			return FMath::TruncToInt(Ordinal);
		}

			TSharedPtr<FJsonObject> TaskAtlasCompositeStepToJson(const FCompositeStep& Step)
			{
				TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
			StepObject->SetNumberField(TEXT("ordinal"), Step.Ordinal);
			StepObject->SetStringField(TEXT("tool"), Step.ToolName);
			if (!Step.EventId.IsEmpty())
			{
				StepObject->SetStringField(TEXT("eventId"), Step.EventId);
			}
			if (!Step.CaptureStatus.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureStatus"), Step.CaptureStatus);
			}
			if (!Step.CaptureRef.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureRef"), Step.CaptureRef);
			}
			if (!Step.PolicyClassAtCapture.IsEmpty())
			{
				StepObject->SetStringField(TEXT("policyClassAtCapture"), Step.PolicyClassAtCapture);
			}
			StepObject->SetBoolField(TEXT("hasCapturedArgs"), Step.bHasCapturedArgs);
			StepObject->SetStringField(TEXT("captureReadStatus"), Step.CaptureReadStatus);
			if (!Step.CaptureReadError.IsEmpty())
			{
				StepObject->SetStringField(TEXT("captureReadError"), Step.CaptureReadError);
			}
				return StepObject;
			}

	#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
			const TCHAR* OfficialToolsetVersion()
			{
				return TEXT("0.1");
		}

		bool TaskAtlasCompositeIsPythonKeyword(const FString& Identifier)
		{
			static const TSet<FString> Keywords = {
				TEXT("False"), TEXT("None"), TEXT("True"), TEXT("and"), TEXT("as"), TEXT("assert"),
				TEXT("async"), TEXT("await"), TEXT("break"), TEXT("class"), TEXT("continue"), TEXT("def"),
				TEXT("del"), TEXT("elif"), TEXT("else"), TEXT("except"), TEXT("finally"), TEXT("for"),
				TEXT("from"), TEXT("global"), TEXT("if"), TEXT("import"), TEXT("in"), TEXT("is"),
				TEXT("lambda"), TEXT("nonlocal"), TEXT("not"), TEXT("or"), TEXT("pass"), TEXT("raise"),
				TEXT("return"), TEXT("try"), TEXT("while"), TEXT("with"), TEXT("yield")
			};
			return Keywords.Contains(Identifier);
		}

		FString TaskAtlasCompositeSanitizePythonIdentifier(const FString& Value, const FString& Fallback, bool bLowercase)
		{
			FString Result;
			for (const TCHAR Character : Value.TrimStartAndEnd())
			{
				const bool bAlpha = FChar::IsAlpha(Character);
				const bool bDigit = FChar::IsDigit(Character);
				if (bAlpha || bDigit)
				{
					Result.AppendChar(bLowercase ? FChar::ToLower(Character) : Character);
				}
				else if (!Result.EndsWith(TEXT("_")))
				{
					Result.AppendChar(TEXT('_'));
				}
			}
			while (Result.StartsWith(TEXT("_")))
			{
				Result.RightChopInline(1);
			}
			while (Result.EndsWith(TEXT("_")))
			{
				Result.LeftChopInline(1);
			}
			if (Result.IsEmpty())
			{
				Result = Fallback;
			}
			if (Result.IsEmpty() || FChar::IsDigit(Result[0]) || TaskAtlasCompositeIsPythonKeyword(Result))
			{
				Result = TEXT("arg_") + Result;
			}
			return Result;
		}

		FString TaskAtlasCompositeMakeClassName(const FString& ToolId)
		{
			TArray<FString> Parts;
			ToolId.ParseIntoArray(Parts, TEXT("_"), true);
			FString ClassName = TEXT("UEAtelier");
			for (FString Part : Parts)
			{
				Part = TaskAtlasCompositeSanitizePythonIdentifier(Part, FString(), true);
				if (Part.IsEmpty())
				{
					continue;
				}
				Part[0] = FChar::ToUpper(Part[0]);
				ClassName += Part;
			}
			ClassName += TEXT("Toolset");
			return ClassName;
		}

		FString TaskAtlasCompositeMakeOfficialToolsetName(const FString& ToolId, const FString& ModuleName, const FString& ClassName)
		{
			return FString::Printf(
				TEXT("Temp.UnrealMcp.OfficialToolsetDrafts.%s.%s.%s"),
				*ToolId,
				*ModuleName,
				*ClassName);
		}

			FString TaskAtlasCompositeMakeUniqueIdentifier(
				const FString& BaseName,
				const FString& Fallback,
				TSet<FString>& UsedNames)
		{
			const FString Sanitized = TaskAtlasCompositeSanitizePythonIdentifier(BaseName, Fallback, true);
			FString Candidate = Sanitized;
			int32 Suffix = 2;
			while (UsedNames.Contains(Candidate))
			{
				Candidate = FString::Printf(TEXT("%s_%d"), *Sanitized, Suffix++);
			}
			UsedNames.Add(Candidate);
			return Candidate;
		}

		FString TaskAtlasCompositeJsonValueToPythonLiteral(const TSharedPtr<FJsonValue>& Value);

		FString TaskAtlasCompositeJsonObjectToPythonLiteral(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return TEXT("{}");
			}

			TArray<FString> Keys;
			UnrealMcp::Compat::GetJsonObjectKeys(*Object, Keys);
			Keys.Sort();

			TArray<FString> Entries;
			for (const FString& Key : Keys)
			{
				const TSharedPtr<FJsonValue>* FoundValue = UnrealMcp::Compat::FindJsonValue(*Object, Key);
				if (!FoundValue || !(*FoundValue).IsValid())
				{
					continue;
				}
				Entries.Add(FString::Printf(
					TEXT("%s: %s"),
					*TaskAtlasCompositePythonQuote(Key),
					*TaskAtlasCompositeJsonValueToPythonLiteral(*FoundValue)));
			}
			return TEXT("{") + FString::Join(Entries, TEXT(", ")) + TEXT("}");
		}

		FString TaskAtlasCompositeJsonValueToPythonLiteral(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return TEXT("None");
			}

			switch (Value->Type)
			{
			case EJson::String:
				return TaskAtlasCompositePythonQuote(Value->AsString());
			case EJson::Number:
			{
				const double Number = Value->AsNumber();
				const int64 IntegerValue = FMath::RoundToInt64(Number);
				if (FMath::IsNearlyEqual(Number, static_cast<double>(IntegerValue)))
				{
					return FString::Printf(TEXT("%lld"), IntegerValue);
				}
				return FString::SanitizeFloat(Number);
			}
			case EJson::Boolean:
				return Value->AsBool() ? FString(TEXT("True")) : FString(TEXT("False"));
			case EJson::Array:
			{
				TArray<FString> Items;
				for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
				{
					Items.Add(TaskAtlasCompositeJsonValueToPythonLiteral(Item));
				}
				return TEXT("[") + FString::Join(Items, TEXT(", ")) + TEXT("]");
			}
			case EJson::Object:
				return TaskAtlasCompositeJsonObjectToPythonLiteral(Value->AsObject());
			case EJson::Null:
			default:
				return TEXT("None");
			}
		}

		FString TaskAtlasCompositeTypeForParamValue(const TSharedPtr<FJsonValue>& Value)
		{
			if (!Value.IsValid())
			{
				return FString();
			}
			if (Value->Type == EJson::String)
			{
				return TEXT("str");
			}
			if (Value->Type == EJson::Boolean)
			{
				return TEXT("bool");
			}
			if (Value->Type == EJson::Number)
			{
				const double Number = Value->AsNumber();
				return FMath::IsNearlyEqual(Number, static_cast<double>(FMath::RoundToInt64(Number)))
					? FString(TEXT("int"))
					: FString(TEXT("float"));
			}
			return FString();
		}

		TSharedPtr<FJsonObject> TaskAtlasCompositeCloneObject(const TSharedPtr<FJsonObject>& Source)
		{
			TSharedPtr<FJsonObject> Clone = MakeShared<FJsonObject>();
			if (Source.IsValid())
			{
				Clone->Values = Source->Values;
			}
			return Clone;
		}

		TSharedPtr<FJsonObject> TaskAtlasCompositeParamTypesToJson(const TMap<FString, FString>& ParamSchemaTypes)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			TArray<FString> Names;
			ParamSchemaTypes.GetKeys(Names);
			Names.Sort();
			for (const FString& Name : Names)
			{
				if (const FString* Type = ParamSchemaTypes.Find(Name))
				{
					Object->SetStringField(Name, *Type);
				}
			}
			return Object;
		}

		FString TaskAtlasCompositeMakeSchemaHash(const TArray<FOfficialToolsetToolInfo>& Tools)
		{
			TArray<FString> Lines;
			for (const FOfficialToolsetToolInfo& Tool : Tools)
			{
				TArray<FString> ParamNames;
				Tool.ParamSchemaTypes.GetKeys(ParamNames);
				ParamNames.Sort();

				TArray<FString> Params;
				for (const FString& ParamName : ParamNames)
				{
					const FString* ParamType = Tool.ParamSchemaTypes.Find(ParamName);
					Params.Add(FString::Printf(TEXT("%s:%s"), *ParamName, ParamType ? **ParamType : TEXT("")));
				}
				Lines.Add(FString::Printf(
					TEXT("%s|%s|%s|%s"),
					*Tool.Name,
					*Tool.GovernedToolId,
					*Tool.ReturnType,
					*FString::Join(Params, TEXT(","))));
			}
			return ComputePythonHandlerSha256(FString::Join(Lines, TEXT("\n")));
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasCompositeMakeTaskIdArray(const FString& TaskId)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			if (!TaskId.TrimStartAndEnd().IsEmpty())
			{
				Values.Add(MakeShared<FJsonValueString>(TaskId.TrimStartAndEnd()));
			}
			return Values;
		}

		FString TaskAtlasCompositeBuildOfficialManifest(
			const FString& ToolsetName,
			const FString& ClassName,
			const FString& ModuleName,
			const FString& TaskId,
			const FString& SchemaHash,
			const TArray<FOfficialToolsetToolInfo>& Tools)
		{
			TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
			Manifest->SetStringField(TEXT("toolsetName"), ToolsetName);
			Manifest->SetStringField(TEXT("className"), ClassName);
			Manifest->SetStringField(TEXT("modulePath"), ModuleName + TEXT(".py"));
			Manifest->SetStringField(TEXT("sourcePath"), FString());

			TArray<TSharedPtr<FJsonValue>> ToolValues;
			for (const FOfficialToolsetToolInfo& Tool : Tools)
			{
				TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
				ToolObject->SetStringField(TEXT("name"), Tool.Name);
				ToolObject->SetObjectField(TEXT("paramSchemaTypes"), TaskAtlasCompositeParamTypesToJson(Tool.ParamSchemaTypes));
				ToolObject->SetStringField(TEXT("returnType"), Tool.ReturnType);
				ToolObject->SetStringField(TEXT("governedToolId"), Tool.GovernedToolId);
				ToolObject->SetObjectField(TEXT("argsTemplate"), TaskAtlasCompositeCloneObject(Tool.ArgsTemplate));
				ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
			}
			Manifest->SetArrayField(TEXT("toolNames"), ToolValues);
			Manifest->SetStringField(TEXT("schemaHash"), SchemaHash);
			Manifest->SetArrayField(TEXT("sourceTaskAtlasTaskIds"), TaskAtlasCompositeMakeTaskIdArray(TaskId));

			TSharedPtr<FJsonObject> ValidatorStatus = MakeShared<FJsonObject>();
			ValidatorStatus->SetBoolField(TEXT("passed"), false);
			ValidatorStatus->SetArrayField(TEXT("issues"), TArray<TSharedPtr<FJsonValue>>());
			ValidatorStatus->SetStringField(TEXT("validatedAt"), FString());
			Manifest->SetObjectField(TEXT("validatorStatus"), ValidatorStatus);

			TSharedPtr<FJsonObject> RegistrationStatus = MakeShared<FJsonObject>();
			RegistrationStatus->SetStringField(TEXT("state"), TEXT("unregistered"));
			RegistrationStatus->SetStringField(TEXT("registeredAt"), FString());
			RegistrationStatus->SetStringField(TEXT("lastError"), FString());
			Manifest->SetObjectField(TEXT("registrationStatus"), RegistrationStatus);

			TSharedPtr<FJsonObject> SmokeResult = MakeShared<FJsonObject>();
			SmokeResult->SetStringField(TEXT("serverEndpoint"), FString());
			SmokeResult->SetBoolField(TEXT("listedInToolsList"), false);
			SmokeResult->SetObjectField(TEXT("sampleCall"), MakeShared<FJsonObject>());
			SmokeResult->SetStringField(TEXT("smokedAt"), FString());
			Manifest->SetObjectField(TEXT("smokeResult"), SmokeResult);

			TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
			Rollback->SetStringField(TEXT("unregisterEntryPoint"), ModuleName + TEXT(".unregister"));
			Rollback->SetStringField(TEXT("deletePath"), FString());
			Rollback->SetStringField(TEXT("reloadPackage"), ModuleName);
			Manifest->SetObjectField(TEXT("rollback"), Rollback);
			Manifest->SetStringField(TEXT("version"), OfficialToolsetVersion());
			return TaskAtlasCompositeJsonToString(Manifest);
		}
#endif
		}

	FString NormalizeReplayEligibility(const FString& ReplayEligibility)
	{
		const FString Trimmed = ReplayEligibility.TrimStartAndEnd();
		if (Trimmed == TEXT("preview_ready")
			|| Trimmed == TEXT("partial")
			|| Trimmed == TEXT("blocked")
			|| Trimmed == TEXT("skeleton_pre_capture"))
		{
			return Trimmed;
		}
		return TEXT("skeleton_pre_capture");
	}

	FString ReplayStatusForReplayEligibility(const FString& ReplayEligibility)
	{
		const FString Normalized = NormalizeReplayEligibility(ReplayEligibility);
		return Normalized == TEXT("preview_ready") ? FString(TEXT("preview_only")) : Normalized;
	}

	FString ReplayStatusSummary(const FString& ReplayEligibility, const FString& ReplayUnavailableReason)
	{
		const FString Status = ReplayStatusForReplayEligibility(ReplayEligibility);
		const FString Reason = ReplayUnavailableReason.TrimStartAndEnd();
		if (Status == TEXT("preview_only"))
		{
			return TEXT("preview_only: generated a captured-argument preview composite; write-capable steps remain dry-run only");
		}
		if (Status == TEXT("partial"))
		{
			return Reason.IsEmpty()
				? FString(TEXT("partial: generated a preview composite with placeholders because at least one step lacks captured arguments"))
				: FString::Printf(TEXT("partial: generated a preview composite with placeholders because %s"), *Reason);
		}
		if (Status == TEXT("blocked"))
		{
			return Reason.IsEmpty()
				? FString(TEXT("blocked: generated a preview composite, but at least one step is denied by call_tool policy"))
				: FString::Printf(TEXT("blocked: generated a preview composite, but %s"), *Reason);
		}
		return Reason.IsEmpty()
			? FString(TEXT("skeleton_pre_capture: generated skeleton because no captured arguments are available"))
			: FString::Printf(TEXT("skeleton_pre_capture: generated skeleton because %s"), *Reason);
	}

	FString ComputePythonHandlerSha256(const FString& MainPy)
	{
		return UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(MainPy);
	}

	FString JsonObjectToCondensedString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	bool BuildCompositeUserToolFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FString& OutToolName,
		FString& OutMainPy,
		FString& OutMainPySha256,
		FString& OutToolJson,
		TSharedPtr<FJsonObject>& OutSmokeArgs,
		TArray<FString>& OutStepTools,
		FString& OutFailureReason)
	{
		OutToolName.Reset();
		OutMainPy.Reset();
		OutMainPySha256.Reset();
		OutToolJson.Reset();
		OutSmokeArgs = MakeShared<FJsonObject>();
		OutStepTools.Reset();
		OutFailureReason.Reset();

		if (!TaskAtlasCompositeIsValidToolId(ToolId, OutFailureReason))
		{
			return false;
		}

		TArray<FCompositeStep> Steps;
		Steps.Reserve(StepRefs.Num() > 0 ? StepRefs.Num() : CriticalPath.Num());

		for (int32 Index = 0; Index < StepRefs.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& StepValue = StepRefs[Index];
			const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
			if (!StepObject.IsValid())
			{
				continue;
			}

			FCompositeStep Step;
			Step.bFromStepRef = true;
			Step.Ordinal = TaskAtlasCompositeGetOrdinalField(StepObject, Index);
			Step.ToolName = TaskAtlasCompositeGetStringField(StepObject, TEXT("tool"));
			Step.EventId = TaskAtlasCompositeGetStringField(StepObject, TEXT("eventId"));
			Step.CaptureStatus = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureStatus"));
			Step.CaptureRef = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureRef"));
			Step.PolicyClassAtCapture = TaskAtlasCompositeGetStringField(StepObject, TEXT("policyClassAtCapture"));
			if (!Step.ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
				|| !VisibleCoreToolNames.Contains(Step.ToolName))
			{
				continue;
			}

			TaskAtlasCompositeReadCapturedArgs(Step);
			Steps.Add(Step);
		}

		TSet<FString> SeenTools;
		if (Steps.Num() == 0)
		{
			for (const FString& RawToolName : CriticalPath)
			{
				const FString ToolName = RawToolName.TrimStartAndEnd();
				if (!ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
					|| !VisibleCoreToolNames.Contains(ToolName)
					|| SeenTools.Contains(ToolName))
				{
					continue;
				}

				SeenTools.Add(ToolName);
				FCompositeStep Step;
				Step.ToolName = ToolName;
				Step.Ordinal = Steps.Num();
				Step.CaptureReadStatus = TEXT("placeholder");
				Steps.Add(Step);
			}
		}

		bool bHasAnyCapturedArgs = false;
		for (const FCompositeStep& Step : Steps)
		{
			OutStepTools.Add(Step.ToolName);
			bHasAnyCapturedArgs = bHasAnyCapturedArgs || Step.bHasCapturedArgs;
		}

		if (OutStepTools.Num() == 0)
		{
			OutFailureReason = TEXT("Make Tool Set requires at least one visible core unreal.* tool in the selected workflow critical path.");
			return false;
		}

		OutToolName = FString::Printf(TEXT("user.%s"), *ToolId);
		const FString NormalizedReplayEligibility = NormalizeReplayEligibility(ReplayEligibility);
		const FString CompositeKind = bHasAnyCapturedArgs ? FString(TEXT("preview")) : FString(TEXT("skeleton"));
		const FString ReplayStatus = ReplayStatusForReplayEligibility(NormalizedReplayEligibility);
		const FString ReplaySummary = ReplayStatusSummary(NormalizedReplayEligibility, ReplayUnavailableReason);

		TSharedPtr<FJsonObject> CapturedDefaults = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> CapturedMeta = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> StepRefValues;
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepKey = FString::Printf(TEXT("step%d"), Index);
			const FCompositeStep& Step = Steps[Index];
			if (Step.bHasCapturedArgs)
			{
				CapturedDefaults->SetObjectField(StepKey, Step.CapturedArgs);
			}
			CapturedMeta->SetObjectField(StepKey, TaskAtlasCompositeStepToJson(Step));
			StepRefValues.Add(MakeShared<FJsonValueObject>(TaskAtlasCompositeStepToJson(Step)));
		}

		const FString CapturedDefaultsJson = JsonObjectToCondensedString(CapturedDefaults);
		const FString CapturedMetaJson = JsonObjectToCondensedString(CapturedMeta);

		OutMainPy += TEXT("# Generated by Task Atlas as a preview composite user tool.\n");
		OutMainPy += FString::Printf(TEXT("# Replay status: %s.\n"), *ReplaySummary);
		OutMainPy += TEXT("# Preview only: read-only/dry-run steps execute, and write-capable steps are forced to dryRun by call_tool policy.\n");
		OutMainPy += TEXT("# This is not real replay; captured arguments are embedded only as JSON data defaults.\n\n");
		OutMainPy += TEXT("import json\n\n");
		OutMainPy += FString::Printf(TEXT("_CAPTURED_JSON = %s\n"), *TaskAtlasCompositePythonQuote(CapturedDefaultsJson));
		OutMainPy += FString::Printf(TEXT("_CAPTURED_META_JSON = %s\n"), *TaskAtlasCompositePythonQuote(CapturedMetaJson));
		OutMainPy += TEXT("_CAPTURED = json.loads(_CAPTURED_JSON)\n");
		OutMainPy += TEXT("_CAPTURED_META = json.loads(_CAPTURED_META_JSON)\n");
		OutMainPy += TEXT("_MISSING = object()\n\n");
		OutMainPy += TEXT("def _json_copy(value):\n");
		OutMainPy += TEXT("    return json.loads(json.dumps(value or {}))\n\n");
		OutMainPy += TEXT("def _step_args(args, override_key, captured_key):\n");
		OutMainPy += TEXT("    if override_key in args and args.get(override_key) is not None:\n");
		OutMainPy += TEXT("        value = args.get(override_key)\n");
		OutMainPy += TEXT("    else:\n");
		OutMainPy += TEXT("        value = _CAPTURED.get(captured_key, {})\n");
		OutMainPy += TEXT("    if not isinstance(value, dict):\n");
		OutMainPy += TEXT("        value = {}\n");
		OutMainPy += TEXT("    return _json_copy(value)\n\n");
		OutMainPy += TEXT("def _effective_args(requested, result):\n");
		OutMainPy += TEXT("    effective = _json_copy(requested)\n");
		OutMainPy += TEXT("    meta = result.get(\"meta\", {}) if isinstance(result, dict) else {}\n");
		OutMainPy += TEXT("    if meta.get(\"forcedDryRun\") and effective.get(\"dryRun\") is not True:\n");
		OutMainPy += TEXT("        effective[\"dryRun\"] = True\n");
		OutMainPy += TEXT("    return effective\n\n");
		OutMainPy += TEXT("def _args_diff(requested, effective):\n");
		OutMainPy += TEXT("    requested = requested or {}\n");
		OutMainPy += TEXT("    effective = effective or {}\n");
		OutMainPy += TEXT("    diff = {}\n");
		OutMainPy += TEXT("    for key in sorted(set(requested.keys()) | set(effective.keys())):\n");
		OutMainPy += TEXT("        requested_value = requested[key] if key in requested else _MISSING\n");
		OutMainPy += TEXT("        effective_value = effective[key] if key in effective else _MISSING\n");
		OutMainPy += TEXT("        if requested_value == effective_value:\n");
		OutMainPy += TEXT("            continue\n");
		OutMainPy += TEXT("        entry = {}\n");
		OutMainPy += TEXT("        if requested_value is _MISSING:\n");
		OutMainPy += TEXT("            entry[\"requestedMissing\"] = True\n");
		OutMainPy += TEXT("        else:\n");
		OutMainPy += TEXT("            entry[\"requested\"] = requested_value\n");
		OutMainPy += TEXT("        if effective_value is _MISSING:\n");
		OutMainPy += TEXT("            entry[\"effectiveMissing\"] = True\n");
		OutMainPy += TEXT("        else:\n");
		OutMainPy += TEXT("            entry[\"effective\"] = effective_value\n");
		OutMainPy += TEXT("        diff[key] = entry\n");
		OutMainPy += TEXT("    return diff\n\n");
		OutMainPy += TEXT("def _policy_decision(result):\n");
		OutMainPy += TEXT("    meta = result.get(\"meta\", {}) if isinstance(result, dict) else {}\n");
		OutMainPy += TEXT("    return meta.get(\"policyDecision\", \"\")\n\n");
		OutMainPy += TEXT("def _blocked_steps(steps):\n");
		OutMainPy += TEXT("    return [step for step in steps if step.get(\"policyDecision\") == \"deny\"]\n\n");
		OutMainPy += TEXT("def _record_step(tool, captured_key, result, requested, effective):\n");
		OutMainPy += TEXT("    result = result or {}\n");
		OutMainPy += TEXT("    meta = result.get(\"meta\", {}) if isinstance(result, dict) else {}\n");
		OutMainPy += TEXT("    step = {\n");
		OutMainPy += TEXT("        \"tool\": tool,\n");
		OutMainPy += TEXT("        \"policyDecision\": _policy_decision(result) or \"unknown\",\n");
		OutMainPy += TEXT("        \"isError\": bool(result.get(\"isError\", True)),\n");
		OutMainPy += TEXT("    }\n");
		OutMainPy += TEXT("    if meta.get(\"forcedDryRun\"):\n");
		OutMainPy += TEXT("        step[\"forcedDryRun\"] = True\n");
		OutMainPy += TEXT("    capture = _CAPTURED_META.get(captured_key)\n");
		OutMainPy += TEXT("    if capture:\n");
		OutMainPy += TEXT("        step[\"capture\"] = capture\n");
		OutMainPy += TEXT("    diff = _args_diff(requested, effective)\n");
		OutMainPy += TEXT("    if diff:\n");
		OutMainPy += TEXT("        step[\"effectiveArgsDiff\"] = diff\n");
		OutMainPy += TEXT("    return step\n\n");
		OutMainPy += TEXT("def execute(args):\n");
		OutMainPy += TEXT("    args = args or {}\n");
		OutMainPy += TEXT("    steps = []\n");
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepName = FString::Printf(TEXT("step%d_args"), Index);
			const FString CapturedKey = FString::Printf(TEXT("step%d"), Index);
			const FString StepIndex = FString::FromInt(Index);
			const FString QuotedTool = TaskAtlasCompositePythonQuote(Steps[Index].ToolName);
			const FString QuotedStepName = TaskAtlasCompositePythonQuote(StepName);
			const FString QuotedCapturedKey = TaskAtlasCompositePythonQuote(CapturedKey);
			OutMainPy += FString::Printf(TEXT("    step%d_requested = _step_args(args, %s, %s)\n"), Index, *QuotedStepName, *QuotedCapturedKey);
			OutMainPy += FString::Printf(TEXT("    r%d = call_tool_raw(%s, step%d_requested)\n"), Index, *QuotedTool, Index);
			OutMainPy += FString::Printf(TEXT("    step%d_effective = _effective_args(step%d_requested, r%d)\n"), Index, Index, Index);
			OutMainPy += FString::Printf(TEXT("    steps.append(_record_step(%s, %s, r%d, step%d_requested, step%d_effective))\n"), *QuotedTool, *QuotedCapturedKey, Index, Index, Index);
			OutMainPy += FString::Printf(TEXT("    if r%d.get(\"isError\", False) and _policy_decision(r%d) != \"deny\":\n"), Index, Index);
			OutMainPy += TEXT("        _blocked = _blocked_steps(steps)\n");
			OutMainPy += FString::Printf(TEXT("        return {\"isError\": True, \"text\": \"composite stopped at step %s (execution error)\", \"structuredContent\": {\"steps\": steps, \"hasBlockedSteps\": bool(_blocked), \"blockedCount\": len(_blocked), \"blockedSteps\": _blocked}}\n"), *StepIndex);
		}
		OutMainPy += TEXT("    _blocked = _blocked_steps(steps)\n");
		OutMainPy += TEXT("    if _blocked:\n");
		OutMainPy += FString::Printf(TEXT("        _text = \"preview composite: %d steps, \" + str(len(_blocked)) + \" blocked by policy (expected for write tools)\"\n"), Steps.Num());
		OutMainPy += TEXT("    else:\n");
		OutMainPy += FString::Printf(TEXT("        _text = \"preview composite executed %d steps\"\n"), Steps.Num());
		OutMainPy += TEXT("    return {\"isError\": False, \"text\": _text, \"structuredContent\": {\"steps\": steps, \"hasBlockedSteps\": bool(_blocked), \"blockedCount\": len(_blocked), \"blockedSteps\": _blocked}}\n");

		OutMainPySha256 = ComputePythonHandlerSha256(OutMainPy);

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FString StepName = FString::Printf(TEXT("step%d_args"), Index);
			TSharedPtr<FJsonObject> StepSchema = MakeShared<FJsonObject>();
			StepSchema->SetStringField(TEXT("type"), TEXT("object"));
			StepSchema->SetStringField(
				TEXT("description"),
				Steps[Index].bHasCapturedArgs
					? FString::Printf(TEXT("Optional override for %s. If omitted, the sanitized captured arguments for step %d are used."), *Steps[Index].ToolName, Index)
					: FString::Printf(TEXT("Arguments forwarded to %s. Placeholder step; fill reviewed args before preview."), *Steps[Index].ToolName));
			StepSchema->SetBoolField(TEXT("additionalProperties"), true);
			Properties->SetObjectField(StepName, StepSchema);
			if (!Steps[Index].bHasCapturedArgs)
			{
				OutSmokeArgs->SetObjectField(StepName, MakeShared<FJsonObject>());
			}
		}

		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetObjectField(TEXT("properties"), Properties);
		InputSchema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>());
		InputSchema->SetBoolField(TEXT("additionalProperties"), false);

		TArray<TSharedPtr<FJsonValue>> CriticalPathValues;
		for (const FString& ToolName : OutStepTools)
		{
			CriticalPathValues.Add(MakeShared<FJsonValueString>(ToolName));
		}

		TSharedPtr<FJsonObject> ToolJson = MakeShared<FJsonObject>();
		ToolJson->SetStringField(TEXT("name"), OutToolName);
		ToolJson->SetStringField(TEXT("title"), Title.TrimStartAndEnd().IsEmpty() ? ToolId : Title.TrimStartAndEnd());
		ToolJson->SetStringField(TEXT("description"), Description);
		ToolJson->SetStringField(TEXT("pythonHandlerSha256"), OutMainPySha256);
		ToolJson->SetArrayField(TEXT("importAllowlist"), TArray<TSharedPtr<FJsonValue>>());
		ToolJson->SetObjectField(TEXT("inputSchema"), InputSchema);
		ToolJson->SetObjectField(TEXT("smokeArgs"), OutSmokeArgs);
		ToolJson->SetStringField(TEXT("generator"), TEXT("task-atlas-composite"));
		ToolJson->SetStringField(TEXT("compositeKind"), CompositeKind);
		ToolJson->SetStringField(TEXT("replayStatus"), ReplayStatus);
		ToolJson->SetStringField(TEXT("replayEligibility"), NormalizedReplayEligibility);
		ToolJson->SetStringField(TEXT("replayUnavailableReason"), ReplayUnavailableReason.TrimStartAndEnd());
		ToolJson->SetStringField(TEXT("taskId"), TaskId.TrimStartAndEnd());
		ToolJson->SetArrayField(TEXT("criticalPath"), CriticalPathValues);
		ToolJson->SetArrayField(TEXT("stepRefs"), StepRefValues);

		OutToolJson = TaskAtlasCompositeJsonToString(ToolJson);
		return true;
	}

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	bool BuildOfficialToolsetFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FOfficialToolsetBuildProduct& OutProduct,
		FString& OutFailureReason)
	{
		(void)ReplayEligibility;
		(void)ReplayUnavailableReason;
		OutProduct = FOfficialToolsetBuildProduct();
		OutFailureReason.Reset();

		if (!TaskAtlasCompositeIsValidToolId(ToolId, OutFailureReason))
		{
			return false;
		}

		TArray<FCompositeStep> Steps;
		Steps.Reserve(StepRefs.Num() > 0 ? StepRefs.Num() : CriticalPath.Num());
		for (int32 Index = 0; Index < StepRefs.Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& StepValue = StepRefs[Index];
			const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
			if (!StepObject.IsValid())
			{
				continue;
			}

			FCompositeStep Step;
			Step.bFromStepRef = true;
			Step.Ordinal = TaskAtlasCompositeGetOrdinalField(StepObject, Index);
			Step.ToolName = TaskAtlasCompositeGetStringField(StepObject, TEXT("tool"));
			Step.EventId = TaskAtlasCompositeGetStringField(StepObject, TEXT("eventId"));
			Step.CaptureStatus = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureStatus"));
			Step.CaptureRef = TaskAtlasCompositeGetStringField(StepObject, TEXT("captureRef"));
			Step.PolicyClassAtCapture = TaskAtlasCompositeGetStringField(StepObject, TEXT("policyClassAtCapture"));
			if (!Step.ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
				|| !VisibleCoreToolNames.Contains(Step.ToolName))
			{
				continue;
			}

			TaskAtlasCompositeReadCapturedArgs(Step);
			Steps.Add(Step);
		}

		TSet<FString> SeenFallbackTools;
		if (Steps.Num() == 0)
		{
			for (const FString& RawToolName : CriticalPath)
			{
				const FString ToolName = RawToolName.TrimStartAndEnd();
				if (!ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive)
					|| !VisibleCoreToolNames.Contains(ToolName)
					|| SeenFallbackTools.Contains(ToolName))
				{
					continue;
				}

				SeenFallbackTools.Add(ToolName);
				FCompositeStep Step;
				Step.ToolName = ToolName;
				Step.Ordinal = Steps.Num();
				Step.CaptureReadStatus = TEXT("placeholder");
				Steps.Add(Step);
			}
		}

		if (Steps.Num() == 0)
		{
			OutFailureReason = TEXT("Official Toolset generation requires at least one visible core unreal.* tool in the workflow critical path.");
			return false;
		}

		OutProduct.ModuleName = TaskAtlasCompositeSanitizePythonIdentifier(ToolId + TEXT("_official_toolset"), TEXT("ueatelier_official_toolset"), true);
		OutProduct.ClassName = TaskAtlasCompositeMakeClassName(ToolId);
		OutProduct.ToolsetName = TaskAtlasCompositeMakeOfficialToolsetName(ToolId, OutProduct.ModuleName, OutProduct.ClassName);

		const FString ToolsetDescription = Description.TrimStartAndEnd().IsEmpty()
			? FString::Printf(TEXT("Governed UEAtelier official toolset generated from Task Atlas task %s."), *TaskId.TrimStartAndEnd())
			: Description.TrimStartAndEnd();

		OutProduct.MainPy += TEXT("\"\"\"Generated UEAtelier official ToolsetRegistry wrapper.\"\"\"\n\n");
		OutProduct.MainPy += TEXT("from __future__ import annotations\n\n");
		OutProduct.MainPy += TEXT("import json\n\n");
		OutProduct.MainPy += TEXT("from toolset_registry.registration import Registration\n");
		OutProduct.MainPy += TEXT("import toolset_registry\n");
		OutProduct.MainPy += TEXT("import unreal\n\n\n");
		OutProduct.MainPy += TEXT("@unreal.uclass()\n");
		OutProduct.MainPy += FString::Printf(TEXT("class %s(unreal.ToolsetDefinition):\n"), *OutProduct.ClassName);
		OutProduct.MainPy += FString::Printf(TEXT("    \"\"\"%s\"\"\"\n"), *ToolsetDescription.ReplaceCharWithEscapedChar());

		TSet<FString> UsedMethodNames;
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FCompositeStep& Step = Steps[Index];
			const FString MethodBase = FString::Printf(TEXT("step%d_%s"), Index, *Step.ToolName.RightChop(7));
			const FString MethodName = TaskAtlasCompositeMakeUniqueIdentifier(MethodBase, FString::Printf(TEXT("step%d_tool"), Index), UsedMethodNames);
			TSharedPtr<FJsonObject> ArgsTemplate = TaskAtlasCompositeCloneObject(Step.CapturedArgs);

			TMap<FString, FString> ParamSchemaTypes;
			TMap<FString, FString> RawKeyToParamName;
			TSet<FString> UsedParamNames;
			TArray<FString> RawArgKeys;
			UnrealMcp::Compat::GetJsonObjectKeys(*ArgsTemplate, RawArgKeys);
			RawArgKeys.Sort();
			for (const FString& RawKey : RawArgKeys)
			{
				const TSharedPtr<FJsonValue>* Value = UnrealMcp::Compat::FindJsonValue(*ArgsTemplate, RawKey);
				const FString ParamType = TaskAtlasCompositeTypeForParamValue(Value ? *Value : nullptr);
				if (ParamType.IsEmpty())
				{
					continue;
				}

				const FString ParamName = TaskAtlasCompositeMakeUniqueIdentifier(RawKey, TEXT("arg"), UsedParamNames);
				ParamSchemaTypes.Add(ParamName, ParamType);
				RawKeyToParamName.Add(RawKey, ParamName);
			}

			TArray<FString> ParamDecls;
			for (const FString& RawKey : RawArgKeys)
			{
				const FString* ParamName = RawKeyToParamName.Find(RawKey);
				if (!ParamName)
				{
					continue;
				}
				const TSharedPtr<FJsonValue>* Value = UnrealMcp::Compat::FindJsonValue(*ArgsTemplate, RawKey);
				const FString* ParamType = ParamSchemaTypes.Find(*ParamName);
				ParamDecls.Add(FString::Printf(
					TEXT("%s: %s = %s"),
					**ParamName,
					ParamType ? **ParamType : TEXT("str"),
					*TaskAtlasCompositeJsonValueToPythonLiteral(Value ? *Value : nullptr)));
			}

			TArray<FString> BodyArgs;
			for (const FString& RawKey : RawArgKeys)
			{
				if (const FString* ParamName = RawKeyToParamName.Find(RawKey))
				{
					BodyArgs.Add(FString::Printf(TEXT("%s: %s"), *TaskAtlasCompositePythonQuote(RawKey), **ParamName));
				}
				else
				{
					const TSharedPtr<FJsonValue>* Value = UnrealMcp::Compat::FindJsonValue(*ArgsTemplate, RawKey);
					BodyArgs.Add(FString::Printf(
						TEXT("%s: %s"),
						*TaskAtlasCompositePythonQuote(RawKey),
						*TaskAtlasCompositeJsonValueToPythonLiteral(Value ? *Value : nullptr)));
				}
			}
			const FString ArgsLiteral = TEXT("{") + FString::Join(BodyArgs, TEXT(", ")) + TEXT("}");
			const FString Params = FString::Join(ParamDecls, TEXT(", "));

			OutProduct.MainPy += TEXT("\n");
			OutProduct.MainPy += TEXT("    @toolset_registry.tool_call\n");
			OutProduct.MainPy += TEXT("    @staticmethod\n");
			OutProduct.MainPy += FString::Printf(TEXT("    def %s(%s) -> str:\n"), *MethodName, *Params);
			OutProduct.MainPy += FString::Printf(TEXT("        \"\"\"Delegate to governed UEAtelier tool %s.\n\n"), *Step.ToolName);
			OutProduct.MainPy += TEXT("        Args:\n");
			if (ParamDecls.Num() == 0)
			{
				OutProduct.MainPy += TEXT("          None.\n");
			}
			else
			{
				for (const FString& RawKey : RawArgKeys)
				{
					const FString* ParamName = RawKeyToParamName.Find(RawKey);
					if (ParamName)
					{
						OutProduct.MainPy += FString::Printf(TEXT("          %s: Forwarded as %s.\n"), **ParamName, *RawKey);
					}
				}
			}
			OutProduct.MainPy += TEXT("        Returns:\n");
			OutProduct.MainPy += TEXT("          JSON string returned by UEAtelier call_tool.\n");
			OutProduct.MainPy += TEXT("        \"\"\"\n");
			OutProduct.MainPy += FString::Printf(
				TEXT("        return unreal.UnrealMcpCallTool.call_tool(%s, json.dumps(%s))\n"),
				*TaskAtlasCompositePythonQuote(Step.ToolName),
				*ArgsLiteral);

			FOfficialToolsetToolInfo ToolInfo;
			ToolInfo.Name = MethodName;
			ToolInfo.ParamSchemaTypes = MoveTemp(ParamSchemaTypes);
			ToolInfo.ReturnType = TEXT("str");
			ToolInfo.GovernedToolId = Step.ToolName;
			ToolInfo.ArgsTemplate = ArgsTemplate;
			OutProduct.Tools.Add(MoveTemp(ToolInfo));
		}

		OutProduct.MainPy += FString::Printf(TEXT("\n\nREGISTRATION = Registration((%s,))\n\n\n"), *OutProduct.ClassName);
		OutProduct.MainPy += TEXT("def register() -> bool: return REGISTRATION.register()\n\n\n");
		OutProduct.MainPy += TEXT("def unregister() -> None: REGISTRATION.unregister()\n");

		OutProduct.MainPySha256 = ComputePythonHandlerSha256(OutProduct.MainPy);
		OutProduct.SchemaHash = TaskAtlasCompositeMakeSchemaHash(OutProduct.Tools);
		OutProduct.ManifestJson = TaskAtlasCompositeBuildOfficialManifest(
			OutProduct.ToolsetName,
			OutProduct.ClassName,
			OutProduct.ModuleName,
			TaskId,
			OutProduct.SchemaHash,
			OutProduct.Tools);
		return true;
	}
#endif

	bool WriteCompositeUserToolFiles(
		const FString& ToolId,
		const FString& MainPy,
		const FString& ToolJson,
		bool bOverwrite,
		FString& OutDirectory,
		FString& OutFailureReason)
	{
		OutDirectory.Reset();
		OutFailureReason.Reset();

		if (!TaskAtlasCompositeIsValidToolId(ToolId, OutFailureReason))
		{
			return false;
		}

		UserRegistry::InitializeUserToolRegistry();
		FString RootDir = UserRegistry::GetUserToolsRootDir();
		FPaths::NormalizeFilename(RootDir);
		RootDir.RemoveFromEnd(TEXT("/"));

		FString ToolDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(RootDir, ToolId));
		FPaths::NormalizeFilename(ToolDir);
		FPaths::CollapseRelativeDirectories(ToolDir);
		ToolDir.RemoveFromEnd(TEXT("/"));

		const FString RootPrefix = RootDir + TEXT("/");
		if (!ToolDir.StartsWith(RootPrefix, ESearchCase::IgnoreCase))
		{
			OutFailureReason = TEXT("Refused to write composite user tool outside the user-tool registry root.");
			return false;
		}

		if (IFileManager::Get().DirectoryExists(*ToolDir))
		{
			if (!bOverwrite)
			{
				OutFailureReason = FString::Printf(TEXT("Composite user tool directory already exists: %s"), *ToolDir);
				return false;
			}
			if (!IFileManager::Get().DeleteDirectory(*ToolDir, false, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to replace existing composite user tool directory: %s"), *ToolDir);
				return false;
			}
		}

		if (!IFileManager::Get().MakeDirectory(*ToolDir, true))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create composite user tool directory: %s"), *ToolDir);
			return false;
		}

		const FString MainPyPath = FPaths::Combine(ToolDir, TEXT("main.py"));
		const FString ToolJsonPath = FPaths::Combine(ToolDir, TEXT("tool.json"));
		if (!FFileHelper::SaveStringToFile(MainPy, *MainPyPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to write composite main.py: %s"), *MainPyPath);
			return false;
		}
		if (!FFileHelper::SaveStringToFile(ToolJson, *ToolJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to write composite tool.json: %s"), *ToolJsonPath);
			return false;
		}

		OutDirectory = ToolDir;
		return true;
	}
}

namespace TaskAtlasWindow
{
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	bool bEmitOfficialToolsetDraft = false;
#endif

	FString JsonObjectToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, const FString& Fallback = FString())
	{
		FString Value;
		if (Object.IsValid() && Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return Fallback;
	}

	bool GetBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, bool bFallback = false)
	{
		bool bValue = bFallback;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	TArray<FString> GetStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		TArray<FString> Values;
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
		{
			return Values;
		}
		for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
		{
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				Values.Add(Value->AsString());
			}
		}
		return Values;
	}

	FString EligibilityToText(UnrealMcp::TaskAtlasService::EEligibility Eligibility)
	{
		switch (Eligibility)
		{
		case UnrealMcp::TaskAtlasService::EEligibility::PreviewReady:
			return TEXT("preview_ready");
		case UnrealMcp::TaskAtlasService::EEligibility::Partial:
			return TEXT("partial");
		case UnrealMcp::TaskAtlasService::EEligibility::Blocked:
			return TEXT("blocked");
		case UnrealMcp::TaskAtlasService::EEligibility::SkeletonPreCapture:
		default:
			return TEXT("skeleton_pre_capture");
		}
	}

	UnrealMcp::FTaskAtlasModel BuildTaskAtlasModel(const TSharedPtr<FJsonObject>& TaskObject, const FString& FallbackTaskId, const TArray<FString>& FallbackCriticalPath)
	{
		UnrealMcp::FTaskAtlasModel Model;
		Model.TaskId = GetStringField(TaskObject, TEXT("taskId"), FallbackTaskId);
		Model.CriticalPath = GetStringArrayField(TaskObject, TEXT("criticalPath"));
		if (Model.CriticalPath.Num() == 0)
		{
			Model.CriticalPath = FallbackCriticalPath;
		}

		const TArray<TSharedPtr<FJsonValue>>* StepRefs = nullptr;
		if (TaskObject.IsValid() && TaskObject->TryGetArrayField(TEXT("stepRefs"), StepRefs) && StepRefs)
		{
			for (const TSharedPtr<FJsonValue>& StepValue : *StepRefs)
			{
				const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
				if (!StepObject.IsValid())
				{
					continue;
				}

				UnrealMcp::FTaskAtlasStepRef Step;
				Step.ToolName = GetStringField(StepObject, TEXT("tool"));
				Step.EventId = GetStringField(StepObject, TEXT("eventId"));
				Step.CaptureStatus = GetStringField(StepObject, TEXT("captureStatus"));
				Step.CaptureRef = GetStringField(StepObject, TEXT("captureRef"));
				Step.CaptureSummary = GetStringField(StepObject, TEXT("captureSummary"));
				Model.StepRefs.Add(MoveTemp(Step));
			}
		}
		return Model;
	}

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
	TArray<TSharedPtr<FJsonValue>> MakeOfficialStepRefs(const TSharedPtr<FJsonObject>& TaskObject)
	{
		TArray<TSharedPtr<FJsonValue>> StepRefs;
		const TArray<TSharedPtr<FJsonValue>>* RawStepRefs = nullptr;
		if (!TaskObject.IsValid() || !TaskObject->TryGetArrayField(TEXT("stepRefs"), RawStepRefs) || !RawStepRefs)
		{
			return StepRefs;
		}
		StepRefs.Reserve(RawStepRefs->Num());
		for (int32 Index = 0; Index < RawStepRefs->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& StepValue = (*RawStepRefs)[Index];
			const TSharedPtr<FJsonObject> RawStep = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
			if (!RawStep.IsValid())
			{
				continue;
			}

			TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
			double Ordinal = static_cast<double>(Index);
			RawStep->TryGetNumberField(TEXT("ordinal"), Ordinal);
			Step->SetNumberField(TEXT("ordinal"), Ordinal);
			for (const TCHAR* FieldName : { TEXT("tool"), TEXT("eventId"), TEXT("captureStatus"), TEXT("captureRef"), TEXT("captureSummary"), TEXT("policyClassAtCapture") })
			{
				const FString Value = GetStringField(RawStep, FieldName).TrimStartAndEnd();
				if (!Value.IsEmpty())
				{
					Step->SetStringField(FieldName, Value);
				}
			}
			StepRefs.Add(MakeShared<FJsonValueObject>(Step));
		}
		return StepRefs;
	}

	TSet<FString> VisibleCoreToolNames()
	{
		TSet<FString> VisibleTools;
		for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
		{
			if (Entry.Exposure == UnrealMcp::EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
			{
				VisibleTools.Add(Entry.Name);
			}
		}
		return VisibleTools;
	}

	bool TryBuildOfficialDraftRequest(
		const STaskAtlasWindow::FWorkflowRow& Row,
		const UnrealMcp::TaskAtlasService::FMakeCompositeResult& CompositeResult,
		UnrealMcp::TaskAtlasService::FOfficialToolsetDraftRequest& OutRequest,
		FString& OutFailureReason)
	{
		if (!Row.Json.IsValid())
		{
			OutFailureReason = TEXT("Selected Task Atlas row does not include task JSON.");
			return false;
		}

		FString ToolId;
		if (CompositeResult.ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive))
		{
			ToolId = CompositeResult.ToolName.RightChop(5);
		}
		if (ToolId.IsEmpty())
		{
			ToolId = UnrealMcp::TaskAtlasService::MakeAtlasToolId(Row.Label.TrimStartAndEnd(), Row.TaskId);
		}

		FString Description = GetStringField(Row.Json, TEXT("aiSummaryText")).TrimStartAndEnd();
		if (Description.IsEmpty())
		{
			Description = GetStringField(Row.Json, TEXT("userIntentText")).TrimStartAndEnd();
		}
		if (Description.IsEmpty())
		{
			Description = FString::Printf(TEXT("Official ToolsetRegistry draft generated from Task Atlas task %s."), *Row.TaskId);
		}

		OutRequest = UnrealMcp::TaskAtlasService::FOfficialToolsetDraftRequest();
		OutRequest.ToolId = ToolId;
		OutRequest.Title = Row.Label.TrimStartAndEnd().IsEmpty() ? ToolId : Row.Label.TrimStartAndEnd();
		OutRequest.Description = Description;
		OutRequest.TaskId = Row.TaskId;
		OutRequest.ReplayEligibility = CompositeResult.ReplayStatus.IsEmpty()
			? EligibilityToText(Row.Eligibility)
			: CompositeResult.ReplayStatus;
		OutRequest.ReplayUnavailableReason = Row.ReplayUnavailableReason;
		OutRequest.CriticalPath = Row.CriticalPath.Num() > 0
			? Row.CriticalPath
			: GetStringArrayField(Row.Json, TEXT("criticalPath"));
		OutRequest.StepRefs = MakeOfficialStepRefs(Row.Json);
		OutRequest.VisibleCoreToolNames = VisibleCoreToolNames();
		return true;
	}

	FString OfficialDraftStatusText(const UnrealMcp::TaskAtlasService::FOfficialToolsetDraftResult& Result)
	{
		if (Result.bSucceeded)
		{
			return FString::Printf(TEXT("; official draft %s generated at %s"), *Result.ToolsetName, *Result.GeneratedDir);
		}
		if (!Result.FailureDiagnosticPath.IsEmpty())
		{
			return FString::Printf(TEXT("; official draft failed: %s. See %s"), *Result.ErrorMessage, *Result.FailureDiagnosticPath);
		}
		return FString::Printf(TEXT("; official draft failed: %s"), *Result.ErrorMessage);
	}
#endif

	int32 WorkflowSortRank(const STaskAtlasWindow::FWorkflowRow& Row)
	{
		if (Row.bPinned)
		{
			return 0;
		}
		if (Row.Rating == TEXT("success"))
		{
			return 1;
		}
		return 2;
	}

	void PopulateVisibleToolRows(TArray<STaskAtlasWindow::FToolRow>& Tools, TMap<FString, STaskAtlasWindow::FToolRow>& ToolsByName)
	{
		for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
		{
			if (Entry.Exposure != UnrealMcp::EToolExposure::Visible)
			{
				continue;
			}

			STaskAtlasWindow::FToolRow Tool;
			Tool.Name = Entry.Name;
			Tool.Category = Entry.Category;
			Tool.RiskLevel = UnrealMcp::LexToString(Entry.Policy.RiskLevel);
			Tool.Description = Entry.Description;
			Tool.Owner = Entry.Policy.Owner;
			Tool.DocsPath = Entry.Policy.DocsPath;
			Tool.InputSchemaText = JsonObjectToPrettyString(Entry.InputSchema);
			Tools.Add(Tool);
			ToolsByName.Add(Tool.Name, Tool);
		}

		Tools.Sort([](const STaskAtlasWindow::FToolRow& Left, const STaskAtlasWindow::FToolRow& Right)
		{
			return Left.Name < Right.Name;
		});
	}

	void AppendWorkflowRowsFromTaskValues(const TArray<TSharedPtr<FJsonValue>>& TaskValues, TArray<STaskAtlasWindow::FWorkflowRow>& Workflows)
	{
		for (const TSharedPtr<FJsonValue>& Value : TaskValues)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject> TaskObject = Value->AsObject();
			STaskAtlasWindow::FWorkflowRow Row;
			Row.TaskId = GetStringField(TaskObject, TEXT("taskId"));
			Row.Label = GetStringField(TaskObject, TEXT("label"), Row.TaskId);
			Row.Rating = GetStringField(TaskObject, TEXT("rating"), TEXT("unrated"));
			Row.TEndUtc = GetStringField(TaskObject, TEXT("tEndUtc"));
			Row.ReplayEligibility = UnrealMcp::TaskAtlasComposite::NormalizeReplayEligibility(
				GetStringField(TaskObject, TEXT("replayEligibility"), TEXT("skeleton_pre_capture")));
			Row.ReplayUnavailableReason = GetStringField(TaskObject, TEXT("replayUnavailableReason"));
			Row.bPinned = GetBoolField(TaskObject, TEXT("pinned"));
			Row.CriticalPath = GetStringArrayField(TaskObject, TEXT("criticalPath"));
			Row.Json = TaskObject;

			const UnrealMcp::FTaskAtlasModel TaskModel = BuildTaskAtlasModel(Row.Json, Row.TaskId, Row.CriticalPath);
			const UnrealMcp::TaskAtlasService::FEligibilityResult Eligibility = UnrealMcp::TaskAtlasService::ClassifyTask(TaskModel);
			Row.Eligibility = Eligibility.Eligibility;
			Row.BlockedFirstStep = Eligibility.BlockedFirstStep;
			Row.BlockedFirstReason = Eligibility.BlockedFirstReason;
			Row.ReplayEligibility = EligibilityToText(Eligibility.Eligibility);
			if (Row.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Blocked && !Row.BlockedFirstReason.IsEmpty())
			{
				Row.ReplayUnavailableReason = Row.BlockedFirstReason;
			}
			Workflows.Add(Row);
		}
	}

	FString JoinCriticalPath(const TArray<FString>& CriticalPath)
	{
		TArray<FString> DisplayTools;
		for (int32 Index = 0; Index < CriticalPath.Num() && Index < 10; ++Index)
		{
			DisplayTools.Add(CriticalPath[Index]);
		}
		FString Text = FString::Join(DisplayTools, TEXT(" -> "));
		if (CriticalPath.Num() > 10)
		{
			Text += TEXT(" -> ...");
		}
		return Text;
	}

	FString TrimDashes(FString Value)
	{
		while (Value.StartsWith(TEXT("-")))
		{
			Value.RightChopInline(1);
		}
		while (Value.EndsWith(TEXT("-")))
		{
			Value.LeftChopInline(1);
		}
		return Value;
	}

	void AppendNormalizedDash(FString& OutValue)
	{
		if (!OutValue.IsEmpty() && !OutValue.EndsWith(TEXT("-")))
		{
			OutValue.AppendChar('-');
		}
	}

	FString SanitizeSkillSlugPart(const FString& Source)
	{
		FString Slug;
		for (const TCHAR Ch : Source)
		{
			if (Ch >= 'A' && Ch <= 'Z')
			{
				Slug.AppendChar(static_cast<TCHAR>(Ch + ('a' - 'A')));
			}
			else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
			{
				Slug.AppendChar(Ch);
			}
			else if (FChar::IsWhitespace(Ch) || Ch == '_' || Ch == '-')
			{
				AppendNormalizedDash(Slug);
			}
		}

		Slug = TrimDashes(Slug);
		if (Slug.Len() > 64)
		{
			Slug = TrimDashes(Slug.Left(64));
		}
		return Slug;
	}

	FString MakeSkillSlug(const FString& Label, const FString& TaskId)
	{
		FString Slug = SanitizeSkillSlugPart(Label);
		if (Slug.IsEmpty() && !TaskId.TrimStartAndEnd().IsEmpty())
		{
			Slug = SanitizeSkillSlugPart(FString::Printf(TEXT("task-%s"), *TaskId));
		}
		if (Slug.IsEmpty())
		{
			Slug = TEXT("task-atlas-workflow");
		}
		return Slug;
	}
}

void STaskAtlasWindow::Construct(const FArguments& InArgs, FUnrealMcpModule* InOwnerModule)
{
	OwnerModule = InOwnerModule;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Task Atlas"))
					.Font(FAppStyle::GetFontStyle("HeadingMedium"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.OnClicked(this, &STaskAtlasWindow::HandleRefreshClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Debug", "Debug"))
					.ToolTipText(LOCTEXT("DebugTooltip", "Show user registry introspection summary"))
					.OnClicked(this, &STaskAtlasWindow::HandleDebugClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("StatusInitial", "Loading Task Atlas."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.64f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.58f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Workflows", "Workflows"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(WorkflowListBox, SVerticalBox)
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.42f)
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("UnusedTools", "Unused Tools"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(UnusedToolListBox, SVerticalBox)
								]
							]
						]
					]
				]
				+ SSplitter::Slot()
				.Value(0.36f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.62f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(10.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(ToolDetailTitle, STextBlock)
								.Text(LOCTEXT("ToolDetailTitle", "Tool Details"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SAssignNew(ToolDetailMeta, STextBlock)
								.AutoWrapText(true)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text(LOCTEXT("ToolDetailMeta", "Select a tool name to inspect registry metadata."))
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SAssignNew(ToolDetailDescription, STextBlock)
								.AutoWrapText(true)
								.Text(FText::GetEmpty())
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(ToolDetailSchema, STextBlock)
									.AutoWrapText(true)
									.Font(FAppStyle::GetFontStyle("SmallFont"))
									.Text(FText::GetEmpty())
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.38f)
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("MadeTools", "Made Tools"))
									.Font(FAppStyle::GetFontStyle("NormalFontBold"))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SButton)
									.Text(LOCTEXT("RefreshMadeTools", "Refresh"))
									.OnClicked(this, &STaskAtlasWindow::HandleRefreshClicked)
								]
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(MadeToolListBox, SVerticalBox)
								]
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("SearchHint", "Search workflows and tools"))
				.OnTextChanged(this, &STaskAtlasWindow::HandleSearchChanged)
			]
		]
	];

	RefreshData();
}

FReply STaskAtlasWindow::HandleRefreshClicked()
{
	RefreshData();
	return FReply::Handled();
}

void STaskAtlasWindow::HandleSearchChanged(const FText& NewText)
{
	SearchText = NewText.ToString().TrimStartAndEnd().ToLower();
	RebuildLists();
}

FReply STaskAtlasWindow::HandlePinClicked(FString TaskId, bool bNewPinned)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("taskId"), TaskId);
	Arguments->SetBoolField(TEXT("pinned"), bNewPinned);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_pin"), *Arguments);
	SetStatus(Result.Text);
	if (!Result.bIsError)
	{
		RefreshData();
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleMakeToolClicked(FWorkflowRow Row)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	UnrealMcp::TaskAtlasService::FMakeCompositeRequest Req;
	Req.TaskId = Row.TaskId;
	Req.bPreferDocumentOnly = Row.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Blocked;
	const FString ExpectedToolName = FString::Printf(
		TEXT("user.%s"),
		*UnrealMcp::TaskAtlasService::MakeAtlasToolId(Row.Label.TrimStartAndEnd(), Row.TaskId));
	const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Req);

	switch (Result.Outcome)
	{
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::CompositeWritten:
	{
		FString Status = FString::Printf(
			TEXT("Composite %s written at %s"),
			*(Result.ToolName.IsEmpty() ? ExpectedToolName : Result.ToolName),
			*Result.GeneratedDir);
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
		if (TaskAtlasWindow::bEmitOfficialToolsetDraft)
		{
			UnrealMcp::TaskAtlasService::FOfficialToolsetDraftRequest OfficialRequest;
			FString FailureReason;
			if (TaskAtlasWindow::TryBuildOfficialDraftRequest(Row, Result, OfficialRequest, FailureReason))
			{
				const UnrealMcp::TaskAtlasService::FOfficialToolsetDraftResult OfficialResult =
					UnrealMcp::TaskAtlasService::GenerateOfficialToolsetDraft(OfficialRequest);
				Status += TaskAtlasWindow::OfficialDraftStatusText(OfficialResult);
			}
			else
			{
				Status += FString::Printf(TEXT("; official draft failed: %s"), *FailureReason);
			}
		}
#endif
		SetStatus(Status);
		break;
	}
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::DocumentOnly:
		SetStatus(FString::Printf(
			TEXT("Document-only (eligibility=%s) at %s"),
			*TaskAtlasWindow::EligibilityToText(Result.Eligibility.Eligibility),
			*Result.DocumentPath));
		break;
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::Blocked:
		SetStatus(FString::Printf(
			TEXT("Blocked at step %d (%s). Use Distill Skill / To RAG."),
			Result.Eligibility.BlockedFirstStep,
			*Result.Eligibility.BlockedFirstReason));
		break;
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::StagingFailed:
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::ReloadRejected:
		SetStatus(FString::Printf(TEXT("Failed: %s. See %s"), *Result.ErrorMessage, *Result.FailureDiagnosticPath));
		break;
	case UnrealMcp::TaskAtlasService::EMakeCompositeOutcome::Skipped:
	default:
		SetStatus(FString::Printf(TEXT("Skipped: %s"), *Result.ErrorMessage));
		break;
	}

	RefreshMadeTools();
	RebuildLists();
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandlePromoteToSkillsClicked(FWorkflowRow Row)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}
	if (Row.TaskId.TrimStartAndEnd().IsEmpty())
	{
		SetStatus(TEXT("Distill Skill requires a taskId on the selected workflow."));
		return FReply::Handled();
	}

	const FString SessionId = TaskAtlasWindow::GetStringField(Row.Json, TEXT("sessionId"));
	if (SessionId.TrimStartAndEnd().IsEmpty())
	{
		SetStatus(TEXT("Distill Skill requires a sessionId on the selected workflow."));
		return FReply::Handled();
	}

	const FString SkillName = TaskAtlasWindow::MakeSkillSlug(Row.Label, Row.TaskId);
	const FString UserIntent = TaskAtlasWindow::GetStringField(Row.Json, TEXT("userIntentText"));
	const FString Goal = UserIntent.TrimStartAndEnd().IsEmpty() ? Row.Label : UserIntent.TrimStartAndEnd();

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("sessionId"), SessionId);
	Arguments->SetStringField(TEXT("skillName"), SkillName);
	Arguments->SetStringField(TEXT("title"), Row.Label);
	Arguments->SetStringField(TEXT("goal"), Goal);
	Arguments->SetBoolField(TEXT("writeDraft"), true);
	Arguments->SetBoolField(TEXT("overwrite"), true);
	Arguments->SetNumberField(TEXT("maxEvents"), 200.0);
	Arguments->SetBoolField(TEXT("includeEvents"), false);

	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.skill_distill_from_activity"), *Arguments);
	if (Result.bIsError)
	{
		SetStatus(Result.Text.IsEmpty() ? FString(TEXT("Distill Skill failed.")) : Result.Text);
		return FReply::Handled();
	}

	const FString DraftPath = TaskAtlasWindow::GetStringField(Result.StructuredContent, TEXT("draftPath"));
	const FString ReturnedSkillName = TaskAtlasWindow::GetStringField(Result.StructuredContent, TEXT("skillName"), SkillName);
	if (!DraftPath.IsEmpty())
	{
		SetStatus(FString::Printf(TEXT("Skill draft '%s' written: %s"), *ReturnedSkillName, *DraftPath));
	}
	else
	{
		SetStatus(Result.Text.IsEmpty() ? FString::Printf(TEXT("Skill draft '%s' written."), *ReturnedSkillName) : Result.Text);
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandlePromoteToRagClicked(FWorkflowRow Row)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	UnrealMcp::TaskAtlasService::FPromoteToRagRequest Req;
	Req.TaskId = Row.TaskId;
	Req.bDryRun = false;
	const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(Req);

	switch (Result.Outcome)
	{
	case UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::Promoted:
		SetStatus(FString::Printf(
			TEXT("RAG source written: %s. Refresh: %s"),
			*Result.KnowledgeSourcePath,
			*Result.RefreshResultText));
		break;
	case UnrealMcp::TaskAtlasService::EPromoteToRagOutcome::RefreshFailed:
		SetStatus(FString::Printf(TEXT("Written but refresh failed: %s"), *Result.ErrorMessage));
		break;
	default:
		SetStatus(FString::Printf(TEXT("Promote: %s"), *Result.ErrorMessage));
		break;
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleToolClicked(FString ToolName)
{
	ShowToolDetails(ToolName);
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleDeleteMadeToolClicked(FString ToolName)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	const EAppReturnType::Type Response = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FText::Format(
			LOCTEXT("ConfirmDeleteMadeTool", "Delete made tool '{0}'?"),
			FText::FromString(ToolName)));
	if (Response != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(ToolName);
	switch (Result.Outcome)
	{
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Deleted:
		SetStatus(FString::Printf(TEXT("Deleted %s"), *ToolName));
		break;
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::NotFound:
		SetStatus(FString::Printf(TEXT("Tool %s not found"), *ToolName));
		break;
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Refused:
		SetStatus(FString::Printf(TEXT("Refused: %s"), *Result.ErrorMessage));
		break;
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::ReloadRejected:
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::Failed:
		SetStatus(FString::Printf(TEXT("Error: %s. See %s"), *Result.ErrorMessage, *Result.FailureDiagnosticPath));
		break;
	case UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome::DryRun:
	default:
		SetStatus(FString::Printf(TEXT("Delete: %s"), *Result.ErrorMessage));
		break;
	}

	RefreshMadeTools();
	RebuildLists();
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleTestNowClicked(FString ToolName)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	UnrealMcp::TaskAtlasService::FSmokeRequest Req;
	Req.ToolName = ToolName;
	Req.bDryRun = false;
	const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(Req);

	switch (Result.Outcome)
	{
	case UnrealMcp::TaskAtlasService::ESmokeOutcome::Passed:
		SetStatus(FString::Printf(TEXT("Smoke passed: %s"), *ToolName));
		break;
	case UnrealMcp::TaskAtlasService::ESmokeOutcome::Failed:
		SetStatus(FString::Printf(TEXT("Smoke failed: %s. See %s"), *Result.ErrorMessage, *Result.FailureDiagnosticPath));
		break;
	default:
		SetStatus(FString::Printf(TEXT("Smoke: %s"), *Result.ErrorMessage));
		break;
	}

	RefreshMadeTools();
	RebuildLists();
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleDebugClicked()
{
	const TArray<UnrealMcp::TaskAtlasService::FUserToolView> Views = UnrealMcp::TaskAtlasService::IntrospectUserRegistry();

	FString Summary = FString::Printf(TEXT("User registry: %d tools loaded.\n"), Views.Num());
	for (const UnrealMcp::TaskAtlasService::FUserToolView& View : Views)
	{
		Summary += FString::Printf(
			TEXT("- %s (%s, %s)\n"),
			*View.ToolName,
			*View.Generator,
			View.bLoaded ? TEXT("loaded") : TEXT("not loaded"));
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
	return FReply::Handled();
}

void STaskAtlasWindow::RefreshData()
{
	Workflows.Reset();
	Tools.Reset();
	MadeTools.Reset();
	ToolsByName.Reset();

	TaskAtlasWindow::PopulateVisibleToolRows(Tools, ToolsByName);

	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		RefreshMadeTools();
		RebuildLists();
		return;
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("filter"), TEXT("all"));
	Arguments->SetNumberField(TEXT("limit"), 200.0);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_list"), *Arguments);
	if (Result.bIsError || !Result.StructuredContent.IsValid())
	{
		SetStatus(Result.Text.IsEmpty() ? TEXT("Task list failed.") : Result.Text);
		RebuildLists();
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* TaskValues = nullptr;
	if (Result.StructuredContent->TryGetArrayField(TEXT("tasks"), TaskValues) && TaskValues)
	{
		TaskAtlasWindow::AppendWorkflowRowsFromTaskValues(*TaskValues, Workflows);
	}

	Workflows.Sort([](const FWorkflowRow& Left, const FWorkflowRow& Right)
	{
		const int32 LeftRank = TaskAtlasWindow::WorkflowSortRank(Left);
		const int32 RightRank = TaskAtlasWindow::WorkflowSortRank(Right);
		if (LeftRank != RightRank)
		{
			return LeftRank < RightRank;
		}
		return Left.TEndUtc > Right.TEndUtc;
	});

	RefreshMadeTools();
	SetStatus(FString::Printf(
		TEXT("Loaded %d workflow(s), %d visible tool(s), and %d made tool(s)."),
		Workflows.Num(),
		Tools.Num(),
		MadeTools.Num()));
	RebuildLists();
}

void STaskAtlasWindow::RefreshMadeTools()
{
	MadeTools.Reset();

	const TArray<UnrealMcp::TaskAtlasService::FMadeToolEntry> Entries = UnrealMcp::TaskAtlasService::ListMadeTools();
	for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : Entries)
	{
		FMadeToolRow Row;
		Row.ToolName = Entry.ToolName;
		Row.ScaffoldDir = Entry.ScaffoldDir;
		Row.RelativeScaffoldDir = Entry.ScaffoldDir;
		Row.CompositeKind = Entry.CompositeKind;
		Row.ReplayStatus = Entry.ReplayStatus;
		Row.CreatedUtc = Entry.CreatedUtc;
		Row.SourceTaskId = Entry.SourceTaskId;
		Row.bLoaded = Entry.bLoadedInUserRegistry;
		Row.bHasFailureMarker = Entry.bHasFailureMarker;
		MadeTools.Add(Row);
	}

	MadeTools.Sort([](const FMadeToolRow& Left, const FMadeToolRow& Right)
	{
		return Left.ToolName < Right.ToolName;
	});
}

void STaskAtlasWindow::RebuildLists()
{
	if (WorkflowListBox.IsValid())
	{
		WorkflowListBox->ClearChildren();
	}
	if (UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->ClearChildren();
	}
	if (MadeToolListBox.IsValid())
	{
		MadeToolListBox->ClearChildren();
	}

	TSet<FString> UsedTools;
	int32 DisplayedWorkflowCount = 0;
	for (const FWorkflowRow& Row : Workflows)
	{
		if (Row.Rating == TEXT("failed") || !WorkflowMatchesSearch(Row))
		{
			continue;
		}

		for (const FString& ToolName : Row.CriticalPath)
		{
			UsedTools.Add(ToolName);
		}
		++DisplayedWorkflowCount;
		if (WorkflowListBox.IsValid())
		{
			WorkflowListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				BuildWorkflowRow(Row)
			];
		}
	}

	if (DisplayedWorkflowCount == 0 && WorkflowListBox.IsValid())
	{
		WorkflowListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoWorkflows", "No matching workflows."))
		];
	}

	int32 DisplayedUnusedCount = 0;
	for (const FToolRow& Tool : Tools)
	{
		if (UsedTools.Contains(Tool.Name) || !ToolMatchesSearch(Tool))
		{
			continue;
		}
		++DisplayedUnusedCount;
		if (UnusedToolListBox.IsValid())
		{
			UnusedToolListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				BuildUnusedToolRow(Tool)
			];
		}
	}

	if (DisplayedUnusedCount == 0 && UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoUnusedTools", "No matching unused tools."))
		];
	}

	int32 DisplayedMadeToolCount = 0;
	for (const FMadeToolRow& Row : MadeTools)
	{
		if (!MadeToolMatchesSearch(Row))
		{
			continue;
		}
		++DisplayedMadeToolCount;
		if (MadeToolListBox.IsValid())
		{
			MadeToolListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				BuildMadeToolRow(Row)
			];
		}
	}

	if (DisplayedMadeToolCount == 0 && MadeToolListBox.IsValid())
	{
		MadeToolListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoMadeTools", "No matching made tools."))
		];
	}
}

TSharedRef<SWidget> STaskAtlasWindow::BuildWorkflowRow(const FWorkflowRow& Row)
{
	const FString PinPrefix = Row.bPinned ? TEXT("[PINNED] ") : FString();
	const FString ReplayStatus = UnrealMcp::TaskAtlasComposite::ReplayStatusForReplayEligibility(Row.ReplayEligibility);
	const FString CriticalPathText = TaskAtlasWindow::JoinCriticalPath(Row.CriticalPath);
	TSharedRef<SWrapBox> CriticalPathWrap = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0f, 4.0f));

	if (Row.CriticalPath.Num() == 0)
	{
		CriticalPathWrap->AddSlot()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoCriticalPath", "No tools recorded"))
		];
	}
	else
	{
		const int32 DisplayCount = FMath::Min(Row.CriticalPath.Num(), 10);
		for (int32 Index = 0; Index < DisplayCount; ++Index)
		{
			CriticalPathWrap->AddSlot()
			[
				BuildToolNameButton(Row.CriticalPath[Index])
			];
		}
		if (Row.CriticalPath.Num() > 10)
		{
			CriticalPathWrap->AddSlot()
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("MoreTools", "..."))
			];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.Text(FText::FromString(PinPrefix + Row.Label))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(TEXT("%s | %s | %s"), *Row.Rating, *ReplayStatus, *CriticalPathText)))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					CriticalPathWrap
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(FText::FromString(Row.bPinned ? TEXT("Unpin") : TEXT("Pin")))
					.OnClicked(this, &STaskAtlasWindow::HandlePinClicked, Row.TaskId, !Row.bPinned)
				]
#if UNREALMCP_HAS_OFFICIAL_TOOLSETS
				+ SWrapBox::Slot()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([]()
					{
						return TaskAtlasWindow::bEmitOfficialToolsetDraft ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.IsEnabled(Row.Eligibility != UnrealMcp::TaskAtlasService::EEligibility::Blocked)
					.ToolTipText(LOCTEXT("EmitOfficialToolsetTooltip", "Also generate an official ToolsetRegistry Python draft for UE5.8."))
					.OnCheckStateChanged_Lambda([](ECheckBoxState NewState)
					{
						TaskAtlasWindow::bEmitOfficialToolsetDraft = NewState == ECheckBoxState::Checked;
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EmitOfficialToolset", "Also generate official toolset (UE5.8)"))
					]
				]
#endif
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("MakeToolSet", "Make Tool Set"))
					.IsEnabled(Row.Eligibility != UnrealMcp::TaskAtlasService::EEligibility::Blocked)
					.ToolTipText(Row.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Blocked
						? FText::Format(
							LOCTEXT("MakeToolSetBlockedTooltip", "Blocked at step {0}: {1}. Use Distill Skill / To RAG."),
							FText::AsNumber(Row.BlockedFirstStep),
							FText::FromString(Row.BlockedFirstReason))
						: LOCTEXT("MakeToolSetTooltip", "Generate composite Python user tool"))
					.OnClicked(this, &STaskAtlasWindow::HandleMakeToolClicked, Row)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("DistillSkill", "Distill Skill"))
					.OnClicked(this, &STaskAtlasWindow::HandlePromoteToSkillsClicked, Row)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ToRag", "To RAG"))
					.OnClicked(this, &STaskAtlasWindow::HandlePromoteToRagClicked, Row)
				]
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildUnusedToolRow(const FToolRow& Row)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				BuildToolNameButton(Row.Name)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FString::Printf(TEXT("[%s] [%s]"), *Row.Category, *Row.RiskLevel)))
			]
	];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildMadeToolRow(const FMadeToolRow& Row)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.Text(FText::FromString(Row.ToolName))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(
						TEXT("%s | %s"),
						*Row.CompositeKind,
						*Row.RelativeScaffoldDir)))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("TestNow", "Test Now"))
					.ToolTipText(LOCTEXT("TestNowTooltip", "Run user-tool smoke (real execution)"))
					.OnClicked(this, &STaskAtlasWindow::HandleTestNowClicked, Row.ToolName)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("DeleteMadeTool", "Delete"))
					.OnClicked(this, &STaskAtlasWindow::HandleDeleteMadeToolClicked, Row.ToolName)
				]
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildToolNameButton(const FString& ToolName)
{
	return SNew(SButton)
		.ContentPadding(FMargin(6.0f, 2.0f))
		.Text(FText::FromString(ToolName))
		.OnClicked(this, &STaskAtlasWindow::HandleToolClicked, ToolName);
}

void STaskAtlasWindow::ShowToolDetails(const FString& ToolName)
{
	const FToolRow* Tool = ToolsByName.Find(ToolName);
	if (!Tool)
	{
		SetStatus(FString::Printf(TEXT("Tool %s was not found in the registry."), *ToolName));
		return;
	}

	if (ToolDetailTitle.IsValid())
	{
		ToolDetailTitle->SetText(FText::FromString(Tool->Name));
	}
	if (ToolDetailMeta.IsValid())
	{
		ToolDetailMeta->SetText(FText::FromString(FString::Printf(
			TEXT("category: %s | risk: %s | owner: %s | docs: %s"),
			*Tool->Category,
			*Tool->RiskLevel,
			*Tool->Owner,
			*Tool->DocsPath)));
	}
	if (ToolDetailDescription.IsValid())
	{
		ToolDetailDescription->SetText(FText::FromString(Tool->Description));
	}
	if (ToolDetailSchema.IsValid())
	{
		ToolDetailSchema->SetText(FText::FromString(Tool->InputSchemaText));
	}
}

bool STaskAtlasWindow::WorkflowMatchesSearch(const FWorkflowRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	if (Row.Label.ToLower().Contains(SearchText)
		|| Row.Rating.ToLower().Contains(SearchText)
		|| Row.ReplayEligibility.ToLower().Contains(SearchText)
		|| Row.ReplayUnavailableReason.ToLower().Contains(SearchText))
	{
		return true;
	}
	for (const FString& ToolName : Row.CriticalPath)
	{
		if (ToolName.ToLower().Contains(SearchText))
		{
			return true;
		}
	}
	return false;
}

bool STaskAtlasWindow::ToolMatchesSearch(const FToolRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	return Row.Name.ToLower().Contains(SearchText)
		|| Row.Category.ToLower().Contains(SearchText)
		|| Row.RiskLevel.ToLower().Contains(SearchText)
		|| Row.Description.ToLower().Contains(SearchText);
}

bool STaskAtlasWindow::MadeToolMatchesSearch(const FMadeToolRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	return Row.ToolName.ToLower().Contains(SearchText)
		|| Row.CompositeKind.ToLower().Contains(SearchText)
		|| Row.RelativeScaffoldDir.ToLower().Contains(SearchText);
}

void STaskAtlasWindow::SetStatus(const FString& Status)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Status));
	}
}

#undef LOCTEXT_NAMESPACE
