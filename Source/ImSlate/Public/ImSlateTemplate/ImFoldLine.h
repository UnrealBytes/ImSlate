// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "Components/TextBlock.h"
#include "ImSlateFactory.h"
#include "Widgets/Text/STextBlock.h"

//
#include "ImFoldLine.generated.h"

namespace ImSlate
{

class IMSLATE_API SImFoldLine : public STextBlock
{
public:
	using FArguments = STextBlock::FArguments;
	void Construct(const FArguments& Args, bool bInIsFolded = true)
	{
		bIsFolded = bInIsFolded;
		STextBlock::Construct(Args);
		SetVisibility(EVisibility::Visible);  // STextBlock defaults to HitTestInvisible — make clickable
	}
	bool GetIsFolded() const { return bIsFolded; }
	void SetFold(bool bInIsFolded = true) { bIsFolded = bInIsFolded; }

	// Override SetText to prepend fold indicator
	void SetTextWithFoldIndicator(const FText& InText);

protected:
	bool bIsFolded = true;
	FText OriginalText;

	// Toggle on RELEASE (not press), and only for a tap (negligible movement). Pressing without
	// consuming the event lets a press-and-drag fall through to the panel's scroll/pan, so dragging
	// up/down over a fold header scrolls the list instead of toggling it.
	FVector2D PressScreenPos = FVector2D::ZeroVector;
	bool bPressActive = false;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled)
		const override;
	friend class ::UImFoldLine;  // ::-qualified: UImFoldLine is a global UCLASS, not ImSlate::UImFoldLine
	FInvalidatableBrushAttribute BorderImage;
};

}  // namespace ImSlate

UCLASS(BlueprintType)
class IMSLATE_API UImFoldLine
	: public UTextBlock
	, public TImFactory<ImSlate::SImFoldLine>
{
	GENERATED_BODY()
public:
	TSharedRef<ImSlate::SImFoldLine> ConstructImWidget() const;
	virtual TSharedRef<SWidget> RebuildWidget() override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Appearance, meta = (DisplayName = "BackgroudBrush"))
	FSlateBrush Background;

protected:
	IM_SLATE_PALETTECATEGORY()
};
