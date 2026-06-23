// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImBorder.h"

//
#include "Components/BorderSlot.h"
#include "Widgets/Layout/SBorder.h"

TSharedRef<SBorder> UImBorder::ConstructImWidget() const
{
	auto MyStealWidget = SNew(SBorder)
							.FlipForRightToLeftFlowDirection(bFlipForRightToLeftFlowDirection);

	if (GetChildrenCount() > 0)
	{
		Cast<UBorderSlot>(GetContentSlot())->BuildSlot(MyBorder.ToSharedRef());
	}

#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealWidget->AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif
#if 0
	MyStealWidget->SetOnMouseButtonDown(BIND_UOBJECT_DELEGATE(FPointerEventHandler, HandleMouseButtonDown));
	MyStealWidget->SetOnMouseButtonUp(BIND_UOBJECT_DELEGATE(FPointerEventHandler, HandleMouseButtonUp));
	MyStealWidget->SetOnMouseMove(BIND_UOBJECT_DELEGATE(FPointerEventHandler, HandleMouseMove));
	MyStealWidget->SetOnMouseDoubleClick(BIND_UOBJECT_DELEGATE(FPointerEventHandler, HandleMouseDoubleClick));
#endif
#if 0
	TAttribute<FLinearColor> ContentColorAndOpacityBinding = PROPERTY_BINDING(FLinearColor, ContentColorAndOpacity);
	TAttribute<FSlateColor> BrushColorBinding = OPTIONAL_BINDING_CONVERT(FLinearColor, BrushColor, FSlateColor, ConvertLinearColorToSlateColor);
	TAttribute<const FSlateBrush*> ImageBinding = OPTIONAL_BINDING_CONVERT(FSlateBrush, Background, const FSlateBrush*, ConvertImage);
	MyStealWidget->SetBorderBackgroundColor(BrushColorBinding);
	MyStealWidget->SetBorderImage(ImageBinding);
	MyStealWidget->SetColorAndOpacity(ContentColorAndOpacityBinding);
#else
	MyStealWidget->SetBorderBackgroundColor(GetBrushColor());
	MyStealWidget->SetBorderImage(&Background);
	MyStealWidget->SetColorAndOpacity(GetContentColorAndOpacity());

#endif
	MyStealWidget->SetClipping(GetClipping());
	MyStealWidget->SetPadding(GetPadding());
	MyStealWidget->SetDesiredSizeScale(GetDesiredSizeScale());
	MyStealWidget->SetShowEffectWhenDisabled(GetShowEffectWhenDisabled());

	MyStealWidget->ForceVolatile(bIsVolatile);
	MyStealWidget->SetRenderOpacity(GetRenderOpacity());
	{
		if (bOverride_Cursor /*|| CursorDelegate.IsBound()*/)
		{
			MyStealWidget->SetCursor(GetCursor());
		}
		//MyStealWidget->SetEnabled(BITFIELD_PROPERTY_BINDING(bIsEnabled));
		MyStealWidget->SetVisibility(ConvertVisibility(GetVisibility()));
	}

	if (GetRenderTransform().IsIdentity())
	{
		MyStealWidget->SetRenderTransform(TOptional<FSlateRenderTransform>());
	}
	else
	{
		MyStealWidget->SetRenderTransform(GetRenderTransform().ToSlateRenderTransform());
	}
	MyStealWidget->SetRenderTransformPivot(GetRenderTransformPivot());

	return MyStealWidget;
}
