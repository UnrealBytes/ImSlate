// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImImage.h"

//
#include "SlateCore.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/SInvalidationPanel.h"

TSharedRef<SImage> UImImage::ConstructImWidget() const
{
	auto MyImImage = SNew(SImage)
						.FlipForRightToLeftFlowDirection(ShouldFlipForRightToLeftFlowDirection());

	// UWidget Begin

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyImImage->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyImImage->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealEditableText->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyImImage->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyImImage->SetClipping(GetClipping());

	MyImImage->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyImImage->ForceVolatile(bIsVolatile);
	MyImImage->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyImImage->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyImImage->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyImImage->SetRenderTransformPivot(GetRenderTransformPivot());

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

	// UWidget End

	MyImImage->SetImage(ConvertImage(GetBrush()));
	MyImImage->SetColorAndOpacity(GetColorAndOpacity());
	//MyImage->SetOnMouseButtonDown(BIND_UOBJECT_DELEGATE(FPointerEventHandler, HandleMouseButtonDown));

	return MyImImage;
}
