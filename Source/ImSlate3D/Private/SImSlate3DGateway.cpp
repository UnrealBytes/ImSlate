// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DGateway.h"

#if defined(IMSLATE3D_API)

#include "ImSlate3DHitManager.h"
#include "ImSlate3DHost.h"   // FImSlate3DHost — camera ViewRect for the desktop→viewport-px conversion

namespace ImSlate
{
namespace
{
	// The single shared gateway instance (one per game viewport). Weak so it drops when removed.
	TWeakPtr<SImSlate3DGateway> GGatewayInstance;

	// Compute the viewport-px cursor ONCE from this fullscreen gateway's geometry + the camera ViewRect, then
	// hand the same VP to every unit (so non-fullscreen units don't re-derive it from their own geometry).
	FVector2f GatewayCursorVP(const FGeometry& GatewayGeom, const FPointerEvent& Event)
	{
		const FImSlate3DHost& Host = FImSlate3DHost::Get();
		const FImCameraProjData Data = Host.GetCameraProjData ? Host.GetCameraProjData() : FImCameraProjData{};
		return MapScreenToViewportPx(GatewayGeom, Data.ViewRect, Event.GetScreenSpacePosition());
	}
}

void SImSlate3DGateway::Construct(const FArguments& InArgs)
{
	// Visible: only a widget in the bubble path receives OnMouse*. Unmatched events return Unhandled so Slate
	// keeps bubbling to the game UI below (transparent pass-through). OnPaint draws nothing.
	SetVisibility(EVisibility::Visible);
}

int32 SImSlate3DGateway::OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&,
	FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool) const
{
	return LayerId;  // draws nothing — pure event entry point
}

FReply SImSlate3DGateway::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = GatewayCursorVP(MyGeometry, MouseEvent);
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonDown(MouseEvent, InVP); });
}

FReply SImSlate3DGateway::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = GatewayCursorVP(MyGeometry, MouseEvent);
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonUp(MouseEvent, InVP); });
}

FReply SImSlate3DGateway::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f VP = GatewayCursorVP(MyGeometry, MouseEvent);
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseMove(MouseEvent, InVP); });
}

FReply SImSlate3DGateway::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Same fullscreen-geometry cursor source as the other entries; wheel is one-shot hit-and-dispatch (no hover/
	// capture) → the front-most pixel-hit unit's leaf bubbles the wheel (e.g. a ScrollBox in a Top-layer window).
	const FVector2f VP = GatewayCursorVP(MyGeometry, MouseEvent);
	return FImSlate3DHitManager::Get().HandleWheel(MouseEvent, VP);
}

void SImSlate3DGateway::Install(const TFunctionRef<void(const TSharedRef<SWidget>&)>& AddToViewport)
{
	if (GGatewayInstance.IsValid())
	{
		return;  // already installed
	}
	TSharedRef<SImSlate3DGateway> Gateway = SNew(SImSlate3DGateway);
	GGatewayInstance = Gateway;
	AddToViewport(Gateway);
}

void SImSlate3DGateway::Uninstall(const TFunctionRef<void(const TSharedRef<SWidget>&)>& RemoveFromViewport)
{
	if (TSharedPtr<SImSlate3DGateway> Gateway = GGatewayInstance.Pin())
	{
		RemoveFromViewport(Gateway.ToSharedRef());
		GGatewayInstance.Reset();
	}
}

bool SImSlate3DGateway::IsInstalled()
{
	return GGatewayInstance.IsValid();
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
