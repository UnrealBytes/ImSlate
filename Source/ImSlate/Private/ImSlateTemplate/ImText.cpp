// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImText.h"

//
#include "SlateCore.h"

#include "Widgets/SInvalidationPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<STextBlock> UImTextBlock::ConstructImWidget() const
{
	auto MyStealTextBlock = SNew(STextBlock)
							.SimpleTextMode(bSimpleTextMode);

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealTextBlock->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealTextBlock->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealEditableText->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealTextBlock->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealTextBlock->SetClipping(GetClipping());

	MyStealTextBlock->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealTextBlock->ForceVolatile(bIsVolatile);
	MyStealTextBlock->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealTextBlock->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealTextBlock->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealTextBlock->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (ToolTipWidget != nullptr)
	// 	{
	// 		TSharedRef<SToolTip> ToolTip = SNew(SToolTip)
	// 										.TextMargin(FMargin(0))
	// 										.BorderImage(nullptr)
	// 										[
	// 											ToolTipWidget->TakeWidget()
	// 										];
	//
	// 		MyStealEditableText->SetToolTip(GetToolTip());
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealEditableText->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	// UWidget

	// MyStealTextBlock->SetText(TextBinding);
	MyStealTextBlock->SetFont(ImSlate::ScaleImSlateFont(GetFont()));
	MyStealTextBlock->SetStrikeBrush(&GetStrikeBrush());
	MyStealTextBlock->SetColorAndOpacity(GetColorAndOpacity());
	MyStealTextBlock->SetShadowOffset(GetShadowOffset());
	MyStealTextBlock->SetShadowColorAndOpacity(GetShadowColorAndOpacity());
	MyStealTextBlock->SetMinDesiredWidth(GetMinDesiredWidth());
	MyStealTextBlock->SetTransformPolicy(GetTextTransformPolicy());
	const_cast<UImTextBlock*>(this)->Super::SynchronizeTextLayoutProperties(*MyStealTextBlock);

	return MyStealTextBlock;
}
