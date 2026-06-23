// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateInternal.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Application/SlateApplicationBase.h"
#include "Fonts/FontMeasure.h"
#include "ImSlate.h"
#include "Rendering/SlateRenderer.h"
#include "SImSlateWindow.h"

namespace ImSlate
{
UWorld* GetImSlateWorldChecked()
{
	return GImSlate->GetWorldChecked();
}

namespace Internal
{
}  // namespace Internal

ImVec2 SimpleMeasureText(float Width, const FString& InText, const FSlateFontInfo& FontInfo, const float FontScale, const ImVec2& LocalShadowOffset, const TOptional<float>& MinDesiredWidth)
{
	const float LocalOutlineSize = FontInfo.OutlineSettings.OutlineSize;
	// Account for the outline width impacting both size of the text by multiplying by 2
	// Outline size in Y is accounted for in MaxHeight calculation in Measure()
	const ImVec2 ComputedOutlineSize(LocalOutlineSize * 2, LocalOutlineSize);
	int32 CharIdx = 0;

	const ImVec2 TextSize = (ImVec2)FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(InText, CharIdx, CharIdx + 1, FontInfo, true, FontScale) + ComputedOutlineSize + LocalShadowOffset;

	auto CachedSimpleDesiredSize = ImVec2(FMath::Max(MinDesiredWidth.Get(0.0f), TextSize.X), TextSize.Y);
	return CachedSimpleDesiredSize;
}

TUniquePtr<ImSlate::FImSlateGCRoot>& ImSlateContext::GetGCRoot()
{
	if (!GCRoot.IsValid())
		GCRoot.Reset(new FImSlateGCRoot(*this));
	return GCRoot;
}

void FImSlateGCRoot::AddReferencedObject(const UObject* InObj)
{
	ImContext.ReferencedObjects.AddUnique(InObj);
}

void FImSlateGCRoot::AddWindowedReferencedObject(SImSlateWindow* InWindow, const UObject* InObj)
{
	check(InWindow && InObj);
	InWindow->AddReferencedObject(InObj);
}

void FImSlateGCRoot::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(ImContext.ReferencedObjects);
	for (auto& Pair : ImContext.WindowsById)
	{
		Pair.Value->AddReferencedObjects(Collector);
	}
}
}  // namespace ImSlate
