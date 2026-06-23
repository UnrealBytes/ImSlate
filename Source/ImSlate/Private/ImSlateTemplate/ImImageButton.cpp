// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImImageButton.h"

#include "Components/ButtonSlot.h"

//

TSharedRef<ImSlate::SImageButton> UImImageButton::ConstructImWidget() const
{
	TSharedPtr<ImSlate::SImageButton> MyStealButton = SNew(ImSlate::SImageButton)
												//.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
												//.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
												//.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
												//.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
												//.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
												.ButtonStyle(&GetStyle())
												.ClickMethod(GetClickMethod())
												.TouchMethod(GetTouchMethod())
												.PressMethod(GetPressMethod())
												.IsFocusable(GetIsFocusable());

	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(MyStealButton.ToSharedRef());
	}

	// UWidget

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealButton->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealButton->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealButton->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealButton->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealButton->SetClipping(GetClipping());

	MyStealButton->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealButton->ForceVolatile(bIsVolatile);
	MyStealButton->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealButton->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealButton->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealButton->SetRenderTransformPivot(GetRenderTransformPivot());

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

	MyStealButton->SetColorAndOpacity(GetColorAndOpacity());
	MyStealButton->SetBorderBackgroundColor(GetBackgroundColor());

	return MyStealButton.ToSharedRef();
}
