// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "ImSlate.h"   // immediate-mode API (Begin/End/Button/Text/SetNextWindow3DTransform) for imslate.3d.im

#include "SImSlate3DTransformBox.h"
#include "SImSlate3DArrow.h"
#include "SImSlate3DPlaneArrow.h"   // SImSlate3DPlaneArrow (composable transparent in-plane arrow) + FOnGizmoPositionChanged
#include "SImSlate3DGateway.h"      // SImSlate3DGateway — single fullscreen 3D event entry (phase 5)
#include "ImSlate3DGizmo.h"         // FImSlate3DGizmo — standalone 3-axis world transform gizmo (FTransform Query/Edit)
#include "SImSlate3DConstraintHandle.h"  // SImSlate3DConstraintHandle — scale/plane/rotate handle (constraint-driven)
#include "SImSlate3DGizmoHud.h"           // SImSlate3DGizmoHud — drag ghost + Δ readout overlay
#include "ImSlate3DProjection.h"          // ImGetViewCentre / FImViewPoint — frustum-centre placement helper
#include "ImSlate3DHitManager.h"
#include "XConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "GameFramework/InputSettings.h"
#include "Engine/Player.h"

// imslate.3d  — spawn one SImSlate3DTransformBox placed ~300uu in front of the camera,
// pitched 30 deg, so its projected quad shows a perspective trapezoid that tracks the
// camera. Proves the M1 projector + MakeCustomVerts compositing end to end.
//
// Usage (in editor/PIE console):
//   imslate.3d              -> show, default pitch 30
//   imslate.3d 0            -> hide
//   imslate.3d 1 45         -> show, pitch 45
//
// Mirrors imslate.XConsole / imslate.ShowDemo registration style.

// Viewport ZOrder for the 3D widget. Default is very low so the 3D sheet sits BELOW all the game UI
// (it never visually covers or blocks other widgets). The hit-test is CustomPath (HitTestInvisible), so a
// low ZOrder is safe — the widget doesn't grab events regardless. Raise this if you want it on top.
static TAutoConsoleVariable<int32> CVarImSlate3DZOrder(
	TEXT("imslate.3d.ZOrder"), -100,
	TEXT("Viewport ZOrder for the imslate.3d widget. Low = below other UI (default -100). Re-run imslate.3d to apply."));

namespace ImSlate3DDemo
{
static TWeakPtr<ImSlate::SImSlate3DTransformBox> GBox;
static TWeakPtr<ImSlate::SImSlate3DHitClip> GHitClip;  // the actual viewport-added wrapper
static TArray<TWeakPtr<ImSlate::SImSlate3DArrow>> GArrows;  // independent gizmo arrow widgets
static TArray<TWeakPtr<ImSlate::SImSlate3DArrowHitClip>> GArrowClips;  // the viewport-added wrappers (own the clip)

static void Hide()
{
	// The HitClip wrapper is what was added to the viewport (it hosts the box).
	if (TSharedPtr<ImSlate::SImSlate3DHitClip> HitClip = GHitClip.Pin())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(HitClip.ToSharedRef());
		}
		GHitClip.Reset();
	}
	// Independent gizmo arrows: the SImSlate3DArrowHitClip wrappers are what was added to the viewport.
	for (TWeakPtr<ImSlate::SImSlate3DArrowHitClip>& W : GArrowClips)
	{
		if (TSharedPtr<ImSlate::SImSlate3DArrowHitClip> Clip = W.Pin())
		{
			if (GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->RemoveViewportWidgetContent(Clip.ToSharedRef());
			}
		}
	}
	GArrowClips.Reset();
	GArrows.Reset();
	GBox.Reset();
}

static void Show(UWorld* World, float PitchDeg, bool bDoubleSided)
{
	Hide();
	if (!World || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
	APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	// Place the sheet 600uu down the view-centre ray via the shared helper (frustum-centre + camera basis).
	const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(600.0);
	if (!VC.bValid) { return; }
	const FVector CamRight = VC.Right, CamUp = VC.Up, Forward = VC.Forward;
	const FVector SheetLoc = VC.Location;

	// Give FImProjector the EXACT world axes (no FRotator round-trip → no mirror/flip ambiguity):
	//   widget right  = CamRight        (screen right)
	//   widget down   = -CamUp          (widget Y grows downward; world up is CamUp)
	//   widget normal = -Forward        (front faces the camera)
	// Pitch tilts the top away by rotating normal & down about CamRight (axis-angle, not Euler).
	const FVector SheetNormal = (-Forward).RotateAngleAxis(PitchDeg, CamRight);
	const FVector SheetDown   = (-CamUp).RotateAngleAxis(PitchDeg, CamRight);

	ImSlate::FImWorldPlacement Placement;
	Placement.WorldLocation = SheetLoc;
	Placement.Pivot = FVector2f(0.5f, 0.5f);
	Placement.WorldScale = 1.5f;  // 400x300 px * 1.5 = 600x450 world units (was 0.4: too small after
	                              // the DesiredSize fix — content is now the real 400x300, not full-screen)
	Placement.Opacity = bDoubleSided ? 1.f : 0.f;  // double-sided = always shown(1); single = cull back(0)
	Placement.bUseExplicitAxes = true;
	Placement.ExplicitRight  = CamRight;
	Placement.ExplicitDown   = SheetDown;
	Placement.ExplicitNormal = SheetNormal;

	// Solid light-blue button style (UE's default SButton brush is dark grey #383838, which reads as a
	// black block in 3D). Override all three states with a flat WhiteBrush tinted blue, brighter on hover.
	static FButtonStyle LightBlueButtonStyle = []()
	{
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
		FButtonStyle S;
		FSlateBrush N = *White; N.TintColor = FLinearColor(0.15f, 0.45f, 0.85f, 0.95f);
		FSlateBrush H = *White; H.TintColor = FLinearColor(0.25f, 0.60f, 1.00f, 0.95f);
		FSlateBrush P = *White; P.TintColor = FLinearColor(0.10f, 0.35f, 0.70f, 0.95f);
		S.SetNormal(N).SetHovered(H).SetPressed(P);
		return S;
	}();

	// A few nested boxes/borders so the projected content shows real box elements forming a
	// perspective trapezoid (M2 projects box-type elements; tinted, texture-faithful later).
	TSharedRef<ImSlate::SImSlate3DTransformBox> Box = SNew(ImSlate::SImSlate3DTransformBox)
	[
		SNew(SBox).WidthOverride(400.f).HeightOverride(300.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.10f, 0.45f, 0.90f, 0.55f))
			.Padding(20.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
				[ SNew(SBox).WidthOverride(120.f).HeightOverride(60.f)
					[ SNew(SButton)  // clickable + hover-highlight, to test M4 hit-test/hover
						.HAlign(HAlign_Center).VAlign(VAlign_Center)         // center the label
						.ButtonStyle(&LightBlueButtonStyle)                  // solid light-blue, not UE's dark default
						.OnClicked_Lambda([]() { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] BUTTON CLICKED")); return FReply::Handled(); })
						.OnHovered_Lambda([]() { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] button HOVERED")); })
						.OnUnhovered_Lambda([]() { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D] button unhovered")); })
						[ SNew(STextBlock)
							.Text(FText::FromString(TEXT("Click")))
							.ColorAndOpacity(FLinearColor::White)
							.Justification(ETextJustify::Center) ] ] ]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
				[ SNew(SBox).WidthOverride(120.f).HeightOverride(60.f)
					[ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.20f, 0.85f, 0.40f, 1.f)) ] ]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ SNew(STextBlock).Text(FText::FromString(TEXT("ImSlate 3D")))
					.ColorAndOpacity(FLinearColor::White)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 36)) ]
			]
		]
	];
	Box->SetWorldPlacement(Placement);

	// Wrap the box in SImSlate3DHitClip: it pushes the trapezoid hit-clip each paint so clicks OUTSIDE the
	// projected quad fall through to the game UI behind, while inside the quad the box still handles them.
	TSharedRef<ImSlate::SImSlate3DHitClip> HitClip = SNew(ImSlate::SImSlate3DHitClip).Box(Box);
	const int32 BaseZ = CVarImSlate3DZOrder.GetValueOnGameThread();
	GEngine->GameViewport->AddViewportWidgetContent(HitClip, BaseZ);
	GBox = Box;
	GHitClip = HitClip;

	// 3-axis transform gizmo as INDEPENDENT 3D widgets (each its own viewport widget, NOT nested in the panel,
	// so none is clipped by the panel's trapezoid — all three can be interacted with regardless of where they
	// project). Each drags the panel's WorldLocation along its world axis. Colours: X red / Y green / Z blue.
	// ZOrder above the panel so the arrows receive events first and paint after the panel (current-frame anchor).
	{
		auto SpawnArrow = [&](const FVector& Axis, const FLinearColor& Col, int32 Order)
		{
			TSharedRef<ImSlate::SImSlate3DArrow> Arrow = SNew(ImSlate::SImSlate3DArrow)
				.Target(Box)
				.WorldAxis(Axis)
				.Color(Col)
				.OverlapOrder(Order)
				.ShaftLength(120.f).ShaftWidth(10.f).HeadLength(40.f).HeadWidth(40.f);
			// Wrap in SImSlate3DArrowHitClip so the arrow only occupies its projected AABB in the hit grid —
			// outside it, clicks fall through to the panel / game UI (else the full-screen arrow widget blocks
			// everything behind it). Inside, FImSlate3DHitManager resolves overlaps per-pixel.
			TSharedRef<ImSlate::SImSlate3DArrowHitClip> ArrowClip = SNew(ImSlate::SImSlate3DArrowHitClip).Arrow(Arrow);
			GEngine->GameViewport->AddViewportWidgetContent(ArrowClip, BaseZ + 1);
			// Register with the shared hit dispatcher (per-pixel + overlap resolution across all 3D units).
			ImSlate::FImSlate3DHitManager::Get().Register(StaticCastSharedRef<ImSlate::IImSlate3DHittable>(Arrow));
			GArrows.Add(Arrow);
			GArrowClips.Add(ArrowClip);
		};
		SpawnArrow(CamRight,    FLinearColor(0.95f, 0.20f, 0.20f, 1.f), 0);  // X / right  → red
		SpawnArrow(SheetDown,   FLinearColor(0.25f, 0.85f, 0.30f, 1.f), 1);  // Y / down   → green
		SpawnArrow(SheetNormal, FLinearColor(0.25f, 0.45f, 0.95f, 1.f), 2);  // Z / normal → blue (out of plane)
	}
}

}  // namespace ImSlate3DDemo

FXConsoleCommandLambdaFull XVar_ImSlate3D(
	TEXT("imslate.3d"),
	TEXT("imslate.3d [bShow=1] [pitchDeg=30] [bDoubleSided=0] — world-anchored 3D-transformed ImSlate quad"),
	[](TOptional<bool> bShow, TOptional<float> Pitch, TOptional<bool> bDoubleSided, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true))
		{
			ImSlate3DDemo::Show(InWorld, Pitch.Get(30.f), bDoubleSided.Get(false));
			Ar.Log(TEXT("imslate.3d: shown"));
		}
		else
		{
			ImSlate3DDemo::Hide();
			Ar.Log(TEXT("imslate.3d: hidden"));
		}
	});

// imslate.3d.wingizmo — test the IN-WINDOW gizmo built by COMPOSITION (box content = SOverlay{ button +
// N transparent SImSlate3DPlaneArrow }, NOT the independent viewport SImSlate3DArrow): you can verify gizmo
// arrows hover-highlight, drag the box along the axis, and clicking the arrows' TRANSPARENT GAPS falls
// through to the button below (per-pixel shape hit). Re-run 0 to hide.
namespace ImSlate3DWinGizmoDemo
{
	static TWeakPtr<ImSlate::SImSlate3DTransformBox> GBox;
	static TWeakPtr<ImSlate::SImSlate3DHitClip> GHitClip;

	static void Hide()
	{
		if (TSharedPtr<ImSlate::SImSlate3DHitClip> Clip = GHitClip.Pin())
		{
			if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->RemoveViewportWidgetContent(Clip.ToSharedRef()); }
		}
		GHitClip.Reset();
		GBox.Reset();
	}

	static void Show(UWorld* World, float PitchDeg)
	{
		Hide();
		if (!World || !GEngine || !GEngine->GameViewport) { return; }
		ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
		APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerCameraManager) { return; }

		const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(600.0);
		if (!VC.bValid) { return; }
		const FVector CamRight = VC.Right, CamUp = VC.Up, Forward = VC.Forward;
		const FVector SheetNormal = (-Forward).RotateAngleAxis(PitchDeg, CamRight);
		const FVector SheetDown   = (-CamUp).RotateAngleAxis(PitchDeg, CamRight);

		ImSlate::FImWorldPlacement Placement;
		Placement.WorldLocation = VC.Location;
		Placement.Pivot = FVector2f(0.5f, 0.5f);
		Placement.WorldScale = 1.5f;
		Placement.bUseExplicitAxes = true;
		Placement.ExplicitRight  = CamRight;
		Placement.ExplicitDown   = SheetDown;
		Placement.ExplicitNormal = SheetNormal;

		// A button UNDER the gizmo so we can verify clicks through the arrows' transparent gaps reach it.
		static FButtonStyle BtnStyle = []() {
			const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
			FButtonStyle S; FSlateBrush N = *White; N.TintColor = FLinearColor(0.15f, 0.45f, 0.85f, 0.9f);
			S.SetNormal(N).SetHovered(N).SetPressed(N); return S;
		}();
		// gizmo = pure composition: box content = SOverlay{ button + N transparent SImSlate3DPlaneArrow arrows }.
		// Each arrow carries its own world axis + a write-back delegate; dragging any arrow rewrites the box's
		// WorldLocation, so the whole composite (button + arrows) moves together. The box knows nothing of gizmos.
		TSharedPtr<ImSlate::SImSlate3DPlaneArrow> ArrowX, ArrowY;
		TSharedRef<ImSlate::SImSlate3DTransformBox> Box = SNew(ImSlate::SImSlate3DTransformBox)
		[
			SNew(SBox).WidthOverride(400.f).HeightOverride(300.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SButton).ButtonStyle(&BtnStyle).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked_Lambda([]() { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][wingizmo] BUTTON under gizmo CLICKED (gap pass-through OK)")); return FReply::Handled(); })
					[ SNew(STextBlock).Text(FText::FromString(TEXT("Button under gizmo"))).ColorAndOpacity(FLinearColor::White) ]
				]
				+ SOverlay::Slot()[ SAssignNew(ArrowX, ImSlate::SImSlate3DPlaneArrow).WorldAxis(CamRight ).Color(FLinearColor(0.95f, 0.20f, 0.20f, 1.f)) ]
				+ SOverlay::Slot()[ SAssignNew(ArrowY, ImSlate::SImSlate3DPlaneArrow).WorldAxis(SheetDown).Color(FLinearColor(0.25f, 0.85f, 0.30f, 1.f)) ]
			]
		];
		Box->SetWorldPlacement(Placement);
		// Wire each arrow to the box (borrow projector + anchor) and write its drag back into the box location.
		TWeakPtr<ImSlate::SImSlate3DTransformBox> WeakBox = Box;
		auto WriteBack = [WeakBox](const FVector& NewPos) { if (auto B = WeakBox.Pin()) B->UpdatePlacementLocation(NewPos); };
		for (const TSharedPtr<ImSlate::SImSlate3DPlaneArrow>& A : { ArrowX, ArrowY })
		{
			A->SetOwnerBox(Box);
			A->SetOnPositionChanged(ImSlate::FOnGizmoPositionChanged::CreateLambda(WriteBack));
		}

		TSharedRef<ImSlate::SImSlate3DHitClip> HitClip = SNew(ImSlate::SImSlate3DHitClip).Box(Box);
		GEngine->GameViewport->AddViewportWidgetContent(HitClip, CVarImSlate3DZOrder.GetValueOnGameThread());
		GBox = Box;
		GHitClip = HitClip;
	}
}

FXConsoleCommandLambdaFull XVar_ImSlate3DWinGizmo(
	TEXT("imslate.3d.wingizmo"),
	TEXT("imslate.3d.wingizmo [bShow=1] [pitchDeg=30] — in-window gizmo (SImSlate3DPlaneArrow) over a button; test hover/drag/gap-passthrough"),
	[](TOptional<bool> bShow, TOptional<float> Pitch, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true)) { ImSlate3DWinGizmoDemo::Show(InWorld, Pitch.Get(30.f)); Ar.Log(TEXT("imslate.3d.wingizmo: shown")); }
		else { ImSlate3DWinGizmoDemo::Hide(); Ar.Log(TEXT("imslate.3d.wingizmo: hidden")); }
	});

// imslate.3d.multiwin — TWO overlapping 3D perspective windows with different overlap orders, to verify the
// multi-window hit dispatch: clicking the FRONT window (higher order) hits it; clicking the front window's
// TRANSPARENT region (outside its trapezoid) falls through to the BACK window; each window's gizmo drags
// independently. Re-run 0 to hide.
namespace ImSlate3DMultiWinDemo
{
	static TArray<TWeakPtr<ImSlate::SImSlate3DHitClip>> GClips;
	static TArray<TSharedPtr<FButtonStyle>> GStyles;  // keep button styles alive for the widgets' lifetime

	static void Hide()
	{
		for (const TWeakPtr<ImSlate::SImSlate3DHitClip>& W : GClips)
		{
			if (TSharedPtr<ImSlate::SImSlate3DHitClip> Clip = W.Pin())
			{
				if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->RemoveViewportWidgetContent(Clip.ToSharedRef()); }
			}
		}
		GClips.Reset();
		GStyles.Reset();
	}

	// Build one perspective window (button + 2-axis gizmo) at WorldLoc with the given overlap order + tint.
	static void SpawnOne(const FVector& WorldLoc, const FVector& CamRight, const FVector& SheetDown,
		const FVector& SheetNormal, int32 Order, const FLinearColor& Tint, const FString& Label)
	{
		ImSlate::FImWorldPlacement Placement;
		Placement.WorldLocation = WorldLoc;
		Placement.Pivot = FVector2f(0.5f, 0.5f);
		Placement.WorldScale = 1.5f;
		Placement.bUseExplicitAxes = true;
		Placement.ExplicitRight  = CamRight;
		Placement.ExplicitDown   = SheetDown;
		Placement.ExplicitNormal = SheetNormal;

		TSharedRef<FButtonStyle> BtnStyle = MakeShared<FButtonStyle>();
		{
			FSlateBrush N = *FCoreStyle::Get().GetBrush("WhiteBrush");
			N.TintColor = Tint;
			BtnStyle->SetNormal(N).SetHovered(N).SetPressed(N);
		}
		GStyles.Add(BtnStyle);  // keep alive (the button only holds a raw &style)
		const FString LabelCopy = Label;
		// Composition: box content = SOverlay{ button + 2 transparent gizmo arrows }. Arrows write drag back
		// into THIS box's location (each window's gizmo independent).
		TSharedPtr<ImSlate::SImSlate3DPlaneArrow> ArrowX, ArrowY;
		TSharedRef<ImSlate::SImSlate3DTransformBox> Box = SNew(ImSlate::SImSlate3DTransformBox)
		[
			SNew(SBox).WidthOverride(400.f).HeightOverride(300.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SButton).ButtonStyle(&BtnStyle.Get()).HAlign(HAlign_Center).VAlign(VAlign_Center)
					.OnClicked_Lambda([LabelCopy]() { UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][multiwin] BUTTON '%s' CLICKED"), *LabelCopy); return FReply::Handled(); })
					[ SNew(STextBlock).Text(FText::FromString(LabelCopy)).ColorAndOpacity(FLinearColor::White) ]
				]
				+ SOverlay::Slot()[ SAssignNew(ArrowX, ImSlate::SImSlate3DPlaneArrow).WorldAxis(CamRight ).Color(FLinearColor(0.95f, 0.20f, 0.20f, 1.f)) ]
				+ SOverlay::Slot()[ SAssignNew(ArrowY, ImSlate::SImSlate3DPlaneArrow).WorldAxis(SheetDown).Color(FLinearColor(0.25f, 0.85f, 0.30f, 1.f)) ]
			]
		];
		Box->SetWorldPlacement(Placement);
		Box->SetOverlapOrder(Order);
		TWeakPtr<ImSlate::SImSlate3DTransformBox> WeakBox = Box;
		auto WriteBack = [WeakBox](const FVector& NewPos) { if (auto B = WeakBox.Pin()) B->UpdatePlacementLocation(NewPos); };
		for (const TSharedPtr<ImSlate::SImSlate3DPlaneArrow>& A : { ArrowX, ArrowY })
		{
			A->SetOwnerBox(Box);
			A->SetOnPositionChanged(ImSlate::FOnGizmoPositionChanged::CreateLambda(WriteBack));
		}

		TSharedRef<ImSlate::SImSlate3DHitClip> HitClip = SNew(ImSlate::SImSlate3DHitClip).Box(Box);
		GEngine->GameViewport->AddViewportWidgetContent(HitClip, CVarImSlate3DZOrder.GetValueOnGameThread());
		GClips.Add(HitClip);
	}

	static void Show(UWorld* World, float PitchDeg)
	{
		Hide();
		if (!World || !GEngine || !GEngine->GameViewport) { return; }
		ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
		APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerCameraManager) { return; }

		const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(600.0);
		if (!VC.bValid) { return; }
		const FVector CamRight = VC.Right, CamUp = VC.Up, Forward = VC.Forward;
		const FVector SheetNormal = (-Forward).RotateAngleAxis(PitchDeg, CamRight);
		const FVector SheetDown   = (-CamUp).RotateAngleAxis(PitchDeg, CamRight);

		// Back window (order 0), then front window (order 1) shifted right+up so the two trapezoids OVERLAP
		// partially on screen. The front's transparent corners reveal the back; clicks there fall through.
		const FVector Base = VC.Location;
		SpawnOne(Base, CamRight, SheetDown, SheetNormal, 0,
			FLinearColor(0.85f, 0.45f, 0.15f, 0.9f), TEXT("BACK (order 0)"));
		SpawnOne(Base + CamRight * 120.f - CamUp * 90.f, CamRight, SheetDown, SheetNormal, 1,
			FLinearColor(0.15f, 0.55f, 0.85f, 0.9f), TEXT("FRONT (order 1)"));
	}
}

FXConsoleCommandLambdaFull XVar_ImSlate3DMultiWin(
	TEXT("imslate.3d.multiwin"),
	TEXT("imslate.3d.multiwin [bShow=1] [pitchDeg=30] — two overlapping 3D windows (orders 0/1); test front-hit / gap-fall-through / independent gizmo drag"),
	[](TOptional<bool> bShow, TOptional<float> Pitch, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true)) { ImSlate3DMultiWinDemo::Show(InWorld, Pitch.Get(30.f)); Ar.Log(TEXT("imslate.3d.multiwin: shown")); }
		else { ImSlate3DMultiWinDemo::Hide(); Ar.Log(TEXT("imslate.3d.multiwin: hidden")); }
	});

// Debug-only: toggle UInputSettings::bUseMouseForTouch. When the project fakes touch from the mouse
// (mobile-style input), the desktop mouse has no hover and a held button re-fires every frame. Set
// this to 0 to get real desktop mouse (hover + single press) while testing 3D widget interaction.
//   imslate.3d.DesktopMouse 1   → real mouse (bUseMouseForTouch=false)
//   imslate.3d.DesktopMouse 0   → restore fake-touch (bUseMouseForTouch=true)
FXConsoleCommandLambdaFull XVar_ImSlate3DDesktopMouse(
	TEXT("imslate.3d.DesktopMouse"),
	TEXT("imslate.3d.DesktopMouse [1=realMouse/0=fakeTouch] — toggle bUseMouseForTouch for testing"),
	[](TOptional<bool> bRealMouse, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (UInputSettings* IS = UInputSettings::GetInputSettings())
		{
			IS->bUseMouseForTouch = !bRealMouse.Get(true);
			Ar.Logf(TEXT("imslate.3d.DesktopMouse: bUseMouseForTouch=%d (realMouse=%d)"),
				IS->bUseMouseForTouch ? 1 : 0, bRealMouse.Get(true) ? 1 : 0);
		}
	});

// imslate.3d.im — IMMEDIATE-MODE 3D window demo. Proves SetNextWindow3DTransform: an ordinary
// ImSlate::Begin/End window, projected onto a world plane that BILLBOARDS toward the camera (rot=0).
// Re-run with 0 to stop. (Distinct from `imslate.3d`, which is the retained SImSlate3DTransformBox path.)
namespace ImSlate3DImDemo
{
	static TSharedPtr<void> GTickHandle;   // per-frame immediate-mode draw; reset to stop
	static bool GOpen = true;

	static void Tick(float /*Dt*/, UWorld* World)
	{
		if (!World || !GEngine) { return; }
		ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
		APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerCameraManager) { return; }

		// Anchor the window at the view-centre ~500uu out; rot defaults to zero → billboards to face us.
		const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(500.0);
		if (!VC.bValid) { return; }
		const FVector Anchor = VC.Location;

		ImSlate::SetNextWindow3DTransform(Anchor);  // billboard (centre pivot, scale 1)
		ImSlate::SetNextWindowSize(FVector2D(320.f, 200.f), ImSlateCond_Once);
		ImSlate::Begin("ImSlate 3D (immediate)", &GOpen);
		if (ImSlate::Button("Im3D_Btn", FVector2D(120.f, 32.f)))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][im] button clicked"));
		}
		ImSlate::Spacing();
		ImSlate::Text("Im3D_Label", NSLOCTEXT("ImSlate3D", "Im3DBillboard", "Billboard 3D window"));
		ImSlate::End();
	}
}

// imslate.3d.gizmoim — gizmo as PURE COMPOSITION of existing framework mechanisms: a transparent 3D window
// (SetNextWindow3DTransform), sized to just contain the arrow, holding ONE GizmoArrow item. Dragging the
// arrow writes a plain FVector back; the window is re-anchored at that FVector each frame, so the whole
// (transparent) window — i.e. the arrow — moves with the drag. No bespoke gizmo widget: window + item + axis.
namespace ImSlate3DGizmoImDemo
{
	static TSharedPtr<void> GTickHandle;
	static bool GOpen = true;
	static FVector GPos = FVector::ZeroVector;
	static bool GInit = false;

	static void Tick(float /*Dt*/, UWorld* World)
	{
		if (!World || !GEngine) { return; }
		ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
		APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerCameraManager) { return; }

		const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(500.0);
		if (!VC.bValid) { return; }
		const FVector CamRight = VC.Right;   // arrow axis = camera right
		if (!GInit) { GPos = VC.Location; GInit = true; }

		// Transparent 3D window centred AT GPos (billboard, centre pivot). Window auto-sizes to its content
		// (size 0 on both axes → fit to the arrow's DesiredSize). Centre pivot puts the arrow root exactly on
		// GPos. Re-anchored at GPos each frame → dragging the arrow (which rewrites GPos) moves the whole window.
		ImSlate::SetNextWindow3DTransform(GPos, FVector2f(0.5f, 0.5f));
		ImSlate::SetNextWindowSize(FVector2D(0.f, 0.f), ImSlateCond_Once);  // auto-fit to content (the arrow)
		ImSlate::SetNextWindowBgAlpha(0.f);  // transparent window → reads as a standalone arrow
		ImSlate::Begin("ImSlate 3D Gizmo (composed)", &GOpen);
		// One X-axis arrow (red). Drag → GPos moves along CamRight → window re-anchors → arrow tracks the drag.
		if (ImSlate::GizmoArrow("GizmoX", GPos, CamRight, FLinearColor(0.95f, 0.20f, 0.20f, 1.f)))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][gizmoim] GPos=(%.1f,%.1f,%.1f)"), GPos.X, GPos.Y, GPos.Z);
		}
		ImSlate::End();
	}
}

FXConsoleCommandLambdaFull XVar_ImSlate3DGizmoIm(
	TEXT("imslate.3d.gizmoim"),
	TEXT("imslate.3d.gizmoim [bShow=1] — gizmo composed from existing mechanisms: transparent 3D window + one GizmoArrow item, drag writes back an FVector"),
	[](TOptional<bool> bShow, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true))
		{
			ImSlate3DGizmoImDemo::GOpen = true;
			ImSlate3DGizmoImDemo::GInit = false;
			// Single fullscreen gateway: routes all 3D events → HitManager → each unit's projector (phase 5).
			// The immediate window's box is non-fullscreen, so it can't be the Slate entry; the gateway is.
			if (GEngine && GEngine->GameViewport)
			{
				ImSlate::SImSlate3DGateway::Install([](const TSharedRef<SWidget>& W)
				{
					GEngine->GameViewport->AddViewportWidgetContent(W, 1000);  // above 3D content
				});
			}
			ImSlate3DGizmoImDemo::GTickHandle = ImSlate::ImSlateTicker::BindDelegate(
				ImSlate::ImSlateTicker::FOnTickWithWorld::CreateStatic(&ImSlate3DGizmoImDemo::Tick), InWorld);
			Ar.Log(TEXT("imslate.3d.gizmoim: shown"));
		}
		else
		{
			ImSlate3DGizmoImDemo::GTickHandle.Reset();
			if (GEngine && GEngine->GameViewport)
			{
				ImSlate::SImSlate3DGateway::Uninstall([](const TSharedRef<SWidget>& W)
				{
					GEngine->GameViewport->RemoveViewportWidgetContent(W);
				});
			}
			Ar.Log(TEXT("imslate.3d.gizmoim: hidden"));
		}
	});

FXConsoleCommandLambdaFull XVar_ImSlate3DIm(
	TEXT("imslate.3d.im"),
	TEXT("imslate.3d.im [bShow=1] — immediate-mode (Begin/End) window projected 3D, billboarding to camera"),
	[](TOptional<bool> bShow, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true))
		{
			ImSlate3DImDemo::GOpen = true;
			ImSlate3DImDemo::GTickHandle = ImSlate::ImSlateTicker::BindDelegate(
				ImSlate::ImSlateTicker::FOnTickWithWorld::CreateStatic(&ImSlate3DImDemo::Tick), InWorld);
			Ar.Log(TEXT("imslate.3d.im: shown"));
		}
		else
		{
			ImSlate3DImDemo::GTickHandle.Reset();
			Ar.Log(TEXT("imslate.3d.im: hidden"));
		}
	});

// imslate.3d.gizmo3 — standalone 3-axis WORLD transform gizmo (Stage A: move only). Three INDEPENDENT
// SImSlate3DArrow widgets in self-projector mode, all anchored at ONE shared world point GAnchor. Each drags
// along its true WORLD axis (X/Y/Z, not camera-relative); dragging any axis rewrites GAnchor and a per-frame
// tick re-anchors all three (so the other two follow). Axes that face the camera draw reversed (ReverseWhenBackface).
// This shared-FVector loop is the no-callback fallback of the eventual FTransform Query/Edit contract.
// Live-tunable gizmo on-screen scale (demo knob): change in console → the gizmo rebuilds at the new size next tick.
static TAutoConsoleVariable<float> CVarGizmo3Scale(
	TEXT("imslate.3d.gizmoscale"), 1.0f,
	TEXT("On-screen size multiplier for imslate.3d.gizmo3 (1 = built-in px sizes). Rebuilds live when changed."));

static TAutoConsoleVariable<int32> CVarGizmo3Space(
	TEXT("imslate.3d.gizmospace"), 0,
	TEXT("imslate.3d.gizmo3 handle axis space: 0 = World (handles stay world X/Y/Z — render AND constraint both use "
	     "world axes, fully consistent), 1 = Local (handles spin with the object's rotation; NOTE constraint axes do "
	     "not yet follow rotation, so Local drag won't match the tilted handles — use World for now). Applies live."));

namespace ImSlate3DGizmo3Demo
{
	static TSharedPtr<ImSlate::FImSlate3DGizmo> GGizmo;                       // the manager (owns axes + handles)
	static TArray<TWeakPtr<ImSlate::SImSlate3DArrowHitClip>> GClips;          // arrow viewport wrappers
	static TArray<TWeakPtr<ImSlate::SImSlate3DConstraintHandle>> GHandles;    // scale/plane/rotate handle widgets
	static FTransform GXform = FTransform::Identity;                          // the demo's "scene data" (drag edits it)
	static bool GSnap = false;                                               // EditTransform clamp demo: 10uu grid snap
	static float GAppliedScale = -1.f;                                       // last scale built with (detect cvar change)
	static TSharedPtr<void> GTickHandle;
	static bool GInit = false;

	static void Hide()
	{
		GTickHandle.Reset();
		if (GGizmo.IsValid())
		{
			for (const TSharedPtr<ImSlate::SImSlate3DArrow>& A : GGizmo->GetArrowWidgets())
			{
				if (A.IsValid()) { ImSlate::FImSlate3DHitManager::Get().Unregister(A.Get()); }
			}
			for (const TSharedPtr<ImSlate::SImSlate3DConstraintHandle>& H : GGizmo->GetConstraintHandleWidgets())
			{
				if (H.IsValid()) { ImSlate::FImSlate3DHitManager::Get().Unregister(H.Get()); }
			}
		}
		if (GEngine && GEngine->GameViewport)
		{
			for (TWeakPtr<ImSlate::SImSlate3DArrowHitClip>& W : GClips)
			{
				if (TSharedPtr<ImSlate::SImSlate3DArrowHitClip> C = W.Pin())
				{
					GEngine->GameViewport->RemoveViewportWidgetContent(C.ToSharedRef());
				}
			}
			for (TWeakPtr<ImSlate::SImSlate3DConstraintHandle>& W : GHandles)
			{
				if (TSharedPtr<ImSlate::SImSlate3DConstraintHandle> H = W.Pin())
				{
					GEngine->GameViewport->RemoveViewportWidgetContent(H.ToSharedRef());
				}
			}
			if (GGizmo.IsValid() && GGizmo->GetHudWidget().IsValid())
			{
				GEngine->GameViewport->RemoveViewportWidgetContent(StaticCastSharedRef<SWidget>(GGizmo->GetHudWidget().ToSharedRef()));
			}
		}
		GClips.Reset();
		GHandles.Reset();
		GGizmo.Reset();
	}

	static void Show(UWorld* World);  // fwd decl (Tick rebuilds on a live scale change)

	// Per-frame: re-anchor all axes; also do the ONE-TIME placement here (not in Show) so it retries each frame
	// until the camera/host data is ready — at the instant the console command runs the host's ViewProj may not
	// be filled yet, which left the gizmo at the origin (0,0,0), off-screen. (R: gizmo not shown)
	static void Tick(float /*Dt*/, UWorld* World)
	{
		// Live scale knob: if imslate.3d.gizmoscale changed, rebuild the gizmo at the new size (GXform/pose kept).
		const float WantScale = CVarGizmo3Scale.GetValueOnGameThread();
		if (GGizmo.IsValid() && !FMath::IsNearlyEqual(WantScale, GAppliedScale) && World)
		{
			Show(World);   // full rebuild at the new scale; GInit stays true so the pose is preserved
			return;
		}
		if (!GInit && World)
		{
			// Place via the player camera (known-good). Also log ImGetViewCentre's result alongside so we can verify
			// the helper without trusting it for placement yet (the helper was giving a wrong, too-close anchor).
			ULocalPlayer* LP = GEngine ? GEngine->GetLocalPlayerFromControllerId(World, 0) : nullptr;
			APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
			if (PC && PC->PlayerCameraManager)
			{
				const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
				const FQuat CamQ = PC->PlayerCameraManager->GetCameraRotation().Quaternion();
				const FVector Fwd = CamQ.GetForwardVector();
				GXform.SetLocation(CamLoc + Fwd * 400.f);
				// World-axis mode: identity rotation so the three axes ARE world X/Y/Z (render ≡ constraint, fully
				// consistent). The 3/4-view tilt belongs to Local mode, which isn't drag-consistent yet.
				GXform.SetRotation(FQuat::Identity);
				GInit = true;

				const ImSlate::FImViewPoint VC = ImSlate::ImGetViewCentre(400.0);
				UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][gizmo3] camPlace=(%.0f,%.0f,%.0f) | helper bValid=%d loc=(%.0f,%.0f,%.0f) cam=(%.0f,%.0f,%.0f)"),
					GXform.GetLocation().X, GXform.GetLocation().Y, GXform.GetLocation().Z,
					VC.bValid ? 1 : 0, VC.Location.X, VC.Location.Y, VC.Location.Z, VC.CameraPos.X, VC.CameraPos.Y, VC.CameraPos.Z);
			}
		}
		if (GGizmo.IsValid())
		{
			// Live Local/World axis-space toggle (verify both spaces behave correctly).
			GGizmo->SetAxisSpace(CVarGizmo3Space.GetValueOnGameThread() == 0
				? ImSlate::EImAxisSpace::World : ImSlate::EImAxisSpace::Local);
			GGizmo->Refresh();
		}
	}

	static void Show(UWorld* World)
	{
		if (!World || !GEngine || !GEngine->GameViewport) { return; }
		Hide();
		ULocalPlayer* LP = GEngine->GetLocalPlayerFromControllerId(World, 0);
		APlayerController* PC = LP ? LP->GetPlayerController(World) : nullptr;
		if (!PC || !PC->PlayerCameraManager) { return; }

		// Placement is done in Tick (retries until camera/host data is ready). GInit=false here → Tick will place it.
		GGizmo = MakeShared<ImSlate::FImSlate3DGizmo>();
		// FTransform Query/Edit contract: the gizmo reads/writes the demo's GXform through these. EditTransform
		// demonstrates CLAMPING — with snap on, the dragged location is quantized to a 10uu grid before applying.
		GGizmo->SetQueryTransform(ImSlate::FImSlate3DGizmo::FOnQueryTransform::CreateLambda([]() { return GXform; }));
		GGizmo->SetEditTransform(ImSlate::FImSlate3DGizmo::FOnEditTransform::CreateLambda([](const FTransform& Wanted)
		{
			FTransform Applied = Wanted;
			if (GSnap)
			{
				FVector L = Wanted.GetLocation();
				L.X = FMath::GridSnap(L.X, 10.0); L.Y = FMath::GridSnap(L.Y, 10.0); L.Z = FMath::GridSnap(L.Z, 10.0);
				Applied.SetLocation(L);
			}
			GXform = Applied;   // commit to the demo's scene data
			return Applied;     // gizmo shows the clamped value
		}));
		// Demo knob (NOT a framework default): on-screen size from the imslate.3d.gizmoscale cvar so you can tune
		// it live (Tick rebuilds when it changes). Must precede Build* (it scales their pixel sizes).
		GAppliedScale = CVarGizmo3Scale.GetValueOnGameThread();
		GGizmo->SetUniformScale(GAppliedScale);
		GGizmo->BuildAxes();

		GGizmo->BuildScaleHandles();   // three world-axis box handles  → FImScaleConstraint
		GGizmo->BuildPlaneHandles();   // three XY/YZ/ZX corner squares → FImPlaneConstraint
		GGizmo->BuildRotateHandles();  // three rings about X/Y/Z       → FImRotateConstraint

		const int32 BaseZ = CVarImSlate3DZOrder.GetValueOnGameThread();
		for (const TSharedPtr<ImSlate::SImSlate3DArrow>& A : GGizmo->GetArrowWidgets())
		{
			if (!A.IsValid()) { continue; }
			TSharedRef<ImSlate::SImSlate3DArrowHitClip> Clip = SNew(ImSlate::SImSlate3DArrowHitClip).Arrow(A.ToSharedRef());
			GEngine->GameViewport->AddViewportWidgetContent(Clip, BaseZ + 1);
			ImSlate::FImSlate3DHitManager::Get().Register(StaticCastSharedRef<ImSlate::IImSlate3DHittable>(A.ToSharedRef()));
			GClips.Add(Clip);
		}
		// Constraint handles (scale): each is its own 2D entry widget — add directly (no HitClip; it claims only
		// its own shape, passing other clicks through) + register with the hit manager.
		for (const TSharedPtr<ImSlate::SImSlate3DConstraintHandle>& H : GGizmo->GetConstraintHandleWidgets())
		{
			if (!H.IsValid()) { continue; }
			GEngine->GameViewport->AddViewportWidgetContent(H.ToSharedRef(), BaseZ + 2);
			ImSlate::FImSlate3DHitManager::Get().Register(StaticCastSharedRef<ImSlate::IImSlate3DHittable>(H.ToSharedRef()));
			GHandles.Add(H);
		}
		// Drag HUD (ghost origin + Δ readout): topmost, HitTestInvisible → never registered with the hit manager.
		GGizmo->BuildHud();
		if (GGizmo->GetHudWidget().IsValid())
		{
			GEngine->GameViewport->AddViewportWidgetContent(StaticCastSharedRef<SWidget>(GGizmo->GetHudWidget().ToSharedRef()), BaseZ + 3);
		}

		GTickHandle = ImSlate::ImSlateTicker::BindDelegate(
			ImSlate::ImSlateTicker::FOnTickWithWorld::CreateStatic(&ImSlate3DGizmo3Demo::Tick), World);
	}
}

FXConsoleCommandLambdaFull XVar_ImSlate3DGizmo3(
	TEXT("imslate.3d.gizmo3"),
	TEXT("imslate.3d.gizmo3 [bShow=1] [bSnap=0] — standalone WORLD transform gizmo via FImSlate3DGizmo + "
	     "FTransform Query/Edit: arrows=move, tip boxes=scale, corner squares=plane-move, rings=rotate. All "
	     "drive ONE FTransform through one Edit clamp. bSnap=1 → 10uu grid clamp (callback clamping demo). 0 hide."),
	[](TOptional<bool> bShow, TOptional<bool> bSnap, UWorld* InWorld, FOutputDevice& Ar)
	{
		if (bShow.Get(true))
		{
			ImSlate3DGizmo3Demo::GSnap = bSnap.Get(false);
			ImSlate3DGizmo3Demo::GInit = false;   // re-place at the camera each time the command is run (scale-rebuild keeps the pose)
			ImSlate3DGizmo3Demo::Show(InWorld);
			Ar.Logf(TEXT("imslate.3d.gizmo3: shown (snap=%d)"), ImSlate3DGizmo3Demo::GSnap ? 1 : 0);
		}
		else
		{
			ImSlate3DGizmo3Demo::Hide();
			Ar.Log(TEXT("imslate.3d.gizmo3: hidden"));
		}
	});

#endif  // defined(IMSLATE3D_API)
