// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateExtra.h"
//

#include "AttributeCompatibility.h"
#include "Brushes/SlateImageBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/SlateDelegates.h"
#include "ImListDataComboImpl.h"
#include "ImSlateTemplates.h"
#include "ImSlateVirtualList.h"
#include "Internationalization/Text.h"
#include "Kismet/KismetMathLibrary.h"
#include "PrivateFieldAccessor.h"
#include "ProtectFieldAccessor.h"
#include "SImSlatePanel.h"
#include "SImSlateViewport.h"
#include "SImSlateWindow.h"
#include "SImViewportGame.h"
#include "SImViewportHost.h"
#include "UnrealCompatibility.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "ImSlateTemplate/ImSearchBox.h"
#include "ImSlateTemplate/ImVirtualKeyboard.h"
#include "ImSlateTemplate/ImCheckBox.h"  // SImCheckBox::SetCheckAccentColor
#if defined(IMSLATE3D_API)
#include "SImSlate3DPlaneArrow.h"        // ImSlate::GizmoArrow — transparent in-plane arrow item
#include "SImSlate3DTransformBox.h"      // current window's Content3DBox (owner of the arrow)
#include "SImSlateWindow.h"             // SImSlateWindow::GetContent3DBox()
#endif

namespace ImSlate
{
namespace ImSlateControls
{
#define GS_PRIVATEACCESS_SPINBOX(T, AliasType)                                           \
	using AliasType = SSpinBox<T>;                                                       \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnBeginSliderMovement, FSimpleDelegate);          \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnEndSliderMovement, AliasType::FOnValueChanged); \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnValueChanged, AliasType::FOnValueChanged);      \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnValueCommitted, AliasType::FOnValueCommitted);
	GS_PRIVATEACCESS_SPINBOX(float, SSpinBoxFloat)
	GS_PRIVATEACCESS_SPINBOX(double, SSpinBoxDouble)
	GS_PRIVATEACCESS_SPINBOX(int32, SSpinBoxInt32)
	GS_PRIVATEACCESS_SPINBOX(uint32, SSpinBoxUInt32)
	GS_PRIVATEACCESS_SPINBOX(int64, SSpinBoxInt64)
	GS_PRIVATEACCESS_SPINBOX(uint64, SSpinBoxUInt64)

#if ENGINE_MAJOR_VERSION < 5
	GS_PRIVATEACCESS_MEMBER(FSlateBrush, ResourceObject, UObject*)
#else
	GS_PRIVATEACCESS_MEMBER(FSlateBrush, ResourceObject, TObjectPtr<UObject>)
	GS_PRIVATEACCESS_MEMBER(SButton, Style, const FButtonStyle*)
#endif
#undef GS_PRIVATEACCESS_SPINBOX
}  // namespace ImSlateControls

using Internal::Item;
template<typename T>
TSharedPtr<T> GetMetaData(SWidget* InWidget)
{
	return InWidget ? InWidget->GetMetaData<T>() : TSharedPtr<T>();
}
void SetDesiredSize(const TSharedRef<SWidget>& InWidget, const ImVec2& InSize)
{
	SetDesiredSize(&InWidget.Get(), InSize);
}

#define IMSLATE_USING_GCROOT_REFERENCE 1
#if IMSLATE_USING_GCROOT_REFERENCE
bool AddReferencedObject(const UObject* InObj)
{
	bool bIsValid = IsValid(InObj);
	if (bIsValid)
		GImSlate->GetGCRoot()->AddReferencedObject(InObj);
	return bIsValid;
}
bool AddWindowedReferencedObject(const UObject* InObj)
{
	bool bIsValid = IsValid(InObj);
	if (bIsValid)
	{
		ImSlateContext& g = *GImSlate;
		g.GetGCRoot()->AddWindowedReferencedObject(g.CurrentWindow, InObj);
	}
	return bIsValid;
}
#else
bool AddReferencedObject(const UObject* InObj)
{
	return IsValid(InObj);
}
bool AddWindowedReferencedObject(const UObject* InObj)
{
	return IsValid(InObj);
}
#endif

struct FButtonMeta
	: public TSharedFromThis<FButtonMeta>
	, public ISlateMetaData
#if !IMSLATE_USING_GCROOT_REFERENCE
	, public FGCObject
#endif
{
	SLATE_METADATA_TYPE(FButtonMeta, ISlateMetaData)
public:
	bool bIsJustClicked = false;

	FButtonStyle MyStyle;
	TSharedPtr<STextBlock> TextBlock = nullptr;

	FText TooltipText;
	double PressStartTime = 0.0;

protected:
#if !IMSLATE_USING_GCROOT_REFERENCE
	virtual void AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(ImSlateControls::PrivateAccess::ResourceObject(MyStyle.Normal));
		Collector.AddReferencedObject(ImSlateControls::PrivateAccess::ResourceObject(MyStyle.Hovered));
		Collector.AddReferencedObject(ImSlateControls::PrivateAccess::ResourceObject(MyStyle.Pressed));
		Collector.AddReferencedObject(ImSlateControls::PrivateAccess::ResourceObject(MyStyle.Disabled));

		static auto GetResourceObject = [](FSlateSound& In) -> auto& { return GS_ACCESS_PROTECT(&In, FSlateSound, ResourceObject)->ResourceObject; };
		Collector.AddReferencedObject(GetResourceObject(MyStyle.PressedSlateSound));
		Collector.AddReferencedObject(GetResourceObject(MyStyle.HoveredSlateSound));
	}
#endif
};

struct FTextMeta
	: public TSharedFromThis<FTextMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FTextMeta, ISlateMetaData)
public:
	bool bIsEdited = false;
	FText Val;
};

bool Button(ImStr Label, const ImVec2& InSize /*= ImVec2(0, 0)*/)
{
	return TextButton(Label, FText::GetEmpty(), InSize);
}

bool TextButton(ImStr InLabel, const FText& InText, const ImVec2& InSize)
{
	auto ItemPtr = Item<SButton>(InLabel, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = false;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SButton> WidgetRef = ImFactoryCreate<UImButton>();
		auto Meta = MakeShared<FButtonMeta>();
		WidgetRef->SetHAlign(HAlign_Center);
		WidgetRef->SetVAlign(VAlign_Center);
		WidgetRef->SetContent(SAssignNew(Meta->TextBlock, STextBlock)
								.Text(InText)
								.Font(GetImSlateDefaultFont())
								.Justification(ETextJustify::Center)
								.Clipping(EWidgetClipping::ClipToBoundsAlways));
		SetDesiredSize(WidgetRef, InSize);

		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();
		WidgetRef->SetOnClicked(CreateWeakLambda(Ptr, [Ptr] {
			Ptr->bIsJustClicked = true;
			return FReply::Handled();
		}));
		return WidgetRef;
	});

	auto RetVal = false;
	if (ItemPtr)
	{
		if (auto Meta = GetMetaData<FButtonMeta>(ItemPtr))
		{
			if (Meta->bIsJustClicked)
			{
				RetVal = Meta->bIsJustClicked;
				Meta->bIsJustClicked = false;
			}
			else
			{
				Meta->TextBlock->SetText(InText);
			}

			if (!GImSlate->NextItemTooltip.IsEmpty() && Meta->TooltipText.IsEmpty())
				Meta->TooltipText = GImSlate->NextItemTooltip;

			if (!Meta->TooltipText.IsEmpty())
			{
				ImSlateContext& g = *GImSlate;
				if (ItemPtr->IsPressed())
				{
					if (Meta->PressStartTime == 0.0)
						Meta->PressStartTime = FPlatformTime::Seconds();
					else if (!g.LongPressTooltip.bVisible && FPlatformTime::Seconds() - Meta->PressStartTime > 0.5)
					{
						g.LongPressTooltip.bVisible = true;
						g.LongPressTooltip.Text = Meta->TooltipText;
						auto Geo = ItemPtr->GetCachedGeometry();
						g.LongPressTooltip.AbsolutePosition = Geo.GetAbsolutePosition() + FVector2D(0, Geo.GetAbsoluteSize().Y);
					}
				}
				else
				{
					Meta->PressStartTime = 0.0;
					if (g.LongPressTooltip.bVisible)
						g.LongPressTooltip.bVisible = false;
				}
			}
		}
	}
	return RetVal;
}

bool ImageButton(ImStr Label, UObject* InTexture, const ImVec2& InSize)
{
	if (!AddWindowedReferencedObject(InTexture))
		return false;
	auto ItemPtr = Item<SImageButton>(Label, [&](FItemSlotPod& InItem) {
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;
		InItem.bFillWidth = false;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SImageButton> WidgetRef = ImFactoryCreate<UImImageButton>();

		auto Meta = MakeShared<FButtonMeta>();
#if ENGINE_MAJOR_VERSION < 5
		Meta->MyStyle = *GS_ACCESS_PROTECT(WidgetRef, SImageButton, Style)->Style;
#else
		Meta->MyStyle = *ImSlateControls::PrivateAccess::Style(*WidgetRef);
#endif

		SetDesiredSize(WidgetRef, InSize);

		struct FImSlateImageBrush : public FSlateBrush
		{
			static void ApplyToStyle(FButtonStyle& InOutStyle, UObject* InResourceObject, const FVector2D& InImageSize)
			{
				FImSlateImageBrush ImageBrush(InOutStyle, InResourceObject, InImageSize);
				ImageBrush.ApplyToStyle(InOutStyle);
			}

		protected:
			FORCENOINLINE FImSlateImageBrush(const FButtonStyle& InStyle,
											 UObject* InResourceObject,
											 const FVector2D& InImageSize,
											 const FSlateColor& InTint = FSlateColor(FLinearColor(1, 1, 1, 1)),
											 ESlateBrushTileType::Type InTiling = ESlateBrushTileType::NoTile,
											 ESlateBrushImageType::Type InImageType = ESlateBrushImageType::FullColor)
				: FSlateBrush(ESlateBrushDrawType::Image, NAME_None, FMargin(0), InTiling, InImageType, InImageSize, InTint, InResourceObject)
			{
				GetResources(InStyle);
			}

			FSlateBrush MergeBursh(const FSlateBrush& InBrush) const
			{
				FSlateBrush ImageBrush = *this;
				ImageBrush.Margin = InBrush.Margin;
				ImageBrush.TintColor = InBrush.TintColor;
				ImageBrush.DrawAs = InBrush.DrawAs;
				ImageBrush.Tiling = InBrush.Tiling;
				ImageBrush.Mirroring = InBrush.Mirroring;
				ImageBrush.ImageType = InBrush.ImageType;
				return ImageBrush;
			}
			FSlateBrush MergeBursh(int32 Index) const { return MergeBursh(*OutBrushes[Index]); }

			void ApplyToStyle(FButtonStyle& InStyle) { InStyle.SetNormal(MergeBursh(0)).SetPressed(MergeBursh(1)).SetHovered(MergeBursh(2)).SetDisabled(MergeBursh(3)); }

			void GetResources(const FButtonStyle& InStyle)
			{
				InStyle.GetResources(OutBrushes);
				check(OutBrushes.Num() >= 4);
				ImageSize = OutBrushes[0]->ImageSize;
			}
			TArray<const FSlateBrush*> OutBrushes;
		};

		FImSlateImageBrush::ApplyToStyle(Meta->MyStyle, InTexture, InSize);

		WidgetRef->SetButtonStyle(&Meta->MyStyle);
		WidgetRef->AddMetadata(Meta);

		WidgetRef->SetContent(SNew(STextBlock)
								.Text(FText::FromString(LabelStr))
								.Font(GetImSlateDefaultFont())
								.Clipping(EWidgetClipping::ClipToBoundsAlways));

		auto Ptr = &Meta.Get();
		WidgetRef->SetOnClicked(CreateWeakLambda(Ptr, [Ptr] {
			Ptr->bIsJustClicked = true;
			return FReply::Handled();
		}));
		return WidgetRef;
	});

	auto RetVal = false;
	if (auto Meta = GetMetaData<FButtonMeta>(ItemPtr))
	{
		RetVal = Meta->bIsJustClicked;
		Meta->bIsJustClicked = false;
	}
	return RetVal;
}

bool Text(ImStr Label, const FText& InText, const ImVec2& InSize, bool bAutoWrapText)
{
	auto ItemPtr = Item<STextBlock>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<STextBlock> WidgetRef = ImFactoryCreate<UImTextBlock>();
		WidgetRef->SetText(InText);
		WidgetRef->SetAutoWrapText(bAutoWrapText);
		SetDesiredSize(WidgetRef, InSize);
		return WidgetRef;
	});

	if (ItemPtr && ItemPtr->GetText().EqualTo(InText))
	{
		ItemPtr->SetText(InText);
		return false;
	}
	return false;
}

bool InputText(ImStr Label, FString& InStr, const ImVec2& InSize /* = ImVec2(0, 0)*/, ImSlateInputTextFlags_ Flags /*= ImSlateInputTextFlags_None*/, ImStr HistoryKey /* = {} */, const FImInputNumericSpec* Numeric /* = nullptr */)
{
	auto ItemPtr = Item<SEditableText>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SEditableText> WidgetRef = ImFactoryCreate<UImEditableText>();
		SetDesiredSize(WidgetRef, InSize);

		WidgetRef->SetText(FText::FromString(InStr));
		WidgetRef->SetIsReadOnly(!!(Flags & ImSlateInputTextFlags_ReadOnly));

		// Numeric keypad on focus. Prefer the explicit FImInputNumericSpec (type/clamp/step) when the
		// caller passes one; otherwise fall back to the InputText flags (CharsDecimal/Hex/Scientific).
		// The widget is a SImEditableText (created from UImEditableText), so the cast is safe.
		const bool bFlagNumeric = (Flags & (ImSlateInputTextFlags_CharsDecimal | ImSlateInputTextFlags_CharsHexadecimal | ImSlateInputTextFlags_CharsScientific)) != 0;
		if (Numeric || bFlagNumeric)
		{
			TSharedRef<SImEditableText> ImEdit = StaticCastSharedRef<SImEditableText>(WidgetRef);
			ImEdit->SetVirtualKeyboardType(Keyboard_Number);
			if (Numeric)
			{
				// Explicit spec: integer hides '.', unsigned hides '-', hex adds A-F; clamp + step.
				ImEdit->SetNumericParams(/*decimal*/!Numeric->bInteger, /*negative*/!Numeric->bUnsigned,
					/*hex*/Numeric->bHex, Numeric->Min, Numeric->Max, Numeric->Step);
			}
			else
			{
				const bool bHex = (Flags & ImSlateInputTextFlags_CharsHexadecimal) != 0;
				const bool bDecimal = (Flags & (ImSlateInputTextFlags_CharsDecimal | ImSlateInputTextFlags_CharsScientific)) != 0;
				ImEdit->SetNumericParams(/*decimal*/bDecimal, /*negative*/true, /*hex*/bHex, {}, {}, {});
			}
		}

		// Input history: enabled only when a non-empty HistoryKey is passed (entries persist per key).
		if (HistoryKey.Len() > 0)
			StaticCastSharedRef<SImEditableText>(WidgetRef)->SetHistoryKey(FString(HistoryKey.Len(), HistoryKey.GetData()));

		bool bEnabled = !(Flags & ImSlateInputTextFlags_ReadOnly);
		WidgetRef->SetEnabled(bEnabled);

		auto Meta = MakeShared<FTextMeta>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();

		GS_ACCESS_PROTECT(WidgetRef, SEditableText, OnTextCommittedCallback)->OnTextCommittedCallback = CreateWeakLambda(Ptr, [&, Ptr](const FText& InCommitText, ETextCommit::Type InTyp) -> void {
			if (InTyp != ETextCommit::OnCleared)
			{
				Ptr->Val = InCommitText;
				Ptr->bIsEdited = true;
			}
			return;
		});

		return WidgetRef;
	});

	auto RetVal = false;
	if (auto Meta = GetMetaData<FTextMeta>(ItemPtr))
	{
		if (Meta->bIsEdited)
		{
			RetVal = Meta->bIsEdited;
			InStr = Meta->Val.ToString();
			Meta->bIsEdited = false;
		}
		else if (!ItemPtr->HasAnyUserFocus().IsSet() && InStr != ItemPtr->GetText().ToString())
		{
			ItemPtr->SetText(FText::FromString(InStr));
		}
	}
	return RetVal;
}

// ==================== SearchBox ====================

struct FSearchBoxMeta : public TSharedFromThis<FSearchBoxMeta>, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FSearchBoxMeta, ISlateMetaData)

	TArray<FString> History;
	FString PrevInput;
	static constexpr int32 MaxHistory = 32;
	static constexpr int32 MaxVisible = 8;

	void AddToHistory(const FString& Str)
	{
		if (Str.IsEmpty()) return;
		History.Remove(Str);
		History.Insert(Str, 0);
		if (History.Num() > MaxHistory)
			History.SetNum(MaxHistory);
	}

	int32 LastHistoryCount = 0;

	static bool MatchesAllTerms(const FString& Candidate, const TArray<FString>& Terms)
	{
		for (const FString& Term : Terms)
		{
			if (!Term.IsEmpty() && !Candidate.Contains(Term, ESearchCase::IgnoreCase))
				return false;
		}
		return true;
	}

	// bIncludeHistory: SearchBox's OWN dropdown wants history rows merged in (true). The VIRTUAL
	// KEYBOARD provider must NOT include them (false) — the keyboard shows history from its own
	// persistent store (FImSlateInputHistory) with delete X buttons; if Filter also fed history here the
	// keyboard would show a SECOND copy without an X, and deleting the keyboard's copy would leave this
	// one behind (it reappears on reopen).
	TArray<FString> Filter(const FString& Input, const TArray<FString>* Src, const TFunction<void(const FString&, TArray<FString>&)>& Cb, bool bIncludeHistory = true)
	{
		TArray<FString> HistoryMatches;
		TArray<FString> Result;

		TArray<FString> Terms;
		Input.ParseIntoArray(Terms, TEXT(" "), true);

		// History matches first (only when this caller wants them — keyboard provider opts out).
		if (bIncludeHistory)
		{
			for (const FString& H : History)
			{
				if (Terms.Num() == 0 || MatchesAllTerms(H, Terms))
					HistoryMatches.Add(H);
			}
		}

		// Source/callback matches (exclude duplicates from history)
		TArray<FString> SourceMatches;
		if (Terms.Num() > 0)
		{
			if (Cb)
			{
				Cb(Input, SourceMatches);
			}
			else if (Src)
			{
				TArray<FString> StartsWithMatches;
				TArray<FString> ContainsMatches;
				for (const FString& S : *Src)
				{
					if (!MatchesAllTerms(S, Terms))
						continue;
					if (S.StartsWith(Terms[0], ESearchCase::IgnoreCase))
						StartsWithMatches.Add(S);
					else
						ContainsMatches.Add(S);
				}
				SourceMatches.Append(StartsWithMatches);
				SourceMatches.Append(ContainsMatches);
			}
			SourceMatches.RemoveAll([&](const FString& S) { return HistoryMatches.Contains(S); });
		}

		LastHistoryCount = FMath::Min(HistoryMatches.Num(), MaxVisible);
		Result.Append(HistoryMatches);
		Result.Append(SourceMatches);
		if (Result.Num() > MaxVisible)
			Result.SetNum(MaxVisible);
		LastHistoryCount = FMath::Min(LastHistoryCount, Result.Num());
		return Result;
	}

	void RemoveFromHistory(const FString& Str)
	{
		History.Remove(Str);
	}
};

bool SearchBox(ImStr Label, FString& InOutStr, const TArray<FString>* Suggestions, TFunction<void(const FString&, TArray<FString>&)> SuggestionCallback, const ImVec2& InSize, bool bShowKeyboardButton)
{
	auto ItemPtr = Item<SImSearchBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SImSearchBox> WidgetRef = SNew(SImSearchBox).bShowKeyboardButton(bShowKeyboardButton);
		WidgetRef->SetText(FText::FromString(InOutStr));
		SetDesiredSize(WidgetRef, InSize);

		auto Meta = MakeShared<FSearchBoxMeta>();
		WidgetRef->AddMetadata(Meta);
		return WidgetRef;
	});

	bool bRetVal = false;
	if (ItemPtr)
	{
		auto SearchBox = static_cast<SImSearchBox*>(ItemPtr);
		auto Meta = GetMetaData<FSearchBoxMeta>(ItemPtr);

		// Give the popped virtual keyboard a stable history key (defaults to the Label) so its history
		// rows get the X delete button and persist. The SearchBox's own dropdown keeps its in-memory
		// history (FSearchBoxMeta) — the two are intentionally separate for now.
		SearchBox->SetHistoryKey(FString(Label.Len(), Label.GetData()));

		// Set suggestion provider for virtual keyboard
		if (Meta)
		{
			TWeakPtr<FSearchBoxMeta> WeakM = Meta;
			SearchBox->SetKeyboardSuggestionProvider(
				[WeakM, Suggestions, SuggestionCallback](const FString& Input, TArray<FString>& Out) {
					if (auto M = WeakM.Pin())
						// No history here — the keyboard renders history from its own persistent store
						// (with X). Including it would duplicate it without an X and resurrect deleted
						// entries on reopen.
						Out = M->Filter(Input, Suggestions, SuggestionCallback, /*bIncludeHistory*/ false);
				});
		}

		// Detect text change, focus-trigger, or first-focus and update suggestions
		FString CurrentText = SearchBox->GetText().ToString();
		bool bForceRefresh = SearchBox->ConsumeNeedsSuggestionRefresh();
		bool bHasFocus = ItemPtr->HasAnyUserFocus().IsSet() || ItemPtr->HasFocusedDescendants();
		if (!bForceRefresh && bHasFocus && !SearchBox->IsMenuOpen() && Meta)
			bForceRefresh = true;
		if (Meta && (bForceRefresh || CurrentText != Meta->PrevInput))
		{
			Meta->PrevInput = CurrentText;
			auto Filtered = Meta->Filter(CurrentText, Suggestions, SuggestionCallback);
			TWeakPtr<FSearchBoxMeta> WeakMeta = Meta;
			SearchBox->SetSuggestions(Filtered, Meta->LastHistoryCount,
				[WeakMeta, Suggestions, SuggestionCallback, SearchBox](const FString& Item) {
					if (auto M = WeakMeta.Pin())
					{
						M->RemoveFromHistory(Item);
						TArray<FString> Refreshed = M->Filter(M->PrevInput, Suggestions, SuggestionCallback);
						SearchBox->SetSuggestions(Refreshed, M->LastHistoryCount, nullptr);
					}
				});
		}

		// Check for commit (Enter or suggestion click)
		if (SearchBox->HasPendingCommit())
		{
			InOutStr = SearchBox->ConsumeCommit();
			if (Meta && !InOutStr.IsEmpty())
			{
				bool bIsValidEntry = !Suggestions || Suggestions->Contains(InOutStr);
				if (!bIsValidEntry && SuggestionCallback)
				{
					TArray<FString> Check;
					SuggestionCallback(InOutStr, Check);
					bIsValidEntry = Check.Contains(InOutStr);
				}
				if (bIsValidEntry)
					Meta->AddToHistory(InOutStr);
			}
			bRetVal = true;
		}
		else if (!ItemPtr->HasAnyUserFocus().IsSet() && !ItemPtr->HasFocusedDescendants() && !SearchBox->IsMenuOpen()
			&& !SearchBox->IsVirtualKeyboardActiveForMe()  // editing via VK: edit is source of truth, don't revert
			&& InOutStr != CurrentText)
		{
			SearchBox->SetText(FText::FromString(InOutStr));
		}
	}
	return bRetVal;
}

// ==================== KeyboardButton ====================

bool KeyboardButton(ImStr Label, const FVirtualKeyboardShowParams& Params, const ImVec2& InSize)
{
	if (TextButton(Label, FText::FromString(TEXT("\x2328")), InSize))
	{
		if (auto Kb = SImSlateVirtualKeyboard::Get())
			Kb->Show(Params);
		return true;
	}
	return false;
}

// ==================== Float/Numeric ====================

struct FFloatMeta
	: public TSharedFromThis<FFloatMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FFloatMeta, ISlateMetaData)
public:
	bool bIgnoreSetValue = false;
	bool bIsChanged = false;
	bool bIsCommitted = false;
	float OldValue = 0.f;
	float ChangedValue = 0.f;
	TUniqueFunction<void(float InVal, float InOldVal)> Cb = [](float InVal, float InOldVal) {};
};

struct FFloatMeta2
	: public TSharedFromThis<FFloatMeta2>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FFloatMeta2, ISlateMetaData)
public:
	bool bIgnoreSetValue = false;
	float ChangedValue = 0.f;
	ImSliderStatus_ State = ImSliderStatus_Normal;
};

ImSliderStatus_ InputFloat(ImStr Label, decltype(FVector::ZeroVector.X)& ValRef, float ValMin, float ValMax, float step, float step_fast, int32 NumDecimals, bool bResetState, ImSlateFloatFlags_ flags, const ImVec2& InSize)
{
	using namespace ImSlate::ImSlateControls;
	ImSliderStatus_ RetVal = ImSliderStatus_Normal;
	auto ItemPtr = Item<SSpinBoxFloat>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SSpinBoxFloat> WidgetRef = ImFactoryCreate<UImSpinBox>();
		InItem.SetMinHeight(19.f);
		WidgetRef->SetValue(ValRef);

		WidgetRef->SetMaxFractionalDigits(NumDecimals);
		SetDesiredSize(WidgetRef, InSize);

		if (ValMin != FLT_MIN)
		{
			WidgetRef->SetMinValue(ValMin);
			WidgetRef->SetMinSliderValue(ValMin);
		}

		if (ValMax != FLT_MAX)
		{
			WidgetRef->SetMaxValue(ValMax);
			WidgetRef->SetMaxSliderValue(ValMax);
		}

		if (!UKismetMathLibrary::NearlyEqual_FloatFloat(step, 0.f))
		{
			WidgetRef->SetDelta(step);
		}

		WidgetRef->SetEnabled(!(flags & ImSlateFloatFlags_ReadOnly));

		auto Meta = MakeShared<FFloatMeta2>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();
		PrivateAccess::OnBeginSliderMovement(WidgetRef.Get()) = CreateWeakLambda(Ptr, [Ptr]() -> void {
			if (Ptr->bIgnoreSetValue)
				return;
			Ptr->bIgnoreSetValue = true;
			Ptr->State = ImSliderStatus_BeginSlider;
		});

		PrivateAccess::OnEndSliderMovement(WidgetRef.Get()) = CreateWeakLambda(Ptr, [Ptr](float InVal) -> void {
			if (Ptr->bIgnoreSetValue)
				return;
			Ptr->State = ImSliderStatus_EndSlider;
		});

		PrivateAccess::OnValueChanged(WidgetRef.Get()) = CreateWeakLambda(Ptr, [Ptr, Name(LabelStr)](float InVal) -> void {
			if (Ptr->bIgnoreSetValue || Ptr->State == ImSliderStatus_Committed)
				return;
			Ptr->ChangedValue = InVal;
			Ptr->State = ImSliderStatus_ValueChanged;
			UE_LOG(LogImSlate, Verbose, TEXT("InputStateFloat Changed: label: %s"), *Name);
		});

		PrivateAccess::OnValueCommitted(WidgetRef.Get()) = CreateWeakLambda(Ptr, [Ptr, Name(LabelStr)](float InVal, ETextCommit::Type InTyp) -> void {
			if (Ptr->bIgnoreSetValue)
				return;

			if (InTyp != ETextCommit::OnCleared)
			{
				Ptr->ChangedValue = InVal;
			}
			UE_LOG(LogImSlate, Verbose, TEXT("InputStateFloat Commit: label: %s"), *Name);
			Ptr->State = ImSliderStatus_Committed;
		});

		return WidgetRef;
	});

	if (auto Meta = GetMetaData<FFloatMeta2>(ItemPtr))
	{
		if (bResetState)
			Meta->State = ImSliderStatus_Normal;

		if (auto* Ptr = static_cast<SSpinBoxFloat*>(ItemPtr))
		{
			if (Meta->State == ImSliderStatus_Normal)
			{
				if (Ptr->GetMaxValue() != ValMax)
				{
					Ptr->SetMaxValue(ValMax);
					Ptr->SetMaxSliderValue(ValMax);
				}
				if (Ptr->GetMinValue() != ValMin)
				{
					Ptr->SetMinValue(ValMin);
					Ptr->SetMinSliderValue(ValMin);
				}

				Meta->bIgnoreSetValue = true;
				Ptr->SetValue(ValRef);
				Meta->bIgnoreSetValue = false;
			}

			if (Ptr->IsEnabled() != (!!!(flags & ImSlateFloatFlags_ReadOnly)))
			{
				Ptr->SetEnabled(!(flags & ImSlateFloatFlags_ReadOnly));
			}
		}

		RetVal = Meta->State;
		switch (Meta->State)
		{
			case ImSliderStatus_BeginSlider:
			{
				Meta->State = ImSliderStatus_ValueChanged;
				Meta->bIgnoreSetValue = false;
			}
			break;
			case ImSliderStatus_EndSlider:
			{
				Meta->State = ImSliderStatus_Normal;
				Meta->bIgnoreSetValue = false;
				ValRef = Meta->ChangedValue;
			}
			break;
			case ImSliderStatus_ValueChanged:
			{
				//UE_LOG(LogImSlate, VeryVerbose, TEXT("ValueChanged Assign: change from %f to %f"), ValRef, Meta->ChangedValue);
				ValRef = Meta->ChangedValue;
			}
			break;
			case ImSliderStatus_Committed:
			{
				UE_LOG(LogImSlate, Verbose, TEXT("InputStateFloat Commit State"));
				Meta->State = ImSliderStatus_Normal;
				Meta->bIgnoreSetValue = false;
				ValRef = Meta->ChangedValue;
			}
			default:
				break;
		}
	}

	return RetVal;
}
template<typename T>
struct UImNumbericWidgetT
{
};
template<>
struct UImNumbericWidgetT<float>
{
	using Type = UImNumericFloatWidget;
};
template<>
struct UImNumbericWidgetT<double>
{
	using Type = UImNumericDoubleWidget;
};
template<>
struct UImNumbericWidgetT<int32>
{
	using Type = UImNumericInt32Widget;
};
template<>
struct UImNumbericWidgetT<uint32>
{
	using Type = UImNumericUInt32Widget;
};
template<>
struct UImNumbericWidgetT<int64>
{
	using Type = UImNumericInt64Widget;
};
template<>
struct UImNumbericWidgetT<uint64>
{
	using Type = UImNumericUInt64Widget;
};
template<typename T>
using UImNumbericWidgetType = typename UImNumbericWidgetT<T>::Type;

template<typename T>
struct TNumericMeta;
#define DEF_IMSLATE_NUMERIC_META(T)                          \
	template<>                                               \
	struct TNumericMeta<T>                                   \
		: public TSharedFromThis<TNumericMeta<T>>            \
		, public ISlateMetaData                              \
	{                                                        \
		SLATE_METADATA_TYPE(TNumericMeta<T>, ISlateMetaData) \
	public:                                                  \
		TOptional<T> ChangedValue;                           \
		ImSliderStatus_ State = ImSliderStatus_Normal;       \
		bool bLockState = false;                             \
	};
DEF_IMSLATE_NUMERIC_META(float)
DEF_IMSLATE_NUMERIC_META(double)
DEF_IMSLATE_NUMERIC_META(int32)
DEF_IMSLATE_NUMERIC_META(uint32)
DEF_IMSLATE_NUMERIC_META(int64)
DEF_IMSLATE_NUMERIC_META(uint64)

template<typename T>
static bool IsNearlyEqual(T A, T B)
{
	if constexpr (TIsFloatingPoint<T>::Value)
		return FMath::IsNearlyEqual(A, B, KINDA_SMALL_NUMBER);
	else
		return A == B;
}

template<typename T>
ImSliderStatus_ NumericGeneric(ImStr Label,  //
							   TOptional<T>& ValRef,
							   T ValMin,
							   T ValMax,
							   T SliderMin,
							   T SliderMax,
							   T Delta,
							   T SliderExponent,
							   TOptional<int32> MinDigits,
							   TOptional<int32> MaxDigits,
							   ImSlateInputTextFlags_ Flags,
							   const ImVec2& InSize)
{
	ImSliderStatus_ RetVal = ImSliderStatus_Normal;
	auto ItemPtr = Item<SImNumericWidget<T>>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SImNumericWidget<T>> WidgetRef = ImFactoryCreate<UImNumbericWidgetType<T>>();

		if (ValMax != std::numeric_limits<T>::max())
		{
			WidgetRef->SetMaxValue(ValMax);
		}
		if (ValMin != std::numeric_limits<T>::min())
		{
			WidgetRef->SetMinValue(ValMin);
		}
		if (SliderMax != std::numeric_limits<T>::max())
		{
			WidgetRef->SetMaxSliderValue(SliderMax);
		}
		if (SliderMin != std::numeric_limits<T>::min())
		{
			WidgetRef->SetMinSliderValue(SliderMin);
		}
		if (!IsNearlyEqual(Delta, (T)0))
		{
			WidgetRef->SetDelta(Delta);
		}
		if (!IsNearlyEqual(SliderExponent, (T)0))
		{
			WidgetRef->SetSliderExponent(SliderExponent);
		}
		if (MinDigits.IsSet())
		{
			WidgetRef->SetMinFractionalDigits(MinDigits);
		}
		if (MaxDigits.IsSet())
		{
			WidgetRef->SetMinFractionalDigits(MaxDigits);
		}
		WidgetRef->SetEnabled(!(Flags & ImSlateInputTextFlags_ReadOnly));
		auto Meta = MakeShared<TNumericMeta<T>>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();

		WidgetRef->SetOnBeginSliderMovement(CreateWeakLambda(Ptr, [Ptr]() -> void {
			if (Ptr->bLockState)
				return;
			UE_LOG(LogImSlate, Verbose, TEXT("Numeric : BeginSlider"));
			Ptr->bLockState = true;
			Ptr->State = ImSliderStatus_BeginSlider;
		}));

		WidgetRef->SetOnEndSliderMovement(CreateWeakLambda(Ptr, [Ptr](T Value) -> void {
			if (Ptr->bLockState)
				return;
			UE_LOG(LogImSlate, Verbose, TEXT("Numeric : EndSlider"));
			Ptr->ChangedValue = Value;
			Ptr->State = ImSliderStatus_EndSlider;
		}));

		WidgetRef->SetOnValueChanged(CreateWeakLambda(Ptr, [Ptr](T Value) -> void {
			if (Ptr->bLockState || Ptr->State == ImSliderStatus_Committed)
				return;
			UE_LOG(LogImSlate, Verbose, TEXT("Numeric : OnValueChanged"));
			Ptr->ChangedValue = Value;
			Ptr->State = ImSliderStatus_ValueChanged;
		}));

		WidgetRef->SetOnValueCommitted(CreateWeakLambda(Ptr, [Ptr](T Value, ETextCommit::Type InTyp) -> void {
			if (Ptr->bLockState)
				return;
			UE_LOG(LogImSlate, Verbose, TEXT("Numeric : OnValueCommitted"));
			if (InTyp != ETextCommit::OnCleared)
			{
				Ptr->ChangedValue = Value;
				Ptr->State = ImSliderStatus_Committed;
			}
		}));

		Ptr->ChangedValue = ValRef;
		WidgetRef->SetValue(TAttribute<TOptional<T>>::CreateSPLambda(Meta, [Ptr]() { return TOptional<T>(Ptr->ChangedValue); }));
		return WidgetRef;
	});

	if (auto Meta = GetMetaData<TNumericMeta<T>>(ItemPtr))
	{
		if (Meta->State == ImSliderStatus_Normal)
		{
			Meta->bLockState = true;
			ItemPtr->SetMultValueVisible(!ValRef.IsSet());
			Meta->ChangedValue = ValRef;
			Meta->bLockState = false;
		}

		RetVal = Meta->State;
		switch (Meta->State)
		{
			case ImSliderStatus_BeginSlider:
			{
				Meta->State = ImSliderStatus_ValueChanged;
				Meta->bLockState = false;
			}
			break;
			case ImSliderStatus_EndSlider:
			{
				Meta->State = ImSliderStatus_Normal;
			}
			break;
			case ImSliderStatus_ValueChanged:
			{
				ValRef = Meta->ChangedValue;
			}
			break;
			case ImSliderStatus_Committed:
			{
				Meta->State = ImSliderStatus_Normal;
				ValRef = Meta->ChangedValue;
			}
			default:
				break;
		}
	}

	return RetVal;
}

IMSLATE_API ImSliderStatus_ NumericFloat(ImStr Label,
										 TOptional<float>& ValRef,
										 float ValMin,
										 float ValMax,
										 float SliderMin,
										 float SliderMax,
										 float Delta,
										 float SliderExponent,
										 TOptional<int32> MinDigits,
										 TOptional<int32> MaxDigits,
										 ImSlateInputTextFlags_ Flags,
										 const ImVec2& InSize)
{
	return NumericGeneric<float>(Label, ValRef, ValMin, ValMax, SliderMin, SliderMax, Delta, SliderExponent, MinDigits, MaxDigits, Flags, InSize);
}

IMSLATE_API ImSliderStatus_ NumericInt(ImStr Label,
									   TOptional<int32>& ValRef,
									   int32 ValMin,
									   int32 ValMax,
									   int32 SliderMin,
									   int32 SliderMax,
									   int32 Delta,
									   int32 SliderExponent,
									   ImSlateInputTextFlags_ Flags,
									   const ImVec2& InSize)
{
	return NumericGeneric<int32>(Label, ValRef, ValMin, ValMax, SliderMin, SliderMax, Delta, SliderExponent, 0, 0, Flags, InSize);
}

//CheckBox
struct FCheckBoxMeta
	: public TSharedFromThis<FCheckBoxMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FCheckBoxMeta, ISlateMetaData)
public:
	bool bHasChanged = false;
	ECheckBoxState CurState = ECheckBoxState::Undetermined;
};

bool CheckBox(ImStr Label, bool& bIsChecked, const ImVec2& InSize, const FLinearColor& AccentColor)
{
	auto ItemPtr = Item<SCheckBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SCheckBox> WidgetRef = ImFactoryCreate<UImCheckBox>();

		//WidgetRef->SetEnabled(!(flags & ImInputTextFlags_ReadOnly));
		WidgetRef->SetIsChecked(bIsChecked);
		StaticCastSharedRef<SImCheckBox>(WidgetRef)->SetCheckAccentColor(AccentColor);  // tint the mark
		SetDesiredSize(WidgetRef, InSize);

		auto Meta = MakeShared<FCheckBoxMeta>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();

		GS_ACCESS_PROTECT(WidgetRef, SCheckBox, OnCheckStateChanged)->OnCheckStateChanged = FOnCheckStateChanged::CreateLambda([Ptr](ECheckBoxState InTyp) -> void {
			UE_LOG(LogImSlate, Verbose, TEXT("Enter CheckStateChanged"));
			if (Ptr->bHasChanged == false)
			{
				Ptr->bHasChanged = true;
			}
			return;
		});

		return WidgetRef;
	});

	bool RetVal = false;
	if (auto Meta = GetMetaData<FCheckBoxMeta>(ItemPtr))
	{
		RetVal = Meta->bHasChanged;
		if (Meta->bHasChanged)
		{
			bIsChecked = ItemPtr->IsChecked();
			Meta->bHasChanged = false;
		}
		else
		{
			ItemPtr->SetIsChecked(bIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}
	}
	return RetVal;
}

bool CheckBox(ImStr Label, ECheckBoxState& CheckState, const ImVec2& InSize)
{
	auto ItemPtr = Item<SCheckBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SCheckBox> WidgetRef = ImFactoryCreate<UImCheckBox>();

		WidgetRef->SetIsChecked(CheckState);

		SetDesiredSize(WidgetRef, InSize);

		auto Meta = MakeShared<FCheckBoxMeta>();
		WidgetRef->AddMetadata(Meta);
		auto Ptr = &Meta.Get();

		Ptr->CurState = CheckState;

		// Tri-state cycle on click. SCheckBox only toggles Checked↔Unchecked on its own (InState is
		// never Undetermined), so we ignore InState and advance our OWN tracked state through all
		// three: Unchecked → Checked → Undetermined → Unchecked.
		GS_ACCESS_PROTECT(WidgetRef, SCheckBox, OnCheckStateChanged)->OnCheckStateChanged = FOnCheckStateChanged::CreateLambda([Ptr](ECheckBoxState /*InState*/) -> void {
			switch (Ptr->CurState)
			{
				case ECheckBoxState::Unchecked:     Ptr->CurState = ECheckBoxState::Checked;       break;
				case ECheckBoxState::Checked:       Ptr->CurState = ECheckBoxState::Undetermined;  break;
				default:                            Ptr->CurState = ECheckBoxState::Unchecked;     break;
			}
			Ptr->bHasChanged = true;
			return;
		});

		return WidgetRef;
	});

	bool RetVal = false;

	if (auto Meta = GetMetaData<FCheckBoxMeta>(ItemPtr))
	{
		RetVal = Meta->bHasChanged;
		if (Meta->bHasChanged)
		{
			UE_LOG(LogImSlate, Verbose, TEXT("Enter EditHasChanged"));
			Meta->bHasChanged = false;
			CheckState = Meta->CurState;
		}
		ItemPtr->SetIsChecked(CheckState);
	}
	return RetVal;
}

struct FImageMeta
	: public TSharedFromThis<FImageMeta>
	, public ISlateMetaData
#if !IMSLATE_USING_GCROOT_REFERENCE
	, public FGCObject
#endif
{
	SLATE_METADATA_TYPE(FImageMeta, ISlateMetaData)
public:
	FSlateBrush ImageBrush;

protected:
#if !IMSLATE_USING_GCROOT_REFERENCE
	virtual void AddReferencedObjects(FReferenceCollector& Collector) { Collector.AddReferencedObject(PrivateAccess::ResourceObject(ImageBrush)); }
#endif
};

bool Image(ImStr Label, UObject* InTexture, const ImVec2& InSize)
{
	if (!ensure(AddWindowedReferencedObject(InTexture)))
		return false;

	auto ItemPtr = Item<SImage>(Label, [&](FItemSlotPod& InItem) {
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;
		InItem.bFillWidth = false;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SImage> WidgetRef = ImFactoryCreate<UImImage>();
		SetDesiredSize(WidgetRef, InSize);

		auto Meta = MakeShared<FImageMeta>();
		auto BrushSize = InSize;
#if ENGINE_MAJOR_VERSION < 5
		auto BrushPtr = GS_ACCESS_PROTECT(WidgetRef, SImage, Image)->Image.GetImage().Get();
#else
		auto BrushPtr = GS_ACCESS_PROTECT(WidgetRef, SImage, GetImageAttribute)->GetImageAttribute().Get();
#endif
		if (BrushSize.IsNearlyZero() && BrushPtr)
			BrushSize = (FVector2D)BrushPtr->ImageSize;

		Meta->ImageBrush = FSlateImageBrush(InTexture, BrushSize);
		WidgetRef->SetImage(&Meta->ImageBrush);

		// UE 5.7: Use TAttribute::CreateLambda for non-UObject types with lambda capture
		WidgetRef->SetImage(TAttribute<const FSlateBrush*>::CreateLambda([Meta]() { return (const FSlateBrush*)&Meta->ImageBrush; }));
		WidgetRef->AddMetadata(Meta);
		return WidgetRef;
	});

	if (ItemPtr)
	{
		if (auto Meta = GetMetaData<FImageMeta>(ItemPtr))
		{
			auto CurBursh = FSlateImageBrush(InTexture, InSize);
			if (CurBursh != Meta->ImageBrush)
			{
				Meta->ImageBrush = CurBursh;
			}
		}
		return true;
	}
	return false;
}

// ComboBoxMeta
struct FComboBoxWidgetMeta
	: public TSharedFromThis<FComboBoxWidgetMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FComboBoxWidgetMeta, ISlateMetaData)
public:
	ImSlateComboFlags_ Flags = ImSlateComboFlags_None;
};

// ComboBox
bool ComboBox(ImStr Label, TSharedPtr<IImComboBoxItem>& InOutSelected, const FComboSourceType& InSource, bool bHasSearchBox /* = false*/, ImSlateComboFlags_ Flags /*= ImSlateComboFlags_None*/, const ImVec2& InSize /*= ImVec2(0, 0)*/)
{
	auto ItemPtr = Item<SImComboBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SImComboBox> WidgetRef = ImFactoryCreate<UImComboButton>();
		auto Meta = MakeShared<FComboBoxWidgetMeta>();
		WidgetRef->AddMetadata(Meta);
		SetDesiredSize(WidgetRef, InSize);
		WidgetRef->SetOptionsSoure(InSource);
		WidgetRef->SetSearchBoxDisplay(bHasSearchBox);
		WidgetRef->SetEnabled(!(Flags & ImSlateComboFlags_ReadOnly));
		return WidgetRef;
	});

	if (ItemPtr)
	{
		if (auto Meta = GetMetaData<FComboBoxWidgetMeta>(ItemPtr))
		{
			if (Meta->Flags != Flags)
			{
				Meta->Flags = Flags;
				ItemPtr->SetEnabled(!(Flags & ImSlateInputTextFlags_ReadOnly));
			}
		}
		if (ItemPtr->GetAndResetChangedState())
		{
			InOutSelected = ItemPtr->GetSelectedItem();
			return true;
		}
		else
		{
			ItemPtr->SetOptionsSoure(InSource);
		}

		ItemPtr->SetSelectedItem(InOutSelected);
	}

	return false;
}

bool ComboBox(ImStr Label, int32& InOutCurrentIndex, const TSharedRef<FImListDataComboImpl>& InDataStore, ImSlateComboFlags_ Flags /*= ImSlateComboFlags_None*/, const ImVec2& InSize /*= ImVec2(0, 0)*/)
{
	auto ItemPtr = Item<SImVirtualComboBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SImVirtualComboBox> WidgetRef = ImFactoryCreate<UImVirtualComboButton>();
		SetDesiredSize(WidgetRef, InSize);
		InDataStore->ClearCurrentSelection();
		WidgetRef->SetDataStore(InDataStore);
		//WidgetRef->SetSearchBoxDisplay(bHasSearchBox);
		WidgetRef->SetEnabled(!(Flags & ImSlateComboFlags_ReadOnly));
		InDataStore->SetCurrentSelected(InOutCurrentIndex);
		return WidgetRef;
	});

	bool bSelectionChanged = false;
	if (ItemPtr && ItemPtr->IsEnabled())
	{
		if (ItemPtr->IsEnabled() == !!(Flags & ImSlateInputTextFlags_ReadOnly))
			ItemPtr->SetEnabled(!(Flags & ImSlateComboFlags_ReadOnly));
		if (auto& bValueChanged = ItemPtr->GetValueChangedRef())
		{
			bSelectionChanged = true;
			bValueChanged = false;
		}
		if (!bSelectionChanged)
		{
			InDataStore->SetCurrentSelected(InOutCurrentIndex);
		}

		InOutCurrentIndex = ItemPtr->GetCurrentSelectedIndex();
	}

	return bSelectionChanged;
}

bool ComboBoxForEnum(ImStr Label, int64& InOutIdx, UEnum* EnumPtr, ImSlateComboFlags_ Flags, const ImVec2& InSize)
{
	check(EnumPtr);

	auto ItemPtr = Item<SImVirtualComboBox>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		FString LabelStr = FString(Label.Len(), Label.GetData());
		TSharedRef<SImVirtualComboBox> WidgetRef = ImFactoryCreate<UImVirtualComboButton>();
		SetDesiredSize(WidgetRef, InSize);

		auto DataBinding = MakeShared<FImListDataComboImpl>();
		DataBinding->Init();
		DataBinding->SetMultiSelect(false);
		DataBinding->SetEnableSearchBox(false);
		auto& DataArr = DataBinding->GetCurData();
		for (int32 NameIndex = 0; NameIndex < EnumPtr->NumEnums() - 1; ++NameIndex)
		{
			auto Val = EnumPtr->GetValueByIndex(NameIndex);
			auto Text = EnumPtr->GetDisplayNameTextByValue(Val);

			DataArr.Add(MakeShared<ImSlate::FImListDataComboData>(Text, Val));
		}
		DataBinding->Reload(DataArr, false);

		DataBinding->ClearCurrentSelection();
		WidgetRef->SetDataStore(DataBinding);
		// WidgetRef->SetSearchBoxDisplay(bHasSearchBox);
		WidgetRef->SetEnabled(!(Flags & ImSlateComboFlags_ReadOnly));
		DataBinding->SetCurrentSelected((int32)InOutIdx);
		return WidgetRef;
	});

	bool bSelectionChanged = false;
	if (ItemPtr)
	{
		if (ItemPtr->IsEnabled())
		{
			if (ItemPtr->IsEnabled() == !!(Flags & ImSlateInputTextFlags_ReadOnly))
				ItemPtr->SetEnabled(!(Flags & ImSlateComboFlags_ReadOnly));
			if (auto& bValueChanged = ItemPtr->GetValueChangedRef())
			{
				bSelectionChanged = true;
				bValueChanged = false;
			}
		}
		if (auto ValueRef = ItemPtr->GetDataStore(); !bSelectionChanged)
		{
			ValueRef->SetCurrentSelected((int32)InOutIdx);
		}

		auto CurIdx = ItemPtr->GetCurrentSelectedIndex();
		InOutIdx = CurIdx < 0 ? -1 : CurIdx;
	}
	return bSelectionChanged;
}

// List Box
struct FListBoxItem
{
public:
	FListBoxItem(const FString& InData, int32 InIndex)
		: Data(InData)
		, Index(InIndex)
	{
	}
	FString Data = TEXT("");
	int32 Index = -1;
};

struct FListBoxMeta
	: public TSharedFromThis<FListBoxMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FListBoxMeta, ISlateMetaData)
public:
	TArray<TSharedPtr<FListBoxItem>> Source;
	int32 SelectedItem = -1;
	bool bSetSelected = false;
};

typedef SListView_Sized<TSharedPtr<FListBoxItem>> ListBoxType;

static TArray<TSharedPtr<FListBoxItem>> StringToSource_ListBox(const TArray<FString>& InSource)
{
	TArray<TSharedPtr<FListBoxItem>> ListSource;
	for (int32 i = 0; i < InSource.Num(); ++i)
	{
		ListSource.Add(MakeShared<FListBoxItem>(InSource[i], i));
	}
	return ListSource;
}

bool ListBox(ImStr Label, int32& SelectedItem, const TArray<FString>& InSource, const ImVec2& InSize)
{
	if (InSource.Num() <= 0)
		return false;

	auto ItemPtr = Item<ListBoxType>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		auto Meta = MakeShared<FListBoxMeta>();
		auto MetaPtr = &Meta.Get();

		Meta->Source = StringToSource_ListBox(InSource);

		TSharedRef<ListBoxType> WidgetRef = SNew(ListBoxType)
											.ListItemsSource(&Meta->Source)
											.OnGenerateRow_Lambda([](TSharedPtr<FListBoxItem> InItem, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow> {
												if (!InItem.IsValid())
												{
													return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
													[
														SNullWidget::NullWidget
													];
												}

												TSharedRef<STextBlock> TextWidgetRef = ImFactoryCreate<UImTextBlock>();
												TextWidgetRef->SetText(FText::FromString(InItem->Data));

												return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
												[
													TextWidgetRef
												];
											})
											.Visibility(EVisibility::Visible)
											.SelectionMode(ESelectionMode::Single)
											.OnMouseButtonClick_Lambda([MetaPtr](TSharedPtr<FListBoxItem> InItem) {
												if (!InItem.IsValid())
													return;

												MetaPtr->SelectedItem = InItem->Index;
												MetaPtr->bSetSelected = true;
											})
											.OnMouseButtonDoubleClick_Lambda([MetaPtr](TSharedPtr<FListBoxItem> InItem) {
												if (!InItem.IsValid())
													return;

												MetaPtr->SelectedItem = InItem->Index;
												MetaPtr->bSetSelected = true;
											})
											.AllowOverscroll(EAllowOverscroll::Yes)
											.ExternalScrollbar(SNew(SScrollBar));

		WidgetRef->AddMetadata(Meta);
		SetDesiredSize(WidgetRef, InSize);

		return WidgetRef;
	});
	// Return
	auto RetVal = false;
	if (auto Meta = GetMetaData<FListBoxMeta>(ItemPtr))
	{
		RetVal = Meta->bSetSelected;
		Meta->bSetSelected = false;
		SelectedItem = Meta->SelectedItem;
	}
	return RetVal;
}

bool VirtualList(ImStr Label, const TSharedRef<IImSlateListData>& InDataStore, const ImVec2& InSize)
{
	auto ItemPtr = Item<SImSlateVirtualList>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		TSharedRef<SImSlateVirtualList> WidgetRef = SNew(SImSlateVirtualList);
		SetDesiredSize(WidgetRef, InSize);
		WidgetRef->SetData(InDataStore);
		return WidgetRef;
	});

	if (ItemPtr)
	{
		ItemPtr->SetData(InDataStore);
		return true;
	}
	return false;
}

bool ListView_String(ImStr Label, TSharedPtr<FString>& InOutSelected, const TArray<TSharedPtr<FString>>& InSource, const ImVec2& InSize /*= ImVec2(0, 0)*/, bool bClearSelection /* = false*/)
{
	if (InSource.Num() <= 0)
		return false;

	auto DataBinding = MakeShared<TImSlateDataStorage<FString>>();
	DataBinding->SetDataBindding([](TSharedPtr<FString> InData, TSharedPtr<SWidget>& OutWidget) {
		if (!InData.IsValid())
		{
			OutWidget = SNullWidget::NullWidget;
			return;
		}

		TSharedRef<STextBlock> WidgetRef = ImFactoryCreate<UImTextBlock>();
		WidgetRef->SetText(FText::FromString(*InData));

		OutWidget = WidgetRef;
	});

	return ListView<FString>(Label, InOutSelected, InSource, DataBinding, InSize, bClearSelection);
}

ImSliderStatus_ InputVector(ImStr Label,
								 FVector& ValRef,
								 FVector ValMin,
								 FVector ValMax,
								 float Step,
								 float StepFast,
								 int32 NumDecimals,
								 bool bResetState,
								 ImSlateInputTextFlags_ Flags,
								 const ImVec2& InSize)
{
	ImSliderStatus_ StateOut = ImSlate::ImSliderStatus_Normal;
	IM_SLATE_SCOPE(Label);
	auto StateX = ImSlate::InputFloat("X", ValRef.X, ValMin.X, ValMax.X, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StateX)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StateX;
		}
		break;
		default:
			break;
	}
	ImSlate::SameLine();
	auto StateY = ImSlate::InputFloat("Y", ValRef.Y, ValMin.Y, ValMax.Y, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StateY)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StateY;
		}
		break;
		default:
			break;
	}
	ImSlate::SameLine();
	auto StateZ = ImSlate::InputFloat("Z", ValRef.Z, ValMin.Z, ValMax.Z, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StateZ)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StateZ;
		}
		break;
		default:
			break;
	}
	return StateOut;
}

ImSliderStatus_ InputRotator(ImStr Label,
								  FRotator& ValRef,
								  FRotator ValMin /*= FRotator(-180.f, -180.f, -180.f) */,
								  FRotator ValMax /*= FRotator(180.f, 180.f, 180.f)*/,
								  float Step /*= 0.0f*/,
								  float StepFast /*= 0.0f*/,
								  int32 NumDecimals /*= 3*/,
								  bool bResetState /*= false*/,
								  ImSlateInputTextFlags_ Flags /*= ImSlateInputTextFlags_None*/,
								  const ImVec2& InSize /*= ImVec2(0, 0)*/)
{
	ImSliderStatus_ StateOut = ImSlate::ImSliderStatus_Normal;
	IM_SLATE_SCOPE(Label);
	auto StatePitch = ImSlate::InputFloat("Pitch", ValRef.Pitch, ValMin.Pitch, ValMax.Pitch, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StatePitch)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StatePitch;
		}
		break;
		default:
			break;
	}
	ImSlate::SameLine();
	auto StateYaw = ImSlate::InputFloat("Yaw", ValRef.Yaw, ValMin.Yaw, ValMax.Yaw, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StateYaw)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StateYaw;
		}
		break;
		default:
			break;
	}
	ImSlate::SameLine();
	auto StateRoll = ImSlate::InputFloat("Roll", ValRef.Roll, ValMin.Roll, ValMax.Roll, Step, StepFast, NumDecimals, bResetState, ImSlate::ImSlateFloatFlags_None, ImVec2(InSize.X / 3.f, InSize.Y));
	switch (StateRoll)
	{
		case ImSlate::ImSliderStatus_BeginSlider:
		case ImSlate::ImSliderStatus_EndSlider:
		case ImSlate::ImSliderStatus_ValueChanged:
		case ImSlate::ImSliderStatus_Committed:
		{
			StateOut = StateRoll;
		}
		break;
		default:
			break;
	}
	return StateOut;
}

//////////////////////////////////////////////////////////////////////////
// Asset/Class Pickers — thin bridge to SImAssetPicker / SImClassPicker
//////////////////////////////////////////////////////////////////////////

struct FAssetPickerMeta
	: public TSharedFromThis<FAssetPickerMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FAssetPickerMeta, ISlateMetaData)
public:
	bool bHasChanged = false;
	FSoftObjectPath ChangedPath;
};

bool AssetPicker(ImStr Label, FSoftObjectPath& InOutPath, UClass* FilterClass, const ImVec2& InSize)
{
	auto ItemPtr = Item<SImAssetPicker>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		auto Meta = MakeShared<FAssetPickerMeta>();
		Meta->ChangedPath = InOutPath;
		auto Ptr = &Meta.Get();

		TSharedRef<SImAssetPicker> WidgetRef = SNew(SImAssetPicker)
			.FilterClass(FilterClass)
			.ObjectPath(InOutPath.ToString())
			.OnAssetPathChanged_Lambda([Ptr](const FSoftObjectPath& NewPath) {
				Ptr->ChangedPath = NewPath;
				Ptr->bHasChanged = true;
			});

		SetDesiredSize(WidgetRef, InSize);
		WidgetRef->AddMetadata(Meta);
		return WidgetRef;
	});

	bool RetVal = false;
	if (auto Meta = GetMetaData<FAssetPickerMeta>(ItemPtr))
	{
		if (Meta->bHasChanged)
		{
			RetVal = true;
			InOutPath = Meta->ChangedPath;
			Meta->bHasChanged = false;
		}
		else
		{
			ItemPtr->SetObjectPath(InOutPath.ToString());
		}
	}
	return RetVal;
}

struct FClassPickerMeta
	: public TSharedFromThis<FClassPickerMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FClassPickerMeta, ISlateMetaData)
public:
	bool bHasChanged = false;
	FSoftClassPath ChangedPath;
};

bool ClassPicker(ImStr Label, FSoftClassPath& InOutPath, UClass* MetaClass, const ImVec2& InSize)
{
	auto ItemPtr = Item<SImClassPicker>(Label, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = true;
		InItem.StretchValue = 1.f;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;

		auto Meta = MakeShared<FClassPickerMeta>();
		Meta->ChangedPath = InOutPath;
		auto Ptr = &Meta.Get();

		const UClass* SelectedClass = InOutPath.TryLoadClass<UObject>();

		TSharedRef<SImClassPicker> WidgetRef = SNew(SImClassPicker)
			.MetaClass(MetaClass)
			.SelectedClass(SelectedClass)
			.OnClassPathChanged_Lambda([Ptr](const FSoftClassPath& NewPath) {
				Ptr->ChangedPath = NewPath;
				Ptr->bHasChanged = true;
			});

		SetDesiredSize(WidgetRef, InSize);
		WidgetRef->AddMetadata(Meta);
		return WidgetRef;
	});

	bool RetVal = false;
	if (auto Meta = GetMetaData<FClassPickerMeta>(ItemPtr))
	{
		if (Meta->bHasChanged)
		{
			RetVal = true;
			InOutPath = Meta->ChangedPath;
			Meta->bHasChanged = false;
		}
		else
		{
			ItemPtr->SetClassPath(InOutPath);
		}
	}
	return RetVal;
}

#if defined(IMSLATE3D_API)
// Stash the arrow's last drag write-back so GizmoArrow can report it (immediate-mode return-value range).
struct FGizmoArrowMeta
	: public TSharedFromThis<FGizmoArrowMeta>
	, public ISlateMetaData
{
	SLATE_METADATA_TYPE(FGizmoArrowMeta, ISlateMetaData)
public:
	bool    bChanged = false;
	FVector NewPos = FVector::ZeroVector;
};

bool GizmoArrow(ImStr Id, FVector& Pos, const FVector& WorldAxis, const FLinearColor& Color)
{
	auto* ItemPtr = Item<SImSlate3DPlaneArrow>(Id, [&](FItemSlotPod& InItem) {
		InItem.bFillWidth = false;
		InItem.HAlignment = HAlign_Fill;
		InItem.VAlignment = VAlign_Fill;
		InItem.StretchValue = 1.f;

		auto Meta = MakeShared<FGizmoArrowMeta>();
		FGizmoArrowMeta* MetaPtr = &Meta.Get();
		TSharedRef<SImSlate3DPlaneArrow> Arrow = SNew(SImSlate3DPlaneArrow)
			.WorldAxis(WorldAxis)
			.Color(Color)
			.OnPositionChanged_Lambda([MetaPtr](const FVector& NewWorldPos)
			{
				MetaPtr->bChanged = true;
				MetaPtr->NewPos = NewWorldPos;
			});
		Arrow->AddMetadata(Meta);
		return Arrow;
	});

	bool RetVal = false;
	if (ItemPtr)
	{
		// Wire the arrow to the current window's 3D box each frame (owner provides projector/anchor; cheap).
		if (ImSlateContext* g = GImSlate)
		{
			if (g->CurrentWindow)
			{
				ItemPtr->SetOwnerBox(g->CurrentWindow->GetContent3DBox());
			}
		}
		if (auto Meta = GetMetaData<FGizmoArrowMeta>(ItemPtr))
		{
			if (Meta->bChanged)
			{
				Pos = Meta->NewPos;       // write the dragged position back
				Meta->bChanged = false;
				RetVal = true;
			}
		}
	}
	return RetVal;
}
#endif  // defined(IMSLATE3D_API)

}  // namespace ImSlate
