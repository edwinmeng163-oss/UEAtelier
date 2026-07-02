#include "UnrealMcpVettedToolset.h"

#include "UnrealMcpHashUtils.h"

namespace UnrealMcp
{
	namespace
	{
		static thread_local TArray<FString> GVettedToolsetToolIdStack;
		static thread_local TArray<FString> GVettedToolsetShaStack;

		const TCHAR* GCompositeViolationForbiddenImport = TEXT("forbidden_import");
		const TCHAR* GCompositeViolationDunderToken = TEXT("dunder_token");
		const TCHAR* GCompositeViolationDynamicAccess = TEXT("dynamic_access");
		const TCHAR* GCompositeViolationForbiddenCall = TEXT("forbidden_call");
		const TCHAR* GCompositeViolationUnrealDirect = TEXT("unreal_direct");
		const TCHAR* GCompositeViolationFileIo = TEXT("file_io");
		const TCHAR* GCompositeViolationEmptySource = TEXT("empty_source");
		const TCHAR* GCompositeViolationAllowlistNotVettable = TEXT("allowlist_not_vettable");
		const TCHAR* GCompositeViolationMalformedSource = TEXT("malformed_source");

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

		bool CompositeIsIdentifierChar(TCHAR Ch)
		{
			return FChar::IsAlnum(Ch) || Ch == TEXT('_');
		}

		bool CompositeIsAllowedImportRoot(const FString& Root)
		{
			return Root == TEXT("json") || Root == TEXT("math") || Root == TEXT("re") || Root == TEXT("datetime") || Root == TEXT("uuid");
		}

		FString CompositeReadImportRoot(const FString& ModuleName)
		{
			const FString Trimmed = ModuleName.TrimStartAndEnd();
			int32 End = 0;
			while (End < Trimmed.Len() && (CompositeIsIdentifierChar(Trimmed[End]) || Trimmed[End] == TEXT('.')))
			{
				++End;
			}
			FString Root = Trimmed.Left(End);
			int32 DotIndex = INDEX_NONE;
			if (Root.FindChar(TEXT('.'), DotIndex))
			{
				Root.LeftInline(DotIndex, EAllowShrinking::No);
			}
			return Root;
		}

		void CompositeAddViolation(FCompositeSourcePolicyResult& Result, const TCHAR* Code, const FString& Detail)
		{
			const FString Violation = FString::Printf(TEXT("%s: %s"), Code, *Detail);
			if (!Result.Violations.Contains(Violation))
			{
				Result.Violations.Add(Violation);
			}
		}

		FString CompositeJoinBackslashContinuations(const FString& Source)
		{
			FString Joined = Source;
			Joined.ReplaceInline(TEXT("\\\r\n"), TEXT(""));
			Joined.ReplaceInline(TEXT("\\\n"), TEXT(""));
			Joined.ReplaceInline(TEXT("\\\r"), TEXT(""));
			return Joined;
		}

		FString CompositeStripCommentsAndTripleBlocks(const FString& Source, bool bBlankStringContents, bool& bMalformed)
		{
			FString Stripped;
			Stripped.Reserve(Source.Len());
			bool bInSingle = false;
			bool bInDouble = false;
			bool bInTripleSingle = false;
			bool bInTripleDouble = false;
			bool bEscaped = false;

			for (int32 Index = 0; Index < Source.Len(); ++Index)
			{
				const TCHAR Ch = Source[Index];
				if (bInTripleSingle || bInTripleDouble)
				{
					const FString Triplet = Source.Mid(Index, 3);
					if ((bInTripleSingle && Triplet == TEXT("'''")) || (bInTripleDouble && Triplet == TEXT("\"\"\"")))
					{
						Index += 2;
						bInTripleSingle = false;
						bInTripleDouble = false;
						continue;
					}
					if (Ch == TEXT('\n') || Ch == TEXT('\r'))
					{
						Stripped.AppendChar(Ch);
					}
					continue;
				}

				const FString Triplet = Source.Mid(Index, 3);
				if (!bInSingle && !bInDouble && Triplet == TEXT("'''"))
				{
					bInTripleSingle = true;
					Index += 2;
					continue;
				}
				if (!bInSingle && !bInDouble && Triplet == TEXT("\"\"\""))
				{
					bInTripleDouble = true;
					Index += 2;
					continue;
				}

				if (!bInSingle && !bInDouble && Ch == TEXT('#'))
				{
					while (Index < Source.Len() && Source[Index] != TEXT('\n') && Source[Index] != TEXT('\r'))
					{
						++Index;
					}
					if (Index < Source.Len())
					{
						Stripped.AppendChar(Source[Index]);
					}
					continue;
				}

				const bool bInsideString = bInSingle || bInDouble;
				Stripped.AppendChar(bBlankStringContents && bInsideString && Ch != TEXT('\'') && Ch != TEXT('"') ? TEXT(' ') : Ch);
				if (bEscaped)
				{
					bEscaped = false;
					continue;
				}
				if ((bInSingle || bInDouble) && Ch == TEXT('\\'))
				{
					bEscaped = true;
					continue;
				}
				if (!bInDouble && Ch == TEXT('\''))
				{
					bInSingle = !bInSingle;
				}
				else if (!bInSingle && Ch == TEXT('"'))
				{
					bInDouble = !bInDouble;
				}
			}

			bMalformed = bInSingle || bInDouble || bInTripleSingle || bInTripleDouble;
			return Stripped;
		}

		bool CompositeIdentifierAt(const FString& Source, int32 Index, const FString& Identifier)
		{
			if (Index < 0 || Index + Identifier.Len() > Source.Len() || Source.Mid(Index, Identifier.Len()) != Identifier)
			{
				return false;
			}
			const int32 Before = Index - 1;
			const int32 After = Index + Identifier.Len();
			return (Before < 0 || !CompositeIsIdentifierChar(Source[Before]))
				&& (After >= Source.Len() || !CompositeIsIdentifierChar(Source[After]));
		}

		int32 CompositeFindIdentifier(const FString& Source, const FString& Identifier, int32 StartIndex = 0)
		{
			int32 Index = Source.Find(Identifier, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
			while (Index != INDEX_NONE)
			{
				if (CompositeIdentifierAt(Source, Index, Identifier))
				{
					return Index;
				}
				Index = Source.Find(Identifier, ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + Identifier.Len());
			}
			return INDEX_NONE;
		}

		int32 CompositeNextNonWhitespaceIndex(const FString& Source, int32 Index)
		{
			while (Index < Source.Len() && FChar::IsWhitespace(Source[Index]))
			{
				++Index;
			}
			return Index;
		}

		bool CompositeIdentifierFollowedBy(const FString& Source, const FString& Identifier, TCHAR Expected)
		{
			int32 Index = CompositeFindIdentifier(Source, Identifier);
			while (Index != INDEX_NONE)
			{
				const int32 NextIndex = CompositeNextNonWhitespaceIndex(Source, Index + Identifier.Len());
				if (NextIndex < Source.Len() && Source[NextIndex] == Expected)
				{
					return true;
				}
				Index = CompositeFindIdentifier(Source, Identifier, Index + Identifier.Len());
			}
			return false;
		}

		const TArray<FString>& CompositeTemplateDunderExceptions()
		{
			// STaskAtlasWindow.cpp:406-498 emits the canonical composite main.py
			// template. It currently emits no dunder tokens, so the exception set is empty.
			static const TArray<FString> Tokens;
			return Tokens;
		}

		bool CompositeIsDunderBodyChar(TCHAR Ch)
		{
			return FChar::IsAlnum(Ch) || Ch == TEXT('_');
		}

		void CompositeCheckImportRoot(FCompositeSourcePolicyResult& Result, const FString& Root, const FString& Detail)
		{
			if (Root.IsEmpty() || !CompositeIsAllowedImportRoot(Root))
			{
				CompositeAddViolation(Result, GCompositeViolationForbiddenImport, FString::Printf(TEXT("%s '%s'"), *Detail, *Root));
			}
		}

		void CompositeScanImports(const FString& Source, FCompositeSourcePolicyResult& Result)
		{
			TArray<FString> Lines;
			Source.ParseIntoArrayLines(Lines, false);
			for (const FString& Line : Lines)
			{
				TArray<FString> Statements;
				Line.ParseIntoArray(Statements, TEXT(";"), false);
				for (FString Statement : Statements)
				{
					Statement = Statement.TrimStartAndEnd();
					int32 InlineColon = INDEX_NONE;
					if (!Statement.StartsWith(TEXT("import ")) && !Statement.StartsWith(TEXT("from ")) && Statement.FindChar(TEXT(':'), InlineColon))
					{
						Statement = Statement.Mid(InlineColon + 1).TrimStartAndEnd();
					}
					if (Statement.StartsWith(TEXT("from ")))
					{
						CompositeCheckImportRoot(Result, CompositeReadImportRoot(Statement.Mid(5)), TEXT("from import root"));
					}
					else if (Statement.StartsWith(TEXT("import ")))
					{
						TArray<FString> Parts;
						Statement.Mid(7).ParseIntoArray(Parts, TEXT(","), false);
						for (const FString& Part : Parts)
						{
							CompositeCheckImportRoot(Result, CompositeReadImportRoot(Part), TEXT("import root"));
						}
					}
				}
			}
		}

		void CompositeScanDunders(const FString& Source, FCompositeSourcePolicyResult& Result)
		{
			for (int32 Index = 0; Index + 1 < Source.Len(); ++Index)
			{
				if (Source[Index] != TEXT('_') || Source[Index + 1] != TEXT('_'))
				{
					continue;
				}
				int32 End = Source.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index + 2);
				while (End != INDEX_NONE)
				{
					bool bValidBody = End > Index + 2;
					for (int32 BodyIndex = Index + 2; bValidBody && BodyIndex < End; ++BodyIndex)
					{
						bValidBody = CompositeIsDunderBodyChar(Source[BodyIndex]);
					}
					if (bValidBody)
					{
						const FString Token = Source.Mid(Index, End + 2 - Index);
						if (!CompositeTemplateDunderExceptions().Contains(Token))
						{
							CompositeAddViolation(Result, GCompositeViolationDunderToken, Token);
						}
						Index = End + 1;
						break;
					}
					End = Source.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromStart, End + 2);
				}
			}
		}

		void CompositeScanForbiddenTokens(const FString& Source, FCompositeSourcePolicyResult& Result)
		{
			const TCHAR* DynamicCalls[] = { TEXT("globals"), TEXT("locals"), TEXT("vars"), TEXT("getattr"), TEXT("setattr"), TEXT("delattr") };
			for (const TCHAR* Name : DynamicCalls)
			{
				if (CompositeIdentifierFollowedBy(Source, Name, TEXT('(')))
				{
					CompositeAddViolation(Result, GCompositeViolationDynamicAccess, FString::Printf(TEXT("%s call"), Name));
				}
			}
			const TCHAR* ForbiddenCalls[] = { TEXT("eval"), TEXT("exec"), TEXT("compile"), TEXT("chr") };
			for (const TCHAR* Name : ForbiddenCalls)
			{
				if (CompositeIdentifierFollowedBy(Source, Name, TEXT('(')))
				{
					CompositeAddViolation(Result, GCompositeViolationForbiddenCall, FString::Printf(TEXT("%s call"), Name));
				}
			}
			if (CompositeIdentifierFollowedBy(Source, TEXT("open"), TEXT('(')))
			{
				CompositeAddViolation(Result, GCompositeViolationFileIo, TEXT("open call"));
			}
			if (CompositeFindIdentifier(Source, TEXT("importlib")) != INDEX_NONE)
			{
				CompositeAddViolation(Result, GCompositeViolationDynamicAccess, TEXT("importlib usage"));
			}
			if (CompositeFindIdentifier(Source, TEXT("subprocess")) != INDEX_NONE)
			{
				CompositeAddViolation(Result, GCompositeViolationForbiddenCall, TEXT("subprocess usage"));
			}
			if (CompositeIdentifierFollowedBy(Source, TEXT("os"), TEXT('.')) || CompositeIdentifierFollowedBy(Source, TEXT("sys"), TEXT('.')))
			{
				CompositeAddViolation(Result, GCompositeViolationForbiddenCall, TEXT("os/sys module usage"));
			}
			if (CompositeIdentifierFollowedBy(Source, TEXT("unreal"), TEXT('.')))
			{
				CompositeAddViolation(Result, GCompositeViolationUnrealDirect, TEXT("unreal module attribute access"));
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
		return VerifyVettedToolsetSha_Pure(Marker, HashUtils::Sha256LowerHexFromUtf8(LiveMainPyUtf8));
	}

	FVettedToolsetVerificationResult VerifyVettedToolsetSha_Pure(
		const FVettedToolsetMarker& Marker,
		const FString& LiveMainPyShaLowerHex)
	{
		if (!Marker.bVetted)
		{
			return { false, TEXT("not_marked") };
		}

		if (Marker.MainPySha256.IsEmpty())
		{
			return { false, TEXT("hash_missing") };
		}

		if (!Marker.MainPySha256.Equals(LiveMainPyShaLowerHex.TrimStartAndEnd().ToLower(), ESearchCase::CaseSensitive))
		{
			return { false, TEXT("hash_mismatch") };
		}

		return { true, TEXT("vetted") };
	}

	FCompositeSourcePolicyResult ValidateCompositeSourcePolicy_Pure(const FString& MainPyUtf8)
	{
		FCompositeSourcePolicyResult Result;
		const FString Joined = CompositeJoinBackslashContinuations(MainPyUtf8);
		bool bMalformed = false;
		const FString StrippedKeepStrings = CompositeStripCommentsAndTripleBlocks(Joined, false, bMalformed);
		bool bBlankMalformed = false;
		const FString StrippedBlankStrings = CompositeStripCommentsAndTripleBlocks(Joined, true, bBlankMalformed);
		bMalformed = bMalformed || bBlankMalformed;
		if (StrippedKeepStrings.TrimStartAndEnd().IsEmpty())
		{
			CompositeAddViolation(Result, GCompositeViolationEmptySource, TEXT("main.py has no executable source"));
		}
		if (bMalformed)
		{
			CompositeAddViolation(Result, GCompositeViolationMalformedSource, TEXT("unterminated quoted string"));
		}

		CompositeScanImports(StrippedBlankStrings, Result);
		CompositeScanDunders(StrippedKeepStrings, Result);
		CompositeScanForbiddenTokens(StrippedBlankStrings, Result);
		Result.bPassed = Result.Violations.Num() == 0;
		return Result;
	}

	FCompositeSourcePolicyResult IsImportAllowlistVettable_Pure(const TArray<FString>& ImportAllowlist)
	{
		FCompositeSourcePolicyResult Result;
		for (const FString& Entry : ImportAllowlist)
		{
			const FString Root = CompositeReadImportRoot(Entry);
			if (Root.IsEmpty() || !CompositeIsAllowedImportRoot(Root))
			{
				CompositeAddViolation(
					Result,
					GCompositeViolationAllowlistNotVettable,
					FString::Printf(TEXT("importAllowlist root '%s'"), *Root));
			}
		}
		Result.bPassed = Result.Violations.Num() == 0;
		return Result;
	}

	FVettedToolsetScope::FVettedToolsetScope(const FString& InToolId, const FString& InVerifiedSha)
		: ToolId(InToolId)
		, VerifiedSha(InVerifiedSha)
	{
		GVettedToolsetToolIdStack.Add(ToolId);
		GVettedToolsetShaStack.Add(VerifiedSha);
	}

	FVettedToolsetScope::~FVettedToolsetScope()
	{
		if (GVettedToolsetToolIdStack.Num() > 0)
		{
			GVettedToolsetToolIdStack.Pop(EAllowShrinking::No);
		}
		if (GVettedToolsetShaStack.Num() > 0)
		{
			GVettedToolsetShaStack.Pop(EAllowShrinking::No);
		}
	}

	bool FVettedToolsetScope::IsActive()
	{
		return GVettedToolsetToolIdStack.Num() > 0;
	}

	FString FVettedToolsetScope::ActiveToolId()
	{
		return GVettedToolsetToolIdStack.Num() > 0 ? GVettedToolsetToolIdStack.Last() : FString();
	}

	FString FVettedToolsetScope::ActiveVerifiedSha()
	{
		return GVettedToolsetShaStack.Num() > 0 ? GVettedToolsetShaStack.Last() : FString();
	}
}
