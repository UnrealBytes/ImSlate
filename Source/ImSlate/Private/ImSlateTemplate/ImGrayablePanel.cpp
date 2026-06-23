// Copyright ImSlate, Inc. All Rights Reserved.
// ImGrayablePanel implementation using GetUncachedDrawElements + VisitTupleElements

#include "ImSlateTemplate/ImGrayablePanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Rendering/DrawElements.h"
#include "PrivateFieldAccessor.h"
#include <utility>
namespace GrayablePanel {
	GS_PRIVATEACCESS_MEMBER(FSlateDrawElement, DrawEffects, ESlateDrawEffect);
}

namespace ImSlate
{

class FApplyGrayscaleVisitor
{
public:
	explicit FApplyGrayscaleVisitor(FSlateDrawElementMap& ElementMap)
		: SlateDrawElementMap(ElementMap)
	{
		RecordStartIndices(SlateDrawElementMap, std::make_index_sequence<TupleSize>{});
	}
	~FApplyGrayscaleVisitor()
	{
		ApplyFromIndices(SlateDrawElementMap, std::make_index_sequence<TupleSize>{});
	}

private:
	FSlateDrawElementMap& SlateDrawElementMap;
	static constexpr size_t TupleSize = TTupleArity<FSlateDrawElementMap>::Value;
	int32 StartIndices[TupleSize] = {0};

	template<size_t... Indices>
	void RecordStartIndices(const FSlateDrawElementMap& ElementMap, std::index_sequence<Indices...>)
	{
		((StartIndices[Indices] = ElementMap.template Get<Indices>().Num()), ...);
	}

	template<size_t... Indices>
	void ApplyFromIndices(FSlateDrawElementMap& ElementMap, std::index_sequence<Indices...>)
	{
		(ApplyToArrayFromIndex(ElementMap.template Get<Indices>(), StartIndices[Indices]), ...);
	}

	template<typename ElementArrayType>
	void ApplyToArrayFromIndex(ElementArrayType& ElementArray, int32 StartIndex)
	{
		const int32 Num = ElementArray.Num();
		for (int32 i = StartIndex; i < Num; ++i)
		{
			auto& Element = ElementArray[i];
			GrayablePanel::PrivateAccess::DrawEffects(Element) = Element.GetDrawEffects() | ESlateDrawEffect::DisabledEffect;
		}
	}
};

void SImGrayablePanel::Construct(const FArguments& InArgs, bool bInIsGrayed)
{
	bIsGrayed = bInIsGrayed;
	SConstraintCanvas::Construct(InArgs);
}

int32 SImGrayablePanel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	if (!bIsGrayed.Get(false))
	{
		return SConstraintCanvas::OnPaint(
			Args, AllottedGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle,  bParentEnabled);
	}

	FApplyGrayscaleVisitor GrayscaleVisitor(const_cast<FSlateDrawElementMap&>(OutDrawElements.GetUncachedDrawElements()));
	return SConstraintCanvas::OnPaint(
		Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SImGrayablePanel::SetGrayed(bool bInGrayed)
{
	bIsGrayed = bInGrayed;
	Invalidate(EInvalidateWidgetReason::Paint);
}
}  // namespace ImSlate

//////////////////////////////////////////////////////////////////////////
// UImGrayablePanel Implementation
//////////////////////////////////////////////////////////////////////////

UImGrayablePanel::UImGrayablePanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedRef<ImSlate::SImGrayablePanel> UImGrayablePanel::ConstructImWidget() const
{
	TSharedRef<ImSlate::SImGrayablePanel> GrayablePanel = SNew(ImSlate::SImGrayablePanel, bIsGrayed);

	// Add all children to the SConstraintCanvas
	for (UPanelSlot* PanelSlot : Slots)
	{
		if (UCanvasPanelSlot* TypedSlot = Cast<UCanvasPanelSlot>(PanelSlot))
		{
			if (TypedSlot->Content)
			{
				TypedSlot->Parent = const_cast<UImGrayablePanel*>(this);
				TypedSlot->BuildSlot(GrayablePanel);
			}
		}
	}

	return GrayablePanel;
}

TSharedRef<SWidget> UImGrayablePanel::RebuildWidget()
{
	MyCanvas = ConstructImWidget();
	return MyCanvas.ToSharedRef();
}

void UImGrayablePanel::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	SetGrayed(bIsGrayed);
}

void UImGrayablePanel::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UImGrayablePanel::SetGrayed(bool bGrayed)
{
	bIsGrayed = bGrayed;

	if (MyCanvas.IsValid())
	{
		StaticCastSharedPtr<ImSlate::SImGrayablePanel>(MyCanvas)->SetGrayed(bIsGrayed);
	}
}

