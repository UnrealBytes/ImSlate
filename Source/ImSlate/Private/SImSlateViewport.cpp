// Copyright ImSlate, Inc. All Rights Reserved.
#include "SImSlateViewport.h"

#include "ImSlatePrivate.h"
#include "Application/SlateApplicationBase.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ImSlate.h"
#include "Kismet/KismetMathLibrary.h"
#include "ProtectFieldAccessor.h"
#include "SImSlateWindow.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SDPIScaler.h"

namespace ImSlate
{
extern TSharedRef<SImSlateViewport>& GetGameViewport();

SImSlateViewport::SImSlateViewport()
	: Children(this)
	, NewChildren(this)
{
	bHasCustomPrepass = true;
	SetCanTick(false);
	bCanSupportFocus = false;
}

 SImSlateViewport::~SImSlateViewport()
{
	NewChildren.GetOwnerPtr() = nullptr;
}

void SImSlateViewport::Construct(const FArguments& InArgs)
{
	WeakGameViewportClient = InArgs._GameViewportClient;
	SetVisibility(InArgs._Visibility);
}

void SImSlateViewport::RemoveAllWindow()
{
	if (Window)
	{
		RemoveWindow(StaticCastSharedRef<SImSlateWindow>(Window->AsShared()));
	}

	auto ChildNum = Children.Num();
	TArray<TSharedRef<SImSlateWindow>> WinsToDel;
	for (int32 i = 0; i < ChildNum; ++i)
	{
		WinsToDel.Add(StaticCastSharedRef<SImSlateWindow>(Children.GetChildAt(i)));
	}

	for (int32 i = 0; i < WinsToDel.Num(); ++i)
	{
		RemoveWindow(WinsToDel[i]);
	}

	Window = nullptr;
}

FChildren* SImSlateViewport::GetChildren()
{
	return &Children;
}

void SImSlateViewport::ClearChildren()
{
	Children.Empty();
	NewChildren.Empty();
}

#if 0
void SWidget::SlatePrepass(float InLayoutScaleMultiplier)
{
	SCOPE_CYCLE_COUNTER(STAT_SlatePrepass);

	if (!GSlateIsOnFastUpdatePath || bNeedsPrepass)
	{
		// If the scale changed, that can affect the desired size of some elements that take it into
		// account, such as text, so when the prepass size changes, so must we invalidate desired size.
		bNeedsDesiredSize = true;

		Prepass_Internal(InLayoutScaleMultiplier);
	}
}
void SWidget::Prepass_Internal(float InLayoutScaleMultiplier)
{
	PrepassLayoutScaleMultiplier = InLayoutScaleMultiplier;

	bool bShouldPrepassChildren = true;
	if (bHasCustomPrepass)
	{
		bShouldPrepassChildren = CustomPrepass(InLayoutScaleMultiplier);
	}

	if (bCanHaveChildren && bShouldPrepassChildren)
	{
		// Cache child desired sizes first. This widget's desired size is a function of its children's sizes.
		FChildren* MyChildren = this->GetChildren();
		const int32 NumChildren = MyChildren->Num();
		for (int32 ChildIndex = 0; ChildIndex < MyChildren->Num(); ++ChildIndex)
		{
			const float ChildLayoutScaleMultiplier = bHasRelativeLayoutScale
				? InLayoutScaleMultiplier * GetRelativeLayoutScale(ChildIndex, InLayoutScaleMultiplier)
				: InLayoutScaleMultiplier;

			const TSharedRef<SWidget>& Child = MyChildren->GetChildAt(ChildIndex);

			if (Child->Visibility.Get() != EVisibility::Collapsed)
			{
				// Recur: Descend down the widget tree.
				Child->Prepass_Internal(ChildLayoutScaleMultiplier);
			}
			else
			{
				// If the child widget is collapsed, we need to store the new layout scale it will have when 
				// it is finally visible and invalidate it's prepass so that it gets that when its visiblity
				// is finally invalidated.
				Child->InvalidatePrepass();
				Child->PrepassLayoutScaleMultiplier = ChildLayoutScaleMultiplier;
			}
		}
		ensure(NumChildren == MyChildren->Num());
	}

	{
		// Cache this widget's desired size.
		CacheDesiredSize(PrepassLayoutScaleMultiplier.Get(1.0f));
		bNeedsPrepass = false;
	}
}
#endif

bool SImSlateViewport::CustomPrepass(float LayoutScaleMultiplier)
{
	if (!WITH_EDITOR || (FSlateThrottleManager::Get().IsAllowingExpensiveTasks() /* && !!NewChildren.Num()*/))
	{
		Children = MoveTemp(NewChildren);
	}
	return true;
}

void SImSlateViewport::OnClose(SImSlateWindow* InWindow)
{
	InWindow->Hidden = true;
}

int32 SImSlateViewport::FindWindowIndex(const SImSlateWindow* InWindow) const
{
	if (!InWindow)
		return INDEX_NONE;
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if (InWindow == &Children[SlotIdx].Get())
		{
			return SlotIdx;
		}
	}

	return INDEX_NONE;
}

int32 SImSlateViewport::OnPaint(const FPaintArgs& Args,  //
								const FGeometry& AllottedGeometry,
								const FSlateRect& MyCullingRect,
								FSlateWindowElementList& OutDrawElements,
								int32 LayerId,
								const FWidgetStyle& InWidgetStyle,
								bool bParentEnabled) const
{
	FArrangedChildren ArrangedChildren(EVisibility::Visible);
	this->ArrangeChildren(AllottedGeometry, ArrangedChildren);

	// Because we paint multiple children, we must track the maximum layer id that they produced in case one of our parents
	// wants to an overlay for all of its contents.
	int32 MaxLayerId = LayerId;

	const bool bForwardedEnabled = ShouldBeEnabled(bParentEnabled);

	const FPaintArgs NewArgs = Args.WithNewParent(this);

	for (int32 ChildIndex = 0; ChildIndex < ArrangedChildren.Num(); ++ChildIndex)
	{
		FArrangedWidget& CurWidget = ArrangedChildren[ChildIndex];

		if (!IsChildWidgetCulled(MyCullingRect, CurWidget))
		{
			const int32 CurWidgetsMaxLayerId = CurWidget.Widget->Paint(NewArgs, CurWidget.Geometry, MyCullingRect, OutDrawElements, MaxLayerId + 1, InWidgetStyle, bForwardedEnabled);

			MaxLayerId = FMath::Max(MaxLayerId, CurWidgetsMaxLayerId);
		}
		else
		{
		}
	}

	if (GImSlate && GImSlate->LongPressTooltip.bVisible)
	{
		const auto& Tip = GImSlate->LongPressTooltip;
		FSlateFontInfo Font = GetImSlateDefaultFont();
		TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D TextSize = (FVector2D)FontMeasure->Measure(Tip.Text, Font);

		FVector2D Padding(8.f, 4.f);
		FVector2D BgSize = TextSize + Padding * 2.f;
		FVector2D LocalPos = AllottedGeometry.AbsoluteToLocal(Tip.AbsolutePosition) + FVector2D(0, 2.f);

		static FSlateBrush TipBrush;
		TipBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		TipBrush.TintColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.95f);
		TipBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		TipBrush.OutlineSettings.CornerRadii = FVector4(4, 4, 4, 4);
		TipBrush.OutlineSettings.Color = FLinearColor(0.4f, 0.4f, 0.4f, 1.f);
		TipBrush.OutlineSettings.Width = 1.f;

		FSlateDrawElement::MakeBox(OutDrawElements, MaxLayerId + 1, AllottedGeometry.ToPaintGeometry(BgSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(LocalPos))), &TipBrush);
		FSlateDrawElement::MakeText(OutDrawElements, MaxLayerId + 2, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(1.f, UE::Slate::CastToVector2f(LocalPos + Padding))), Tip.Text, Font, ESlateDrawEffect::None, FLinearColor::White);

		MaxLayerId += 2;
	}

	return MaxLayerId;
}

TSharedRef<SWidget> SImSlateViewport::ToSharedRefWithDPI(TAttribute<float> InDPIScale)
{
	return SNew(SScaleBox)
			.IgnoreInheritedScale(true)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[
				SNew(SDPIScaler)
				.DPIScale(InDPIScale)
				.Visibility(EVisibility::SelfHitTestInvisible)
				[
					SharedThis<SImSlateViewport>(this)
				]
			];
}

ImVec2 SImSlateViewport::GetWorkCenter() const
{
#if ENGINE_MAJOR_VERSION < 5
	return FSlateApplicationBase::Get().GetPreferredWorkArea().GetCenter();
#else
	return (FVector2d)FSlateApplicationBase::Get().GetPreferredWorkArea().GetCenter();
#endif
}

ImVec2 SImSlateViewport::GetViewportCenter() const
{
	return (ImVec2)GetCachedGeometry().GetLocalPositionAtCoordinates(FVector2D(0.5f, 0.5f));
}

float SImSlateViewport::StaticGetDPIScaleFactorAtPoint(FVector2D AbsPos)
{
	float DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(AbsPos.X, AbsPos.Y);
	if (UKismetMathLibrary::NearlyEqual_FloatFloat(DPIScale, 0.f))
		DPIScale = 1.f;
	return DPIScale;
}

FVector2D SImSlateViewport::ComputeDesiredSize(float) const
{
	// viewport have no desired size -- their size is always determined by their container
	return FVector2D::ZeroVector;
}

}  // namespace ImSlate
