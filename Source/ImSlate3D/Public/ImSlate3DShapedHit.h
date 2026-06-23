// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3DAPI.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Types/ISlateMetaData.h"   // ISlateMetaData / SLATE_METADATA_TYPE
#include "Widgets/SWidget.h"        // GetMetaData

namespace ImSlate
{
// Point-in-triangle (barycentric sign test). Shared by every per-pixel shape hit-test in the 3D module:
// the viewport-top arrow (screen space) and the in-window gizmo arrow (plane-local space) both build a
// 7-vertex polygon (shaft quad + head triangle) and test it as triangles with this.
inline bool PointInTri(const FVector2f& P, const FVector2f& A, const FVector2f& B, const FVector2f& C)
{
	const float d1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
	const float d2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
	const float d3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);
	const bool bNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
	const bool bPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
	return !(bNeg && bPos);
}

/**
 * A child widget inside a 3D-projected window that has a NON-rectangular (or transparent-gapped) shape,
 * e.g. a gizmo arrow. The window's hit walk (SImSlate3DTransformBox::BuildLocalPath) does a cheap layout-
 * rect broad phase, then — if the child implements this — a per-pixel narrow phase in PLANE-LOCAL pixels
 * (the same space the cursor unprojects into). Return false for points in the transparent gaps so the hit
 * falls through to the sibling/widget below. This is what makes irregular controls inside a transparent
 * perspective window respond precisely.
 */
class IImSlate3DPlaneShaped
{
public:
	virtual ~IImSlate3DPlaneShaped() = default;
	// PlaneLocalPt = cursor unprojected to the window plane's local pixels (widget-local). True = solid hit.
	virtual bool HitTestPlaneLocal(FVector2f PlaneLocalPt) const = 0;

	// This shaped child's full extent in PLANE-LOCAL px, which MAY exceed the panel content rect [0..Size]
	// (e.g. a gizmo arrow head sticking out past the panel edge). The 3D box unions these into its hit-clip
	// trapezoid + broad-phase AABB so the overhang stays hittable (else the engine hit-grid drops it before
	// the per-pixel test). Default false = no overhang declared → box uses only [0..Size]. Return true and
	// fill Min/Max to declare an extent.
	virtual bool GetPlaneLocalBounds(FVector2f& OutMin, FVector2f& OutMax) const { return false; }
};

// Slate widgets have no built-in cross-cast to a secondary interface. A shaped widget registers itself by
// attaching FImPlaneShapedMeta (carrying the IImSlate3DPlaneShaped*) as widget metadata; the hit walk
// queries it via AsPlaneShaped(). Lighter than RTTI and works across the SWidget hierarchy.
class FImPlaneShapedMeta : public ISlateMetaData
{
public:
	SLATE_METADATA_TYPE(FImPlaneShapedMeta, ISlateMetaData)
	explicit FImPlaneShapedMeta(IImSlate3DPlaneShaped* InShaped) : Shaped(InShaped) {}
	IImSlate3DPlaneShaped* Shaped = nullptr;
};

// Returns the widget's IImSlate3DPlaneShaped if it declared one (via FImPlaneShapedMeta), else null.
inline IImSlate3DPlaneShaped* AsPlaneShaped(const TSharedRef<SWidget>& W)
{
	const TSharedPtr<FImPlaneShapedMeta> Meta = W->GetMetaData<FImPlaneShapedMeta>();
	return Meta.IsValid() ? Meta->Shaped : nullptr;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
