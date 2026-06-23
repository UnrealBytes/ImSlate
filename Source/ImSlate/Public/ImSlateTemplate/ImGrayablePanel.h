// Copyright ImSlate, Inc. All Rights Reserved.
// ImGrayablePanel - Grayscale panel using VisitTupleElements + DisabledEffect
//
// Inherits from SConstraintCanvas/UCanvasPanel to support multiple children.
// Applies DisabledEffect to all child elements for GPU-based grayscale.

#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanel.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ImSlateFactory.h"

//
#include "ImGrayablePanel.generated.h"

//////////////////////////////////////////////////////////////////////////
// SImGrayablePanel - Slate Layer (inherits SConstraintCanvas)
//////////////////////////////////////////////////////////////////////////

/**
 * Slate widget that renders children with grayscale effect.
 * Inherits from SConstraintCanvas to support multiple children with anchors.
 */
namespace ImSlate
{

class IMSLATE_API SImGrayablePanel : public SConstraintCanvas
{
public:
	using FArguments = SConstraintCanvas::FArguments;

	void Construct(const FArguments& InArgs, bool bInIsGrayed);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	void SetGrayed(bool bInGrayed);
	bool IsGrayed() const { return bIsGrayed.Get(false); }

protected:
	TAttribute<bool> bIsGrayed;
};

}  // namespace ImSlate

//////////////////////////////////////////////////////////////////////////
// UImGrayablePanel - UMG Layer (inherits UCanvasPanel)
//////////////////////////////////////////////////////////////////////////

/**
 * A canvas panel widget that renders children with grayscale effect.
 * Supports multiple children with Canvas Slot anchoring.
 *
 * Features:
 * - Property binding (bIsGrayed) and function call (SetGrayed) triggering
 * - Uses Slate's built-in DisabledEffect for true GPU-based desaturation
 * - No material or RenderTarget required
 * - Does not affect interaction logic
 * - Supports multiple children with anchors (like UCanvasPanel)
 *
 * Usage:
 *   1. Add ImGrayablePanel to your Widget Blueprint
 *   2. Place child widgets inside (supports multiple children)
 *   3. Call SetGrayed(true) or set bIsGrayed = true
 */
UCLASS(BlueprintType)
class IMSLATE_API UImGrayablePanel
	: public UCanvasPanel
	, public TImFactory<ImSlate::SImGrayablePanel>
{
	GENERATED_BODY()

public:
	UImGrayablePanel(const FObjectInitializer& ObjectInitializer);

	TSharedRef<ImSlate::SImGrayablePanel> ConstructImWidget() const;

	//~ Begin UWidget Interface
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	//~ End UWidget Interface

	/**
	 * Set grayscale state
	 * @param bGrayed - true to enable grayscale, false to restore normal
	 */
	UFUNCTION(BlueprintCallable, Category = "ImGrayablePanel")
	void SetGrayed(bool bGrayed);

	/**
	 * Get current grayscale state
	 */
	UFUNCTION(BlueprintPure, Category = "ImGrayablePanel")
	bool IsGrayed() const { return bIsGrayed; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	IM_SLATE_PALETTECATEGORY()

public:
	/**
	 * Whether grayscale is enabled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ImGrayablePanel", meta = (ExposeOnSpawn = "true"))
	bool bIsGrayed = false;
};
