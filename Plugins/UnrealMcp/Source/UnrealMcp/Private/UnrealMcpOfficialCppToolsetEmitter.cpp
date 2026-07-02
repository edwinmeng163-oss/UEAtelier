#include "UnrealMcpOfficialCppToolsetEmitter.h"

#if UNREALMCP_HAS_OFFICIAL_TOOLSETS

#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp::TaskAtlasOfficialCpp
{
	namespace
	{
		static FCriticalSection GOfficialCppDraftMutationLock;

		struct FCppEmitterStep
		{
			FString ToolName;
			FString CaptureRef;
			FString CaptureStatus;
			TSharedPtr<FJsonObject> Args;
		};

		FString CppEmitterNormalizeAbsolutePath(const FString& Path)
		{
			FString Result = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Result);
			FPaths::CollapseRelativeDirectories(Result);
			Result.RemoveFromEnd(TEXT("/"));
			return Result;
		}

		bool CppEmitterPathEqualsOrChild(const FString& ChildPath, const FString& RootPath)
		{
			FString Child = CppEmitterNormalizeAbsolutePath(ChildPath);
			FString Root = CppEmitterNormalizeAbsolutePath(RootPath);
#if PLATFORM_WINDOWS || PLATFORM_MAC
			Child = Child.ToLower();
			Root = Root.ToLower();
#endif
			return Child == Root || Child.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive);
		}

		bool CppEmitterIsSafeDirectoryName(const FString& DirectoryName)
		{
			return !DirectoryName.IsEmpty()
				&& !DirectoryName.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				&& FPaths::IsRelative(DirectoryName);
		}

		bool CppEmitterIsValidToolId(const FString& ToolId, FString& OutReason)
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
			if (!CppEmitterIsSafeDirectoryName(ToolId))
			{
				OutReason = TEXT("Tool id must be a safe single directory name without slashes, traversal, drives, or UNC paths.");
				return false;
			}
			for (const TCHAR Character : ToolId)
			{
				const bool bAlpha = (Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('A') && Character <= TEXT('Z'));
				const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
				if (!bAlpha && !bDigit && Character != TEXT('_'))
				{
					OutReason = TEXT("Tool id may contain only alphanumeric characters and underscores.");
					return false;
				}
			}
			return true;
		}

		FString CppEmitterJsonToString(const TSharedPtr<FJsonObject>& Object, bool bPretty)
		{
			FString Output;
			if (!Object.IsValid())
			{
				return Output;
			}
			if (bPretty)
			{
				const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
				FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			}
			else
			{
				const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
				FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			}
			return Output;
		}

		bool CppEmitterParseJsonObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		FString CppEmitterTextLiteral(const FString& Value)
		{
			FString Result = TEXT("TEXT(\"");
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
			Result += TEXT("\")");
			return Result;
		}

		FString CppEmitterPascalIdentifier(const FString& Value, const FString& Fallback)
		{
			FString Result;
			bool bCapNext = true;
			for (const TCHAR Character : Value.TrimStartAndEnd())
			{
				const bool bAlpha = FChar::IsAlpha(Character);
				const bool bDigit = FChar::IsDigit(Character);
				if (!bAlpha && !bDigit)
				{
					bCapNext = true;
					continue;
				}
				TCHAR OutChar = Character;
				if (Result.IsEmpty() && bDigit)
				{
					Result += TEXT("N");
				}
				if (bCapNext && bAlpha)
				{
					OutChar = FChar::ToUpper(Character);
				}
				Result.AppendChar(OutChar);
				bCapNext = false;
			}
			return Result.IsEmpty() ? Fallback : Result;
		}

		FString CppEmitterApiMacro(const FString& ModuleName)
		{
			FString Result;
			for (const TCHAR Character : ModuleName)
			{
				if (FChar::IsAlpha(Character))
				{
					Result.AppendChar(FChar::ToUpper(Character));
				}
				else if (FChar::IsDigit(Character))
				{
					Result.AppendChar(Character);
				}
				else
				{
					Result.AppendChar(TEXT('_'));
				}
			}
			return Result + TEXT("_API");
		}

		FString CppEmitterToolsetName(const FString& ToolId, const FString& ModuleName, const FString& ClassName)
		{
			return FString::Printf(
				TEXT("Temp.UnrealMcp.OfficialCppToolsetDrafts.%s.%s.%s"),
				*ToolId,
				*ModuleName,
				*ClassName);
		}

		FString CppEmitterGetStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
		}

		bool CppEmitterIsReplayableCaptureStatus(const FString& CaptureStatus)
		{
			const FString Normalized = CaptureStatus.TrimStartAndEnd().ToLower();
			return Normalized == TEXT("captured") || Normalized == TEXT("redacted");
		}

		TSharedPtr<FJsonObject> CppEmitterReadArgsTemplate(const FString& CaptureRef, const FString& CaptureStatus)
		{
			if (CaptureRef.TrimStartAndEnd().IsEmpty() || !CppEmitterIsReplayableCaptureStatus(CaptureStatus))
			{
				return MakeShared<FJsonObject>();
			}

			TSharedPtr<FJsonObject> CapturedContent;
			FString ReadError;
			if (!UnrealMcp::CapturedArgsStore::ReadCapturedArgs(CaptureRef, CapturedContent, ReadError))
			{
				return MakeShared<FJsonObject>();
			}

			const TSharedPtr<FJsonObject>* SanitizedArgs = nullptr;
			if (!CapturedContent.IsValid()
				|| !CapturedContent->TryGetObjectField(TEXT("sanitizedArguments"), SanitizedArgs)
				|| !SanitizedArgs
				|| !(*SanitizedArgs).IsValid())
			{
				return MakeShared<FJsonObject>();
			}
			return *SanitizedArgs;
		}

		TSet<FString> CppEmitterVisibleCoreToolNames()
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

		TArray<FCppEmitterStep> CppEmitterCollectSteps(
			const TArray<FString>& CriticalPath,
			const TArray<TSharedPtr<FJsonValue>>& StepRefs,
			const TSet<FString>& VisibleCoreToolNames)
		{
			TArray<FCppEmitterStep> Steps;
			for (const TSharedPtr<FJsonValue>& StepValue : StepRefs)
			{
				const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
				const FString ToolName = CppEmitterGetStringField(StepObject, TEXT("tool"));
				if (!ToolName.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive) || !VisibleCoreToolNames.Contains(ToolName))
				{
					continue;
				}

				FCppEmitterStep Step;
				Step.ToolName = ToolName;
				Step.CaptureRef = CppEmitterGetStringField(StepObject, TEXT("captureRef"));
				Step.CaptureStatus = CppEmitterGetStringField(StepObject, TEXT("captureStatus"));
				Step.Args = CppEmitterReadArgsTemplate(Step.CaptureRef, Step.CaptureStatus);
				Steps.Add(MoveTemp(Step));
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
					FCppEmitterStep Step;
					Step.ToolName = ToolName;
					Step.Args = MakeShared<FJsonObject>();
					Steps.Add(MoveTemp(Step));
				}
			}
			return Steps;
		}

		FString CppEmitterSchemaHash(const TArray<FOfficialCppToolsetToolInfo>& Tools)
		{
			TArray<FString> Lines;
			for (const FOfficialCppToolsetToolInfo& Tool : Tools)
			{
				Lines.Add(FString::Printf(
					TEXT("%s|%s|FString|%s"),
					*Tool.FunctionName,
					*Tool.GovernedToolId,
					*UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(Tool.ArgsJson)));
			}
			return UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(FString::Join(Lines, TEXT("\n")));
		}

		void CppEmitterAddFile(
			FOfficialCppToolsetBuildProduct& Product,
			const FString& RelativePath,
			const FString& Contents,
			bool bSourceFile)
		{
			FOfficialCppToolsetFile File;
			File.RelativePath = RelativePath;
			File.Contents = Contents;
			File.Sha256 = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(Contents);
			File.bSourceFile = bSourceFile;
			Product.Files.Add(MoveTemp(File));
		}

		TArray<TSharedPtr<FJsonValue>> CppEmitterMakeTaskIdArray(const FString& TaskId)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			if (!TaskId.TrimStartAndEnd().IsEmpty())
			{
				Values.Add(MakeShared<FJsonValueString>(TaskId.TrimStartAndEnd()));
			}
			return Values;
		}

		FString CppEmitterBuildManifest(FOfficialCppToolsetBuildProduct& Product, const FString& TaskId)
		{
			TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
			Manifest->SetStringField(TEXT("version"), TEXT("0.1"));
			Manifest->SetStringField(TEXT("variant"), TEXT("cpp"));
			Manifest->SetStringField(TEXT("toolId"), Product.ToolId);
			Manifest->SetStringField(TEXT("pluginName"), Product.PluginName);
			Manifest->SetStringField(TEXT("moduleName"), Product.ModuleName);
			Manifest->SetStringField(TEXT("className"), Product.ClassName);
			Manifest->SetStringField(TEXT("cppClassName"), Product.CppClassName);
			Manifest->SetStringField(TEXT("toolsetName"), Product.ToolsetName);
			Manifest->SetStringField(TEXT("pluginDescriptorPath"), Product.PluginDescriptorRelativePath);
			Manifest->SetStringField(TEXT("schemaHash"), Product.SchemaHash);
			Manifest->SetBoolField(TEXT("buildRequired"), true);
			Manifest->SetBoolField(TEXT("restartRequired"), true);
			Manifest->SetArrayField(TEXT("sourceTaskAtlasTaskIds"), CppEmitterMakeTaskIdArray(TaskId));

			TArray<TSharedPtr<FJsonValue>> ToolValues;
			for (const FOfficialCppToolsetToolInfo& Tool : Product.Tools)
			{
				TSharedPtr<FJsonObject> ToolObject = MakeShared<FJsonObject>();
				ToolObject->SetStringField(TEXT("name"), Tool.FunctionName);
				ToolObject->SetStringField(TEXT("returnType"), TEXT("FString"));
				ToolObject->SetStringField(TEXT("governedToolId"), Tool.GovernedToolId);
				ToolObject->SetStringField(TEXT("argsJsonSha256"), UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(Tool.ArgsJson));
				ToolValues.Add(MakeShared<FJsonValueObject>(ToolObject));
			}
			Manifest->SetArrayField(TEXT("toolNames"), ToolValues);

			TArray<TSharedPtr<FJsonValue>> FileValues;
			for (const FOfficialCppToolsetFile& File : Product.Files)
			{
				TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
				FileObject->SetStringField(TEXT("relativePath"), File.RelativePath);
				FileObject->SetStringField(TEXT("sha256"), File.Sha256);
				FileObject->SetBoolField(TEXT("sourceFile"), File.bSourceFile);
				FileValues.Add(MakeShared<FJsonValueObject>(FileObject));
			}
			Manifest->SetArrayField(TEXT("files"), FileValues);

			TSharedPtr<FJsonObject> ValidatorStatus = MakeShared<FJsonObject>();
			ValidatorStatus->SetBoolField(TEXT("passed"), false);
			ValidatorStatus->SetArrayField(TEXT("issues"), TArray<TSharedPtr<FJsonValue>>());
			Manifest->SetObjectField(TEXT("validatorStatus"), ValidatorStatus);

			TSharedPtr<FJsonObject> RegistrationStatus = MakeShared<FJsonObject>();
			RegistrationStatus->SetStringField(TEXT("state"), TEXT("requires_build_restart"));
			RegistrationStatus->SetStringField(TEXT("lastError"), FString());
			Manifest->SetObjectField(TEXT("registrationStatus"), RegistrationStatus);
			return CppEmitterJsonToString(Manifest, true);
		}

		bool CppEmitterUpdateValidatorStatus(
			const FString& ManifestJson,
			bool bPassed,
			const TArray<FString>& Issues,
			FString& OutManifestJson)
		{
			TSharedPtr<FJsonObject> Manifest;
			if (!CppEmitterParseJsonObject(ManifestJson, Manifest))
			{
				return false;
			}
			TArray<TSharedPtr<FJsonValue>> IssueValues;
			for (const FString& Issue : Issues)
			{
				IssueValues.Add(MakeShared<FJsonValueString>(Issue));
			}
			TSharedPtr<FJsonObject> ValidatorStatus = MakeShared<FJsonObject>();
			ValidatorStatus->SetBoolField(TEXT("passed"), bPassed);
			ValidatorStatus->SetArrayField(TEXT("issues"), IssueValues);
			Manifest->SetObjectField(TEXT("validatorStatus"), ValidatorStatus);
			OutManifestJson = CppEmitterJsonToString(Manifest, true);
			return true;
		}

		FString CppEmitterBuildPluginDescriptor(
			const FOfficialCppToolsetBuildProduct& Product,
			const FString& Title,
			const FString& Description,
			const FString& TaskId)
		{
			TSharedPtr<FJsonObject> Descriptor = MakeShared<FJsonObject>();
			Descriptor->SetNumberField(TEXT("FileVersion"), 3);
			Descriptor->SetNumberField(TEXT("Version"), 1);
			Descriptor->SetStringField(TEXT("VersionName"), TEXT("0.0.0-cpp-draft"));
			Descriptor->SetStringField(TEXT("FriendlyName"), Title.TrimStartAndEnd().IsEmpty() ? Product.PluginName : Title.TrimStartAndEnd());
			Descriptor->SetStringField(
				TEXT("Description"),
				Description.TrimStartAndEnd().IsEmpty()
					? FString::Printf(TEXT("UEAtelier C++ official toolset draft generated from Task Atlas task %s."), *TaskId.TrimStartAndEnd())
					: Description.TrimStartAndEnd());
			Descriptor->SetStringField(TEXT("Category"), TEXT("AI"));
			Descriptor->SetStringField(TEXT("CreatedBy"), TEXT("UEAtelier Task Atlas"));
			Descriptor->SetBoolField(TEXT("CanContainContent"), false);
			Descriptor->SetBoolField(TEXT("IsBetaVersion"), true);
			Descriptor->SetBoolField(TEXT("Installed"), false);

			TSharedPtr<FJsonObject> Module = MakeShared<FJsonObject>();
			Module->SetStringField(TEXT("Name"), Product.ModuleName);
			Module->SetStringField(TEXT("Type"), TEXT("Editor"));
			Module->SetStringField(TEXT("LoadingPhase"), TEXT("PostEngineInit"));
			TArray<TSharedPtr<FJsonValue>> Modules;
			Modules.Add(MakeShared<FJsonValueObject>(Module));
			Descriptor->SetArrayField(TEXT("Modules"), Modules);

			TSharedPtr<FJsonObject> ToolsetRegistryPlugin = MakeShared<FJsonObject>();
			ToolsetRegistryPlugin->SetStringField(TEXT("Name"), TEXT("ToolsetRegistry"));
			ToolsetRegistryPlugin->SetBoolField(TEXT("Enabled"), true);
			TSharedPtr<FJsonObject> UnrealMcpPlugin = MakeShared<FJsonObject>();
			UnrealMcpPlugin->SetStringField(TEXT("Name"), TEXT("UnrealMcp"));
			UnrealMcpPlugin->SetBoolField(TEXT("Enabled"), true);
			TArray<TSharedPtr<FJsonValue>> Plugins;
			Plugins.Add(MakeShared<FJsonValueObject>(ToolsetRegistryPlugin));
			Plugins.Add(MakeShared<FJsonValueObject>(UnrealMcpPlugin));
			Descriptor->SetArrayField(TEXT("Plugins"), Plugins);
			return CppEmitterJsonToString(Descriptor, true);
		}

		FString CppEmitterBuildCs(const FString& ModuleName)
		{
			return FString::Printf(
				TEXT("using UnrealBuildTool;\n\n")
				TEXT("public class %s : ModuleRules\n")
				TEXT("{\n")
				TEXT("\tpublic %s(ReadOnlyTargetRules Target) : base(Target)\n")
				TEXT("\t{\n")
				TEXT("\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n")
				TEXT("\t\tIWYUSupport = IWYUSupport.Full;\n")
				TEXT("\t\tbUseUnity = false;\n\n")
				TEXT("\t\tPublicDependencyModuleNames.Add(\"Core\");\n\n")
				TEXT("\t\tPrivateDependencyModuleNames.AddRange(new string[]\n")
				TEXT("\t\t{\n")
				TEXT("\t\t\t\"CoreUObject\",\n")
				TEXT("\t\t\t\"Engine\",\n")
				TEXT("\t\t\t\"ToolsetRegistry\",\n")
				TEXT("\t\t\t\"UnrealMcp\",\n")
				TEXT("\t\t});\n")
				TEXT("\t}\n")
				TEXT("}\n"),
				*ModuleName,
				*ModuleName);
		}

		FString CppEmitterModuleCpp(const FOfficialCppToolsetBuildProduct& Product)
		{
			return FString::Printf(
				TEXT("#include \"Modules/ModuleInterface.h\"\n")
				TEXT("#include \"Modules/ModuleManager.h\"\n")
				TEXT("#include \"ToolsetRegistry/UToolsetRegistry.h\"\n")
				TEXT("#include \"%s.h\"\n\n")
				TEXT("class F%sModule : public IModuleInterface\n")
				TEXT("{\n")
				TEXT("public:\n")
				TEXT("\tvirtual void StartupModule() override\n")
				TEXT("\t{\n")
				TEXT("\t\tUToolsetRegistry::RegisterToolsetClass(%s::StaticClass());\n")
				TEXT("\t}\n\n")
				TEXT("\tvirtual void ShutdownModule() override\n")
				TEXT("\t{\n")
				TEXT("\t\tUToolsetRegistry::UnregisterToolsetClass(%s::StaticClass());\n")
				TEXT("\t}\n")
				TEXT("};\n\n")
				TEXT("IMPLEMENT_MODULE(F%sModule, %s);\n"),
				*Product.ClassName,
				*Product.ModuleName,
				*Product.CppClassName,
				*Product.CppClassName,
				*Product.ModuleName,
				*Product.ModuleName);
		}

		FString CppEmitterHeader(const FOfficialCppToolsetBuildProduct& Product)
		{
			FString Header = FString::Printf(
				TEXT("#pragma once\n\n")
				TEXT("#include \"CoreMinimal.h\"\n")
				TEXT("#include \"ToolsetRegistry/ToolsetDefinition.h\"\n\n")
				TEXT("#include \"%s.generated.h\"\n\n")
				TEXT("UCLASS()\n")
				TEXT("class %s %s : public UToolsetDefinition\n")
				TEXT("{\n")
				TEXT("\tGENERATED_BODY()\n\n")
				TEXT("public:\n"),
				*Product.ClassName,
				*CppEmitterApiMacro(Product.ModuleName),
				*Product.CppClassName);
			for (const FOfficialCppToolsetToolInfo& Tool : Product.Tools)
			{
				Header += FString::Printf(
					TEXT("\tUFUNCTION(meta = (AICallable), Category = \"UEAtelier Task Atlas\")\n")
					TEXT("\tstatic FString %s();\n\n"),
					*Tool.FunctionName);
			}
			Header += TEXT("};\n");
			return Header;
		}

		FString CppEmitterClassCpp(const FOfficialCppToolsetBuildProduct& Product)
		{
			FString Source = FString::Printf(
				TEXT("#include \"%s.h\"\n\n")
				TEXT("#include \"UnrealMcpCallToolLibrary.h\"\n\n"),
				*Product.ClassName);
			for (const FOfficialCppToolsetToolInfo& Tool : Product.Tools)
			{
				Source += FString::Printf(
					TEXT("FString %s::%s()\n")
					TEXT("{\n")
					TEXT("\tconst FString ArgsJson = %s;\n")
					TEXT("\treturn UUnrealMcpCallToolLibrary::CallTool(%s, ArgsJson);\n")
					TEXT("}\n\n"),
					*Product.CppClassName,
					*Tool.FunctionName,
					*CppEmitterTextLiteral(Tool.ArgsJson),
					*CppEmitterTextLiteral(Tool.GovernedToolId));
			}
			return Source;
		}

		FString CppEmitterStripCommentsAndStrings(const FString& Source)
		{
			FString Output;
			bool bLineComment = false;
			bool bBlockComment = false;
			bool bString = false;
			bool bChar = false;
			bool bEscaped = false;
			for (int32 Index = 0; Index < Source.Len(); ++Index)
			{
				const TCHAR C = Source[Index];
				const TCHAR Next = Index + 1 < Source.Len() ? Source[Index + 1] : TEXT('\0');
				if (bLineComment)
				{
					if (C == TEXT('\n'))
					{
						bLineComment = false;
						Output.AppendChar(C);
					}
					continue;
				}
				if (bBlockComment)
				{
					if (C == TEXT('*') && Next == TEXT('/'))
					{
						bBlockComment = false;
						++Index;
					}
					continue;
				}
				if (bString || bChar)
				{
					if (bEscaped)
					{
						bEscaped = false;
						continue;
					}
					if (C == TEXT('\\'))
					{
						bEscaped = true;
						continue;
					}
					if ((bString && C == TEXT('"')) || (bChar && C == TEXT('\'')))
					{
						bString = false;
						bChar = false;
					}
					continue;
				}
				if (C == TEXT('/') && Next == TEXT('/'))
				{
					bLineComment = true;
					++Index;
					continue;
				}
				if (C == TEXT('/') && Next == TEXT('*'))
				{
					bBlockComment = true;
					++Index;
					continue;
				}
				if (C == TEXT('"'))
				{
					bString = true;
					continue;
				}
				if (C == TEXT('\''))
				{
					bChar = true;
					continue;
				}
				Output.AppendChar(C);
			}
			return Output;
		}

		const FOfficialCppToolsetFile* CppEmitterFindFile(const FOfficialCppToolsetBuildProduct& Product, const FString& RelativePath)
		{
			for (const FOfficialCppToolsetFile& File : Product.Files)
			{
				if (File.RelativePath == RelativePath)
				{
					return &File;
				}
			}
			return nullptr;
		}

		int32 CppEmitterCountOccurrences(const FString& Source, const FString& Needle)
		{
			int32 Count = 0;
			int32 Offset = 0;
			while (true)
			{
				const int32 Found = Source.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, Offset);
				if (Found == INDEX_NONE)
				{
					return Count;
				}
				++Count;
				Offset = Found + Needle.Len();
			}
		}

		void CppEmitterValidateIncludes(
			const FOfficialCppToolsetBuildProduct& Product,
			const FOfficialCppToolsetFile& File,
			TArray<FString>& OutIssues)
		{
			TArray<FString> Lines;
			File.Contents.ParseIntoArrayLines(Lines, false);
			for (const FString& Line : Lines)
			{
				const FString Trimmed = Line.TrimStartAndEnd();
				if (!Trimmed.StartsWith(TEXT("#include "), ESearchCase::CaseSensitive))
				{
					continue;
				}
				FString IncludeName = Trimmed;
				IncludeName.RemoveFromStart(TEXT("#include "));
				IncludeName = IncludeName.TrimStartAndEnd();
				IncludeName.RemoveFromStart(TEXT("\""));
				IncludeName.RemoveFromEnd(TEXT("\""));

				const bool bAllowed =
					IncludeName == TEXT("CoreMinimal.h")
					|| IncludeName == TEXT("Modules/ModuleInterface.h")
					|| IncludeName == TEXT("Modules/ModuleManager.h")
					|| IncludeName == TEXT("ToolsetRegistry/ToolsetDefinition.h")
					|| IncludeName == TEXT("ToolsetRegistry/UToolsetRegistry.h")
					|| IncludeName == TEXT("UnrealMcpCallToolLibrary.h")
					|| IncludeName == Product.ClassName + TEXT(".h")
					|| IncludeName == Product.ClassName + TEXT(".generated.h");
				if (!bAllowed)
				{
					OutIssues.Add(FString::Printf(TEXT("Unexpected include in %s: %s"), *File.RelativePath, *IncludeName));
				}
			}
		}

		void CppEmitterValidateDeniedTokens(const FOfficialCppToolsetFile& File, TArray<FString>& OutIssues)
		{
			const FString Scannable = File.RelativePath.EndsWith(TEXT(".Build.cs"))
				? File.Contents
				: CppEmitterStripCommentsAndStrings(File.Contents);
			static const TCHAR* DeniedTokens[] = {
				TEXT("GEditor"),
				TEXT("EditorLevelLibrary"),
				TEXT("EditorAssetLibrary"),
				TEXT("AssetRegistry"),
				TEXT("AssetTools"),
				TEXT("IAssetTools"),
				TEXT("SpawnActor"),
				TEXT("UWorld"),
				TEXT("IFileManager"),
				TEXT("FFileHelper"),
				TEXT("FPlatformFileManager"),
				TEXT("SaveStringToFile"),
				TEXT("DeleteDirectory"),
				TEXT("Move("),
				TEXT("CopyFile"),
				TEXT("ExecProcess"),
				TEXT("CreateProc"),
				TEXT("IPythonScriptPlugin"),
				TEXT("ExecPython"),
				TEXT("PythonScriptPlugin"),
				TEXT("FSocket")
			};
			for (const TCHAR* Token : DeniedTokens)
			{
				if (Scannable.Contains(Token, ESearchCase::CaseSensitive))
				{
					OutIssues.Add(FString::Printf(TEXT("Forbidden generated C++ token in %s: %s"), *File.RelativePath, Token));
				}
			}
		}

		bool CppEmitterManifestFileMap(
			const TSharedPtr<FJsonObject>& Manifest,
			TMap<FString, FString>& OutHashes,
			TMap<FString, bool>& OutSourceFlags)
		{
			OutHashes.Reset();
			OutSourceFlags.Reset();
			const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;
			if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("files"), FileValues) || !FileValues)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& FileValue : *FileValues)
			{
				const TSharedPtr<FJsonObject> FileObject = FileValue.IsValid() && FileValue->Type == EJson::Object ? FileValue->AsObject() : nullptr;
				FString RelativePath;
				FString Sha;
				bool bSourceFile = false;
				if (!FileObject.IsValid()
					|| !FileObject->TryGetStringField(TEXT("relativePath"), RelativePath)
					|| !FileObject->TryGetStringField(TEXT("sha256"), Sha))
				{
					return false;
				}
				FileObject->TryGetBoolField(TEXT("sourceFile"), bSourceFile);
				OutHashes.Add(RelativePath, Sha);
				OutSourceFlags.Add(RelativePath, bSourceFile);
			}
			return true;
		}

		bool CppEmitterWriteFile(const FString& RootDir, const FOfficialCppToolsetFile& File)
		{
			const FString Path = CppEmitterNormalizeAbsolutePath(FPaths::Combine(RootDir, File.RelativePath));
			if (!CppEmitterPathEqualsOrChild(Path, RootDir))
			{
				return false;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			return FFileHelper::SaveStringToFile(File.Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}

	bool BuildOfficialCppToolsetFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FOfficialCppToolsetBuildProduct& OutProduct,
		FString& OutFailureReason)
	{
		(void)ReplayEligibility;
		(void)ReplayUnavailableReason;
		OutProduct = FOfficialCppToolsetBuildProduct();
		OutFailureReason.Reset();

		const FString TrimmedToolId = ToolId.TrimStartAndEnd();
		if (!CppEmitterIsValidToolId(TrimmedToolId, OutFailureReason))
		{
			return false;
		}

		const TSet<FString> EffectiveVisibleTools = VisibleCoreToolNames.Num() > 0 ? VisibleCoreToolNames : CppEmitterVisibleCoreToolNames();
		const TArray<FCppEmitterStep> Steps = CppEmitterCollectSteps(CriticalPath, StepRefs, EffectiveVisibleTools);
		if (Steps.Num() == 0)
		{
			OutFailureReason = TEXT("Official C++ Toolset generation requires at least one visible core unreal.* tool in the workflow critical path.");
			return false;
		}

		const FString NameStem = CppEmitterPascalIdentifier(TrimmedToolId, TEXT("Generated"));
		OutProduct.ToolId = TrimmedToolId;
		OutProduct.PluginName = FString(TEXT("UEAtelier")) + NameStem + TEXT("CppToolset");
		OutProduct.ModuleName = OutProduct.PluginName;
		OutProduct.ClassName = OutProduct.PluginName + TEXT("Definition");
		OutProduct.CppClassName = FString(TEXT("U")) + OutProduct.ClassName;
		OutProduct.ToolsetName = CppEmitterToolsetName(OutProduct.ToolId, OutProduct.ModuleName, OutProduct.ClassName);
		OutProduct.PluginDescriptorRelativePath = FPaths::Combine(TEXT("cpp"), OutProduct.PluginName, OutProduct.PluginName + TEXT(".uplugin"));

		TSet<FString> UsedFunctionNames;
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FCppEmitterStep& Step = Steps[Index];
			const FString FunctionBase = FString::Printf(TEXT("Step%d%s"), Index, *CppEmitterPascalIdentifier(Step.ToolName.RightChop(7), TEXT("Tool")));
			FString FunctionName = FunctionBase;
			int32 Suffix = 2;
			while (UsedFunctionNames.Contains(FunctionName))
			{
				FunctionName = FString::Printf(TEXT("%s%d"), *FunctionBase, Suffix++);
			}
			UsedFunctionNames.Add(FunctionName);

			FOfficialCppToolsetToolInfo ToolInfo;
			ToolInfo.FunctionName = FunctionName;
			ToolInfo.GovernedToolId = Step.ToolName;
			ToolInfo.ArgsJson = CppEmitterJsonToString(Step.Args.IsValid() ? Step.Args : MakeShared<FJsonObject>(), false);
			OutProduct.Tools.Add(MoveTemp(ToolInfo));
		}
		OutProduct.SchemaHash = CppEmitterSchemaHash(OutProduct.Tools);

		const FString SourceRoot = FPaths::Combine(TEXT("cpp"), OutProduct.PluginName, TEXT("Source"), OutProduct.ModuleName);
		CppEmitterAddFile(OutProduct, OutProduct.PluginDescriptorRelativePath, CppEmitterBuildPluginDescriptor(OutProduct, Title, Description, TaskId), false);
		CppEmitterAddFile(OutProduct, FPaths::Combine(SourceRoot, OutProduct.ModuleName + TEXT(".Build.cs")), CppEmitterBuildCs(OutProduct.ModuleName), true);
		CppEmitterAddFile(OutProduct, FPaths::Combine(SourceRoot, TEXT("Private"), OutProduct.ModuleName + TEXT("Module.cpp")), CppEmitterModuleCpp(OutProduct), true);
		CppEmitterAddFile(OutProduct, FPaths::Combine(SourceRoot, TEXT("Public"), OutProduct.ClassName + TEXT(".h")), CppEmitterHeader(OutProduct), true);
		CppEmitterAddFile(OutProduct, FPaths::Combine(SourceRoot, TEXT("Private"), OutProduct.ClassName + TEXT(".cpp")), CppEmitterClassCpp(OutProduct), true);
		OutProduct.ManifestJson = CppEmitterBuildManifest(OutProduct, TaskId);
		return true;
	}

	bool ValidateOfficialCppToolsetFiles(
		const FOfficialCppToolsetBuildProduct& Product,
		TArray<FString>& OutIssues)
	{
		OutIssues.Reset();
		if (Product.ToolId.IsEmpty() || Product.PluginName.IsEmpty() || Product.ModuleName.IsEmpty() || Product.ClassName.IsEmpty() || Product.CppClassName.IsEmpty())
		{
			OutIssues.Add(TEXT("Product identity fields are incomplete."));
			return false;
		}
		if (Product.SchemaHash.IsEmpty())
		{
			OutIssues.Add(TEXT("schemaHash is required."));
		}
		if (Product.Tools.Num() == 0)
		{
			OutIssues.Add(TEXT("At least one generated UFUNCTION is required."));
		}

		const FString SourceRoot = FPaths::Combine(TEXT("cpp"), Product.PluginName, TEXT("Source"), Product.ModuleName);
		TSet<FString> ExpectedPaths;
		ExpectedPaths.Add(Product.PluginDescriptorRelativePath);
		ExpectedPaths.Add(FPaths::Combine(SourceRoot, Product.ModuleName + TEXT(".Build.cs")));
		ExpectedPaths.Add(FPaths::Combine(SourceRoot, TEXT("Private"), Product.ModuleName + TEXT("Module.cpp")));
		ExpectedPaths.Add(FPaths::Combine(SourceRoot, TEXT("Public"), Product.ClassName + TEXT(".h")));
		ExpectedPaths.Add(FPaths::Combine(SourceRoot, TEXT("Private"), Product.ClassName + TEXT(".cpp")));

		TSet<FString> SeenPaths;
		for (const FOfficialCppToolsetFile& File : Product.Files)
		{
			if (!ExpectedPaths.Contains(File.RelativePath))
			{
				OutIssues.Add(FString::Printf(TEXT("Unexpected generated file: %s"), *File.RelativePath));
			}
			if (SeenPaths.Contains(File.RelativePath))
			{
				OutIssues.Add(FString::Printf(TEXT("Duplicate generated file: %s"), *File.RelativePath));
			}
			SeenPaths.Add(File.RelativePath);
			if (File.Sha256 != UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(File.Contents))
			{
				OutIssues.Add(FString::Printf(TEXT("File hash mismatch: %s"), *File.RelativePath));
			}
			CppEmitterValidateIncludes(Product, File, OutIssues);
			CppEmitterValidateDeniedTokens(File, OutIssues);
		}
		for (const FString& ExpectedPath : ExpectedPaths)
		{
			if (!SeenPaths.Contains(ExpectedPath))
			{
				OutIssues.Add(FString::Printf(TEXT("Missing generated file: %s"), *ExpectedPath));
			}
		}

		TSharedPtr<FJsonObject> Manifest;
		if (!CppEmitterParseJsonObject(Product.ManifestJson, Manifest))
		{
			OutIssues.Add(TEXT("Manifest JSON did not parse."));
		}
		else
		{
			FString ManifestVariant;
			FString ManifestSchemaHash;
			bool bBuildRequired = false;
			bool bRestartRequired = false;
			Manifest->TryGetStringField(TEXT("variant"), ManifestVariant);
			Manifest->TryGetStringField(TEXT("schemaHash"), ManifestSchemaHash);
			Manifest->TryGetBoolField(TEXT("buildRequired"), bBuildRequired);
			Manifest->TryGetBoolField(TEXT("restartRequired"), bRestartRequired);
			if (ManifestVariant != TEXT("cpp"))
			{
				OutIssues.Add(TEXT("Manifest variant must be cpp."));
			}
			if (ManifestSchemaHash != Product.SchemaHash || ManifestSchemaHash != CppEmitterSchemaHash(Product.Tools))
			{
				OutIssues.Add(TEXT("Manifest schemaHash does not match generated tool schema."));
			}
			if (!bBuildRequired || !bRestartRequired)
			{
				OutIssues.Add(TEXT("Manifest must require build and restart."));
			}

			const TSharedPtr<FJsonObject>* RegistrationStatus = nullptr;
			FString RegistrationState;
			if (!Manifest->TryGetObjectField(TEXT("registrationStatus"), RegistrationStatus)
				|| !RegistrationStatus
				|| !(*RegistrationStatus).IsValid()
				|| !(*RegistrationStatus)->TryGetStringField(TEXT("state"), RegistrationState)
				|| RegistrationState != TEXT("requires_build_restart"))
			{
				OutIssues.Add(TEXT("Manifest registrationStatus.state must be requires_build_restart."));
			}

			TMap<FString, FString> ManifestHashes;
			TMap<FString, bool> ManifestSourceFlags;
			if (!CppEmitterManifestFileMap(Manifest, ManifestHashes, ManifestSourceFlags))
			{
				OutIssues.Add(TEXT("Manifest files array is missing or invalid."));
			}
			else
			{
				if (ManifestHashes.Num() != Product.Files.Num())
				{
					OutIssues.Add(TEXT("Manifest file count does not match generated file count."));
				}
				for (const FOfficialCppToolsetFile& File : Product.Files)
				{
					const FString* ManifestSha = ManifestHashes.Find(File.RelativePath);
					const bool* ManifestSourceFlag = ManifestSourceFlags.Find(File.RelativePath);
					if (!ManifestSha || *ManifestSha != File.Sha256)
					{
						OutIssues.Add(FString::Printf(TEXT("Manifest hash mismatch for %s."), *File.RelativePath));
					}
					if (!ManifestSourceFlag || *ManifestSourceFlag != File.bSourceFile)
					{
						OutIssues.Add(FString::Printf(TEXT("Manifest source flag mismatch for %s."), *File.RelativePath));
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ManifestTools = nullptr;
			if (!Manifest->TryGetArrayField(TEXT("toolNames"), ManifestTools) || !ManifestTools || ManifestTools->Num() != Product.Tools.Num())
			{
				OutIssues.Add(TEXT("Manifest toolNames count does not match generated UFUNCTION count."));
			}
		}

		const FOfficialCppToolsetFile* Descriptor = CppEmitterFindFile(Product, Product.PluginDescriptorRelativePath);
		if (!Descriptor)
		{
			OutIssues.Add(TEXT("Plugin descriptor is missing."));
		}
		else
		{
			TSharedPtr<FJsonObject> DescriptorJson;
			if (!CppEmitterParseJsonObject(Descriptor->Contents, DescriptorJson))
			{
				OutIssues.Add(TEXT("Plugin descriptor JSON did not parse."));
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Plugins = nullptr;
				if (!DescriptorJson->TryGetArrayField(TEXT("Modules"), Modules) || !Modules || Modules->Num() != 1)
				{
					OutIssues.Add(TEXT("Plugin descriptor must contain exactly one module."));
				}
				else
				{
					const TSharedPtr<FJsonObject> ModuleObject = (*Modules)[0].IsValid() && (*Modules)[0]->Type == EJson::Object ? (*Modules)[0]->AsObject() : nullptr;
					FString ModuleName;
					FString ModuleType;
					if (!ModuleObject.IsValid()
						|| !ModuleObject->TryGetStringField(TEXT("Name"), ModuleName)
						|| !ModuleObject->TryGetStringField(TEXT("Type"), ModuleType)
						|| ModuleName != Product.ModuleName
						|| ModuleType != TEXT("Editor"))
					{
						OutIssues.Add(TEXT("Plugin descriptor module does not match the generated Editor module."));
					}
				}
				if (!DescriptorJson->TryGetArrayField(TEXT("Plugins"), Plugins) || !Plugins || Plugins->Num() != 2)
				{
					OutIssues.Add(TEXT("Plugin descriptor must contain ToolsetRegistry and UnrealMcp dependencies only."));
				}
				else
				{
					TSet<FString> PluginNames;
					for (const TSharedPtr<FJsonValue>& PluginValue : *Plugins)
					{
						const TSharedPtr<FJsonObject> PluginObject = PluginValue.IsValid() && PluginValue->Type == EJson::Object ? PluginValue->AsObject() : nullptr;
						FString Name;
						if (PluginObject.IsValid())
						{
							PluginObject->TryGetStringField(TEXT("Name"), Name);
						}
						PluginNames.Add(Name);
					}
					if (!PluginNames.Contains(TEXT("ToolsetRegistry")) || !PluginNames.Contains(TEXT("UnrealMcp")))
					{
						OutIssues.Add(TEXT("Plugin descriptor is missing required plugin dependencies."));
					}
				}
			}
		}

		const FOfficialCppToolsetFile* BuildCs = CppEmitterFindFile(Product, FPaths::Combine(SourceRoot, Product.ModuleName + TEXT(".Build.cs")));
		const FOfficialCppToolsetFile* ModuleCpp = CppEmitterFindFile(Product, FPaths::Combine(SourceRoot, TEXT("Private"), Product.ModuleName + TEXT("Module.cpp")));
		const FOfficialCppToolsetFile* Header = CppEmitterFindFile(Product, FPaths::Combine(SourceRoot, TEXT("Public"), Product.ClassName + TEXT(".h")));
		const FOfficialCppToolsetFile* ClassCpp = CppEmitterFindFile(Product, FPaths::Combine(SourceRoot, TEXT("Private"), Product.ClassName + TEXT(".cpp")));
		if (BuildCs)
		{
			if (!BuildCs->Contents.Contains(FString(TEXT("public class ")) + Product.ModuleName + TEXT(" : ModuleRules"))
				|| !BuildCs->Contents.Contains(FString(TEXT("public ")) + Product.ModuleName + TEXT("(ReadOnlyTargetRules Target) : base(Target)")))
			{
				OutIssues.Add(TEXT("Build.cs class and constructor do not match the generated module name."));
			}
			TArray<FString> RequiredDependencies;
			RequiredDependencies.Add(TEXT("\"CoreUObject\""));
			RequiredDependencies.Add(TEXT("\"Engine\""));
			RequiredDependencies.Add(TEXT("\"ToolsetRegistry\""));
			RequiredDependencies.Add(TEXT("\"UnrealMcp\""));
			for (const FString& RequiredDependency : RequiredDependencies)
			{
				if (!BuildCs->Contents.Contains(RequiredDependency, ESearchCase::CaseSensitive))
				{
					OutIssues.Add(FString::Printf(TEXT("Build.cs missing dependency %s."), *RequiredDependency));
				}
			}
		}
		if (ModuleCpp)
		{
			if (!ModuleCpp->Contents.Contains(FString(TEXT("UToolsetRegistry::RegisterToolsetClass(")) + Product.CppClassName + TEXT("::StaticClass())"))
				|| !ModuleCpp->Contents.Contains(FString(TEXT("UToolsetRegistry::UnregisterToolsetClass(")) + Product.CppClassName + TEXT("::StaticClass())"))
				|| !ModuleCpp->Contents.Contains(FString(TEXT("IMPLEMENT_MODULE(F")) + Product.ModuleName + TEXT("Module, ") + Product.ModuleName + TEXT(")")))
			{
				OutIssues.Add(TEXT("Module cpp does not register/unregister the generated toolset class consistently."));
			}
		}
		if (Header)
		{
			if (!Header->Contents.Contains(FString(TEXT("class ")) + CppEmitterApiMacro(Product.ModuleName) + TEXT(" ") + Product.CppClassName + TEXT(" : public UToolsetDefinition")))
			{
				OutIssues.Add(TEXT("Header does not declare the expected UToolsetDefinition subclass."));
			}
			if (CppEmitterCountOccurrences(Header->Contents, TEXT("UFUNCTION(")) != Product.Tools.Num())
			{
				OutIssues.Add(TEXT("Header UFUNCTION count does not match manifest/tool list."));
			}
		}
		if (ClassCpp)
		{
			if (!ClassCpp->Contents.Contains(TEXT("#include \"UnrealMcpCallToolLibrary.h\"")))
			{
				OutIssues.Add(TEXT("Class cpp must include UnrealMcpCallToolLibrary.h."));
			}
			for (const FOfficialCppToolsetToolInfo& Tool : Product.Tools)
			{
				if (!ClassCpp->Contents.Contains(FString(TEXT("FString ")) + Product.CppClassName + TEXT("::") + Tool.FunctionName + TEXT("()"))
					|| !ClassCpp->Contents.Contains(FString(TEXT("UUnrealMcpCallToolLibrary::CallTool(")) + CppEmitterTextLiteral(Tool.GovernedToolId)))
				{
					OutIssues.Add(FString::Printf(TEXT("Generated function %s does not delegate through UUnrealMcpCallToolLibrary::CallTool."), *Tool.FunctionName));
				}
			}
		}
		return OutIssues.Num() == 0;
	}

	FOfficialCppToolsetDraftResult GenerateOfficialCppToolsetDraft(const FOfficialCppToolsetDraftRequest& Req)
	{
		FOfficialCppToolsetDraftResult Result;
		const FString ToolId = Req.ToolId.TrimStartAndEnd().IsEmpty()
			? UnrealMcp::TaskAtlasService::MakeAtlasToolId(Req.Title, Req.TaskId)
			: Req.ToolId.TrimStartAndEnd();

		FScopeLock MutationGuard(&GOfficialCppDraftMutationLock);
		const FString DraftRoot = CppEmitterNormalizeAbsolutePath(UnrealMcp::TaskAtlasService::OfficialToolsetDraftsRootDir());
		const FString TargetDir = CppEmitterNormalizeAbsolutePath(FPaths::Combine(DraftRoot, ToolId));
		if (!CppEmitterIsSafeDirectoryName(ToolId) || !CppEmitterPathEqualsOrChild(TargetDir, DraftRoot))
		{
			Result.ErrorCode = TEXT("invalid_name");
			Result.ErrorMessage = TEXT("Refused to write official C++ toolset draft outside Saved/UnrealMcp/OfficialToolsetDrafts.");
			return Result;
		}
		if (IFileManager::Get().DirectoryExists(*TargetDir))
		{
			Result.ErrorCode = TEXT("collision_existing_target");
			Result.ErrorMessage = FString::Printf(TEXT("Official C++ toolset draft target already exists: %s"), *TargetDir);
			return Result;
		}

		FOfficialCppToolsetBuildProduct Product;
		FString FailureReason;
		const TSet<FString> VisibleCoreToolNames = Req.VisibleCoreToolNames.Num() > 0 ? Req.VisibleCoreToolNames : CppEmitterVisibleCoreToolNames();
		if (!BuildOfficialCppToolsetFiles(
			ToolId,
			Req.Title,
			Req.Description,
			Req.TaskId,
			Req.ReplayEligibility,
			Req.ReplayUnavailableReason,
			Req.CriticalPath,
			Req.StepRefs,
			VisibleCoreToolNames,
			Product,
			FailureReason))
		{
			Result.ErrorCode = TEXT("official_cpp_build_failed");
			Result.ErrorMessage = FailureReason;
			return Result;
		}

		Result.ModuleName = Product.ModuleName;
		Result.ClassName = Product.ClassName;
		Result.ToolsetName = Product.ToolsetName;
		Result.SchemaHash = Product.SchemaHash;

		const FString StagingDir = CppEmitterNormalizeAbsolutePath(FPaths::Combine(
			DraftRoot,
			FString::Printf(TEXT("__staging_cpp_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short))));
		Result.StagingDir = StagingDir;
		if (!IFileManager::Get().MakeDirectory(*StagingDir, true))
		{
			Result.ErrorCode = TEXT("staging_write_failed");
			Result.ErrorMessage = TEXT("Failed to create staged official C++ toolset directory.");
			return Result;
		}

		for (const FOfficialCppToolsetFile& File : Product.Files)
		{
			if (!CppEmitterWriteFile(StagingDir, File))
			{
				IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
				Result.ErrorCode = TEXT("staging_write_failed");
				Result.ErrorMessage = FString::Printf(TEXT("Failed to write staged official C++ toolset file: %s"), *File.RelativePath);
				return Result;
			}
		}

		TArray<FString> ValidatorIssues;
		if (!ValidateOfficialCppToolsetFiles(Product, ValidatorIssues))
		{
			Result.ValidatorIssues = ValidatorIssues;
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("validator_rejected");
			Result.ErrorMessage = ValidatorIssues.Num() > 0 ? ValidatorIssues[0] : FString(TEXT("Official C++ toolset validator rejected the generated draft."));
			return Result;
		}

		FString FinalManifestJson;
		if (!CppEmitterUpdateValidatorStatus(Product.ManifestJson, true, ValidatorIssues, FinalManifestJson))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("manifest_parse_failed");
			Result.ErrorMessage = TEXT("Generated official C++ toolset manifest did not parse.");
			return Result;
		}
		const FString StagedManifestPath = FPaths::Combine(StagingDir, TEXT("manifest.json"));
		if (!FFileHelper::SaveStringToFile(FinalManifestJson, *StagedManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("manifest_write_failed");
			Result.ErrorMessage = TEXT("Failed to write staged official C++ toolset manifest.");
			return Result;
		}

		if (!IFileManager::Get().Move(*TargetDir, *StagingDir, false, true))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.ErrorCode = TEXT("staging_rename_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to rename official C++ staging directory '%s' to '%s'."), *StagingDir, *TargetDir);
			return Result;
		}

		Result.GeneratedDir = TargetDir;
		Result.ManifestPath = FPaths::Combine(TargetDir, TEXT("manifest.json"));
		Result.PluginDescriptorPath = FPaths::Combine(TargetDir, Product.PluginDescriptorRelativePath);
		Result.ManifestJson = FinalManifestJson;
		Result.StagingDir.Reset();
		for (const FOfficialCppToolsetFile& File : Product.Files)
		{
			if (File.bSourceFile)
			{
				Result.SourceFiles.Add(FPaths::Combine(TargetDir, File.RelativePath));
			}
		}
		Result.ValidatorIssues = ValidatorIssues;
		Result.bSucceeded = true;
		return Result;
	}

	FOfficialCppToolsetDriftResult DetectOfficialCppToolsetDraftDrift(const FString& GeneratedDir)
	{
		FOfficialCppToolsetDriftResult Result;
		const FString ManifestPath = FPaths::Combine(GeneratedDir, TEXT("manifest.json"));

		FString ManifestText;
		if (!FPaths::FileExists(ManifestPath) || !FFileHelper::LoadFileToString(ManifestText, *ManifestPath))
		{
			Result.Drifts.Add(TEXT("manifest_missing: manifest.json absent or unreadable"));
			return Result;
		}

		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestText);
		if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
		{
			Result.Drifts.Add(TEXT("manifest_unparsable: manifest.json is not valid JSON"));
			return Result;
		}

		FString SchemaHash;
		Manifest->TryGetStringField(TEXT("schemaHash"), SchemaHash);
		if (SchemaHash.TrimStartAndEnd().IsEmpty())
		{
			Result.Drifts.Add(TEXT("schema_hash_missing: manifest schemaHash is empty"));
		}

		const TArray<TSharedPtr<FJsonValue>>* FileValues = nullptr;
		if (!Manifest->TryGetArrayField(TEXT("files"), FileValues) || FileValues == nullptr || FileValues->Num() == 0)
		{
			Result.Drifts.Add(TEXT("manifest_files_missing: manifest files array absent or empty"));
			Result.bClean = Result.Drifts.Num() == 0;
			return Result;
		}

		for (const TSharedPtr<FJsonValue>& Value : *FileValues)
		{
			const TSharedPtr<FJsonObject>* FileObject = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(FileObject) || FileObject == nullptr || !FileObject->IsValid())
			{
				Result.Drifts.Add(TEXT("manifest_file_entry_malformed: non-object entry in files array"));
				continue;
			}

			FString RelativePath;
			FString ExpectedSha;
			(*FileObject)->TryGetStringField(TEXT("relativePath"), RelativePath);
			(*FileObject)->TryGetStringField(TEXT("sha256"), ExpectedSha);
			if (RelativePath.TrimStartAndEnd().IsEmpty() || ExpectedSha.TrimStartAndEnd().IsEmpty())
			{
				Result.Drifts.Add(TEXT("manifest_file_entry_malformed: entry missing relativePath or sha256"));
				continue;
			}

			const FString AbsolutePath = FPaths::Combine(GeneratedDir, RelativePath);
			FString LiveContents;
			if (!FPaths::FileExists(AbsolutePath) || !FFileHelper::LoadFileToString(LiveContents, *AbsolutePath))
			{
				Result.Drifts.Add(FString::Printf(TEXT("file_missing: %s"), *RelativePath));
				continue;
			}

			const FString LiveSha = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(LiveContents);
			if (!LiveSha.Equals(ExpectedSha.TrimStartAndEnd().ToLower(), ESearchCase::CaseSensitive))
			{
				Result.Drifts.Add(FString::Printf(TEXT("file_hash_drift: %s"), *RelativePath));
			}
		}

		Result.bClean = Result.Drifts.Num() == 0;
		return Result;
	}
}

#endif
