// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImRichTextBlock.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/RichTextBlockDecorator.h"
#include "Engine/Font.h"
#include "RenderingThread.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Text/SRichTextBlock.h"
#if ENGINE_MAJOR_VERSION >= 5
#include "RenderDeferredCleanup.h"
#endif

namespace ImSlate
{
template<class ObjectType>
struct FDeferredDeletor : public FDeferredCleanupInterface
{
public:
	FDeferredDeletor(ObjectType* InInnerObjectToDelete)
		: InnerObjectToDelete(InInnerObjectToDelete)
	{
	}

	virtual ~FDeferredDeletor() { delete InnerObjectToDelete; }

private:
	ObjectType* InnerObjectToDelete;
};

template<typename ObjectType>
FORCEINLINE auto MakeShareableDeferredCleanup(ObjectType* InObject)
{
	return MakeShareable(InObject, [](ObjectType* ObjectToDelete) { BeginCleanup(new FDeferredDeletor<ObjectType>(ObjectToDelete)); });
}
}  // namespace ImSlate

TSharedRef<SRichTextBlock> UImRichTextBlock::ConstructImWidget() const
{
	auto MutableThis = const_cast<UImRichTextBlock*>(this);
	auto& MyStyleInstance = MutableThis->StyleInstance;
	auto DefaultTextStylePtr = &GetDefaultTextStyle();
	if (!MyStyleInstance.IsValid())
	{
		MyStyleInstance = ImSlate::MakeShareableDeferredCleanup(new FSlateStyleSet(TEXT("RichTextStyle")));
		if (GetTextStyleSet() && GetTextStyleSet()->GetRowStruct()->IsChildOf(FRichTextStyleRow::StaticStruct()))
		{
			for (const auto& Entry : GetTextStyleSet()->GetRowMap())
			{
				FName SubStyleName = Entry.Key;
				FRichTextStyleRow* RichTextStyle = (FRichTextStyleRow*)Entry.Value;

				if (SubStyleName == FName(TEXT("Default")))
				{
					DefaultTextStylePtr = &RichTextStyle->TextStyle;
				}

				MyStyleInstance->Set(SubStyleName, RichTextStyle->TextStyle);
			}
		}

		for (TSubclassOf<URichTextBlockDecorator> DecoratorClass : DecoratorClasses)
		{
			if (UClass* ResolvedClass = DecoratorClass.Get())
			{
				if (!ResolvedClass->HasAnyClassFlags(CLASS_Abstract))
				{
					URichTextBlockDecorator* Decorator = NewObject<URichTextBlockDecorator>(MutableThis, ResolvedClass);
					MutableThis->InstanceDecorators.Add(Decorator);
				}
			}
		}
	}

	TArray<TSharedRef<class ITextDecorator>> CreatedDecorators;
	for (URichTextBlockDecorator* Decorator : MutableThis->InstanceDecorators)
	{
		if (Decorator)
		{
			TSharedPtr<ITextDecorator> TextDecorator = Decorator->CreateDecorator(MutableThis);
			if (TextDecorator.IsValid())
			{
				CreatedDecorators.Add(TextDecorator.ToSharedRef());
			}
		}
	}

	auto WidgetRef = SNew(SRichTextBlock)
						.TextStyle(bOverrideDefaultStyle ? &GetDefaultTextStyleOverride() : DefaultTextStylePtr)
						.Marshaller(FRichTextLayoutMarshaller::Create(MutableThis->CreateMarkupParser(), MutableThis->CreateMarkupWriter(), CreatedDecorators, MyStyleInstance.Get()))
						.CreateSlateTextLayout(FCreateSlateTextLayout::CreateWeakLambda(MutableThis, [](SWidget* InOwner, const FTextBlockStyle& InDefaultTextStyle) { return FSlateTextLayout::Create(InOwner, InDefaultTextStyle); }));

	WidgetRef->SetText(GetText());
	WidgetRef->SetTransformPolicy(GetTransformPolicy());
	WidgetRef->SetMinDesiredWidth(GetMinDesiredWidth());
	MutableThis->UTextLayoutWidget::SynchronizeTextLayoutProperties(*WidgetRef);

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	WidgetRef->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	return WidgetRef;
}

TSharedRef<SWidget> UImRichTextBlock::RebuildWidget()
{
	UpdateStyleData();

	TArray<TSharedRef<class ITextDecorator>> CreatedDecorators;
	CreateDecorators(CreatedDecorators);

	TextMarshaller = FRichTextLayoutMarshaller::Create(CreateMarkupParser(), CreateMarkupWriter(), CreatedDecorators, StyleInstance.Get());

	MyRichTextBlock = SNew(SRichTextBlock)
						.TextStyle(bOverrideDefaultStyle ? &GetDefaultTextStyleOverride() : &GetDefaultTextStyle())
						.Marshaller(TextMarshaller)
						.CreateSlateTextLayout(FCreateSlateTextLayout::CreateWeakLambda(this, [this](SWidget* InOwner, const FTextBlockStyle& InDefaultTextStyle) mutable {
							TextLayout = FSlateTextLayout::Create(InOwner, InDefaultTextStyle);
							return StaticCastSharedPtr<FSlateTextLayout>(TextLayout).ToSharedRef();
						}));

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyRichTextBlock->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	return MyRichTextBlock.ToSharedRef();
}

void UImDialogueBox::PlayLine(const FText& InLine)
{
	check(GetWorld());

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(LetterTimer);

	CurrentLine = InLine;
	CurrentLetterIndex = 0;
	CachedLetterIndex = 0;
	CurrentSegmentIndex = 0;
	MaxLetterIndex = 0;
	Segments.Empty();
	CachedSegmentText.Empty();

	if (CurrentLine.IsEmpty())
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::GetEmpty());
		}

		bHasFinishedPlaying = true;
		OnLineFinishedPlaying();

		SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::GetEmpty());
		}

		bHasFinishedPlaying = false;

		FTimerDelegate Delegate;
		Delegate.BindUObject(this, &ThisClass::PlayNextLetter);

		TimerManager.SetTimer(LetterTimer, Delegate, LetterPlayTime, true);

		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UImDialogueBox::SkipToLineEnd()
{
	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.ClearTimer(LetterTimer);

	CurrentLetterIndex = MaxLetterIndex - 1;
	if (IsValid(LineText))
	{
		LineText->SetText(FText::FromString(CalculateSegments()));
	}

	bHasFinishedPlaying = true;
	OnLineFinishedPlaying();
}

TSharedRef<SWidget> UImDialogueBox::RebuildWidget()
{
	// This will only be false in the following conditions:
	// 1) This is the lowest-level class.
	// 2) You are not extending this class with a UMG widget.
	// 3) You did not already set the root component.
	// This code is here (instead of ignoreing the RootWidget null check)
	// is to allow you to extend this calss in UMG without having to update your code

	if (!Cast<UWidgetBlueprintGeneratedClass>(GetClass()) && WidgetTree && !WidgetTree->RootWidget)
	{
		// Construct the root widget. Root widgets are UCanvasPanels by default in UMG.
		auto RootPanel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootWidget"));
		WidgetTree->RootWidget = RootPanel;

		LineText = WidgetTree->ConstructWidget<UImRichTextBlock>(UImRichTextBlock::StaticClass(), TEXT("LineText"));
		if (auto LineTextSlot = Cast<UCanvasPanelSlot>(RootPanel->AddChild(LineText)))
		{
			LineTextSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			LineTextSlot->SetAlignment(FVector2D(.5f, .5f));
			LineTextSlot->SetOffsets(FVector2D(0.f, 0.f));
			LineTextSlot->SetSize(FVector2D(0.f, 0.f));
		}
	}

	// The root widget needs to be set before calling Super::RebuildWidget().
	// The reason for this is that UWdiget::RebuildWidget() calls TakeWidget() and the
	// constructor which are required for propert root widget initialization.
	// We cannot simply call TakeWidget() here.
	return Super::RebuildWidget();
}

void UImDialogueBox::PlayNextLetter()
{
	if (Segments.Num() == 0)
	{
		CalculateWrappedString();
	}

	FString WrappedString = CalculateSegments();

	// TODO: How do we keep indexing of text i18n-friendly?
	if (CurrentLetterIndex < MaxLetterIndex)
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::FromString(WrappedString));
		}

		OnPlayLetter();
		++CurrentLetterIndex;
	}
	else
	{
		if (IsValid(LineText))
		{
			LineText->SetText(FText::FromString(CalculateSegments()));
		}

		FTimerManager& TimerManager = GetWorld()->GetTimerManager();
		TimerManager.ClearTimer(LetterTimer);

		FTimerDelegate Delegate;
		Delegate.BindUObject(this, &ThisClass::SkipToLineEnd);

		TimerManager.SetTimer(LetterTimer, Delegate, EndHoldTime, false);
	}
}

// TODO: Need to recalculate this + CalculateSegments when the text box gets resized.
void UImDialogueBox::CalculateWrappedString()
{
	if (IsValid(LineText) && LineText->GetTextLayout().IsValid())
	{
		TSharedPtr<FSlateTextLayout> Layout = LineText->GetTextLayout();
		TSharedPtr<FRichTextLayoutMarshaller> Marshaller = LineText->GetTextMarshaller();

		const FGeometry& TextBoxGeometry = LineText->GetCachedGeometry();
		const FVector2D TextBoxSize = TextBoxGeometry.GetLocalSize();

		Layout->SetWrappingWidth(TextBoxSize.X);
		Marshaller->SetText(CurrentLine.ToString(), *Layout.Get());
		Layout->UpdateIfNeeded();

		bool bHasWrittenText = false;
		for (const FTextLayout::FLineView& View : Layout->GetLineViews())
		{
			const FTextLayout::FLineModel& Model = Layout->GetLineModels()[View.ModelIndex];

			for (TSharedRef<ILayoutBlock> Block : View.Blocks)
			{
				TSharedRef<IRun> Run = Block->GetRun();

				FDialogueTextSegment Segment;
				Run->AppendTextTo(Segment.Text, Block->GetTextRange());

				// HACK: For some reason image decorators (and possibly other decorators that don't
				// have actual text inside them) result in the run containing a zero width space instead of
				// nothing. This messes up our checks for whether the text is empty or not, which doesn't
				// have an effect on image decorators but might cause issues for other custom ones.
				if (Segment.Text.Len() == 1 && Segment.Text[0] == 0x200B)
				{
					Segment.Text.Empty();
				}

				Segment.RunInfo = Run->GetRunInfo();
				Segments.Add(Segment);

				// A segment with a named run should still take up time for the typewriter effect.
				MaxLetterIndex += FMath::Max(Segment.Text.Len(), Segment.RunInfo.Name.IsEmpty() ? 0 : 1);

				if (!Segment.Text.IsEmpty() || !Segment.RunInfo.Name.IsEmpty())
				{
					bHasWrittenText = true;
				}
			}

			if (bHasWrittenText)
			{
				Segments.Add(FDialogueTextSegment{TEXT("\n")});
				++MaxLetterIndex;
			}
		}

		Layout->SetWrappingWidth(0);
		LineText->SetText(LineText->GetText());
	}
	else
	{
		Segments.Add(FDialogueTextSegment{CurrentLine.ToString()});
		MaxLetterIndex = Segments[0].Text.Len();
	}
}

FString UImDialogueBox::CalculateSegments()
{
	FString Result = CachedSegmentText;

	int32 Idx = CachedLetterIndex;
	while (Idx <= CurrentLetterIndex && CurrentSegmentIndex < Segments.Num())
	{
		const FDialogueTextSegment& Segment = Segments[CurrentSegmentIndex];
		if (!Segment.RunInfo.Name.IsEmpty())
		{
			Result += FString::Printf(TEXT("<%s"), *Segment.RunInfo.Name);

			if (Segment.RunInfo.MetaData.Num() > 0)
			{
				for (const TTuple<FString, FString>& MetaData : Segment.RunInfo.MetaData)
				{
					Result += FString::Printf(TEXT(" %s=\"%s\""), *MetaData.Key, *MetaData.Value);
				}
			}

			if (Segment.Text.IsEmpty())
			{
				Result += TEXT("/>");
				++Idx;  // This still takes up an index for the typewriter effect.
			}
			else
			{
				Result += TEXT(">");
			}
		}

		bool bIsSegmentComplete = true;
		if (!Segment.Text.IsEmpty())
		{
			int32 LettersLeft = CurrentLetterIndex - Idx + 1;
			bIsSegmentComplete = LettersLeft >= Segment.Text.Len();
			LettersLeft = FMath::Min(LettersLeft, Segment.Text.Len());
			Idx += LettersLeft;

			Result += Segment.Text.Mid(0, LettersLeft);

			if (!Segment.RunInfo.Name.IsEmpty())
			{
				Result += TEXT("</>");
			}
		}

		if (bIsSegmentComplete)
		{
			CachedLetterIndex = Idx;
			CachedSegmentText = Result;
			++CurrentSegmentIndex;
		}
		else
		{
			break;
		}
	}

	return Result;
}
