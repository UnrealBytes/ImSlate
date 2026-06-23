// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImEditableTextBox.h"

#include "Widgets/Input/SEditableTextBox.h"

namespace ImSlate
{

void SImEditableTextBox::Construct(const FArguments& InArgs)
{
	SEditableTextBox::Construct(InArgs);
	TAttribute<const FSlateBrush*> ImageAttr(this, &SImEditableTextBox::GetBorderImage);
#if ENGINE_MAJOR_VERSION >= 5
	SetBorderImage(ImageAttr);
#else
	BorderImage = ImageAttr;
#endif
}

void SImEditableTextBox::SetBackgroundImageError(const FSlateBrush* ImageError)
{
	BorderImageError = ImageError;
}

const FSlateBrush* SImEditableTextBox::GetBorderImage() const
{
	if (bInErrorState && BorderImageError)
	{
		return BorderImageError;
	}
	else if (EditableText->IsTextReadOnly())
	{
		return &Style->BackgroundImageReadOnly;
	}
	else if (EditableText->HasKeyboardFocus())
	{
		return &Style->BackgroundImageFocused;
	}
	else
	{
		if (EditableText->IsHovered())
		{
			return &Style->BackgroundImageHovered;
		}
		else
		{
			return &Style->BackgroundImageNormal;
		}
	}
}

/**
* Interface for tool tips.
*/
class FDelegateToolTipBox : public IToolTip
{
public:
	/**
	* Gets the widget that this tool tip represents.
	*
	* @return The tool tip widget.
	*/
	virtual TSharedRef<SWidget> AsWidget() override { return GetContentWidget(); }

	/**
	* Gets the tool tip's content widget.
	*
	* @return The content widget.
	*/
	virtual TSharedRef<SWidget> GetContentWidget() override
	{
		if (CachedToolTip.IsValid())
		{
			return CachedToolTip.ToSharedRef();
		}

		UWidget* Widget = ToolTipWidgetDelegate.Execute();
		if (Widget)
		{
			CachedToolTip = Widget->TakeWidget();
			return CachedToolTip.ToSharedRef();
		}

		return SNullWidget::NullWidget;
	}

	/**
	* Sets the tool tip's content widget.
	*
	* @param InContentWidget The new content widget to set.
	*/
	virtual void SetContentWidget(const TSharedRef<SWidget>& InContentWidget) override { CachedToolTip = InContentWidget; }

	/**
	* Checks whether this tool tip has no content to display right now.
	*
	* @return true if the tool tip has no content to display, false otherwise.
	*/
	virtual bool IsEmpty() const override { return !ToolTipWidgetDelegate.IsBound(); }

	/**
	* Checks whether this tool tip can be made interactive by the user (by holding Ctrl).
	*
	* @return true if it is an interactive tool tip, false otherwise.
	*/
	virtual bool IsInteractive() const override { return false; }

	virtual void OnClosed() override
	{
		//TODO Notify interface implementing widget of closure

		CachedToolTip.Reset();
	}

	virtual void OnOpening() override
	{
		//TODO Notify interface implementing widget of opening
	}

public:
	UWidget::FGetWidget ToolTipWidgetDelegate;

private:
	TSharedPtr<SWidget> CachedToolTip;
};
}  // namespace ImSlate

TSharedRef<SEditableTextBox> UImEditableTextBox::ConstructImWidget() const
{
	auto MyStealWidget = SNew(SEditableTextBox)
							 .Style(&WidgetStyle)
							 .MinDesiredWidth(GetMinimumDesiredWidth())
							 .IsCaretMovedWhenGainFocus(GetIsCaretMovedWhenGainFocus())
							 .SelectAllTextWhenFocused(GetSelectAllTextWhenFocused())
							 .RevertTextOnEscape(GetRevertTextOnEscape())
							 .ClearKeyboardFocusOnCommit(GetClearKeyboardFocusOnCommit())
							 .SelectAllTextOnCommit(GetSelectAllTextOnCommit())
							 .AllowContextMenu(AllowContextMenu)
							 //.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
							 //.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
							 .VirtualKeyboardType(EVirtualKeyboardType::AsKeyboardType(KeyboardType.GetValue()))
							 .VirtualKeyboardOptions(VirtualKeyboardOptions)
							 .VirtualKeyboardTrigger(VirtualKeyboardTrigger)
							 .VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
							 .Justification(GetJustification());

	// #if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// 	bRoutedSynchronizeProperties = true;
	// #endif

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealWidget->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealWidget->SetCursor(GetCursor());  // PROPERTY_BINDING(EMouseCursor::Type, Cursor));
		}

		//MyStealWidget->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealWidget->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	MyStealWidget->SetClipping(GetClipping());

	MyStealWidget->SetFlowDirectionPreference(GetFlowDirectionPreference());
	MyStealWidget->ForceVolatile(bIsVolatile);
	MyStealWidget->SetRenderOpacity(GetRenderOpacity());

	if (GetRenderTransform().IsIdentity())
	{
		MyStealWidget->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealWidget->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}

	MyStealWidget->SetRenderTransformPivot(GetRenderTransformPivot());

	// 	if (ToolTipWidgetDelegate.IsBound() && !IsDesignTime())
	// 	{
	// 		TSharedRef<ImSlate::FDelegateToolTip> ToolTip = MakeShared<ImSlate::FDelegateToolTip>();
	// 		ToolTip->ToolTipWidgetDelegate = ToolTipWidgetDelegate;
	// 		MyStealWidget->SetToolTip(GetToolTip());
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
	// 		MyStealWidget->SetToolTip(GetToolTip());
	// 	}
	// 	else if (!ToolTipText.IsEmpty() || ToolTipTextDelegate.IsBound())
	// 	{
	// 		MyStealWidget->SetToolTipText(PROPERTY_BINDING(FText, ToolTipText));
	// 	}

	//TAttribute<FText> TextBinding = PROPERTY_BINDING(FText, Text);
	//TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);

	MyStealWidget->SetText(GetText());
	MyStealWidget->SetHintText(GetHintText());
	MyStealWidget->SetIsReadOnly(GetIsReadOnly());
	MyStealWidget->SetIsPassword(GetIsPassword());
	MyStealWidget->SetAllowContextMenu(AllowContextMenu);
	MyStealWidget->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
	MyStealWidget->SetJustification(GetJustification());
	// TODO UMG Complete making all properties settable on SEditableTextBox

	((FShapedTextOptions&)ShapedTextOptions).SynchronizeShapedTextProperties(*MyStealWidget);

	return MyStealWidget;
}

