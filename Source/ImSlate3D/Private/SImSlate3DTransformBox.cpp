// Copyright Epic Games, Inc. All Rights Reserved.
#include "SImSlate3DTransformBox.h"

#if defined(IMSLATE3D_API)

#include "Rendering/DrawElements.h"
#include "Layout/Clipping.h"               // FSlateClippingZone (trapezoid hit-clip)
#include "Rendering/DrawElementTypes.h"
#include "Rendering/DrawElementCoreTypes.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateShaderResource.h"  // FSlateShaderResourceProxy (shader path reads box brush texture)
#include "Fonts/FontCache.h"               // FSlateFontCache, glyph atlas data
#include "ImSlate3DHitTest.h"              // FIm3DHitTestPath
#include "ImSlate3DShaderElement.h"        // FIm3DShaderElement / FIm3DQuad (separate ImSlate3D module)
#include "ImSlate3DProjection.h"           // ImSlate::EmitCapturedElementQuads / AppendWorldSegmentQuad3D (ImSlate3D module)
#include "ImSlate3DShapedHit.h"            // IImSlate3DPlaneShaped / AsPlaneShaped (per-pixel shape narrow-phase)
#include "ImSlate3DHost.h"                  // FImSlate3DHost — Engine-coupled hooks (camera/viewport) injected by main module
#include "SImSlate3DArrow.h"              // GetIm3DArrowStyle / EImArrowStyle (single GizmoStyle CVar source)
#include "Widgets/SViewport.h"             // SetCustomHitTestPath (SViewport type; the viewport is fetched via the host hook)
#include "Algo/Reverse.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarImSlate3DHitProbe(
	TEXT("imslate.3d.HitProbe"), 0,
	TEXT("Draw a cross at the current cursor mapped through the 3D widget (cursor → viewport px → Unproject to "
	     "the widget plane → Project back to screen). If the cross sits on the real cursor, the projection/"
	     "hit round-trip is self-consistent; drift exposes a coordinate-space bug. 1=on."));

static TAutoConsoleVariable<int32> CVarImSlate3DForceLayer(
	TEXT("imslate.3d.ForceLayer"), -1,
	TEXT("Debug override of EVERY 3D box's occlusion hit-layer: -1=use each box's own HitLayer (default), "
	     "0=Top (full-screen Gateway, above all UI), 1=Normal (box widget on its own layer), "
	     "2=Bottom (ICustomHitTestPath on the SViewport, only fires with nothing above)."));

namespace ImSlate
{
int32 GetIm3DForcedLayer()
{
	const int32 V = CVarImSlate3DForceLayer.GetValueOnGameThread();
	return (V >= 0 && V <= 2) ? V : INDEX_NONE;
}

void SImSlate3DTransformBox::Construct(const FArguments& InArgs)
{
	// Pure 3D-ify container: just host the user content. The box knows nothing about gizmos — shaped children
	// (e.g. SImSlate3DPlaneArrow arrows the caller places in their own SOverlay) declare their shape/bounds via
	// IImSlate3DPlaneShaped, and the box's capture pipeline projects everything onto the 3D plane.
	ChildSlot
	[
		InArgs._Content.Widget
	];

	// Hit-testing is done by THIS widget directly (see OnMouse* below): the box itself is hittable so
	// it receives the cursor over its rect, unprojects it, and forwards to the child subtree. We do NOT
	// rely on the engine's ICustomHitTestPath (it only fires when the cursor reaches the game SViewport,
	// which an overlay covers).
}

SImSlate3DTransformBox::~SImSlate3DTransformBox()
{
	UnregisterHitPath();
	FImSlate3DHitManager::Get().Unregister(this);
}

void SImSlate3DTransformBox::SetWorldPlacement(const FImWorldPlacement& InPlacement)
{
	Placement = InPlacement;
	bHasPlacement = true;
	ApplyHitLayer();  // visibility + hit-path registration (heavy; not called per drag frame)
	// Always join the shared 3D hit dispatch: Top/Normal route through the manager (HandleEvent walks all
	// registered boxes); Bottom routes via the engine custom-path but registering is harmless (manager only
	// dispatches when an entry calls HandleEvent). Register de-dups.
	FImSlate3DHitManager::Get().Register(StaticCastSharedRef<IImSlate3DHittable>(
		StaticCastSharedRef<SImSlate3DTransformBox>(AsShared())));
}

void SImSlate3DTransformBox::SetHitLayer(EImHitLayer InLayer)
{
	HitLayer = InLayer;
	if (bHasPlacement) { ApplyHitLayer(); }
}

EImHitLayer SImSlate3DTransformBox::ResolvedHitLayer() const
{
	// Debug ForceLayer override wins, else this box's own HitLayer.
	const int32 Forced = GetIm3DForcedLayer();
	return (Forced != INDEX_NONE) ? (EImHitLayer)Forced : HitLayer;
}

void SImSlate3DTransformBox::ApplyHitLayer()
{
	const EImHitLayer Layer = ResolvedHitLayer();
	switch (Layer)
	{
	case EImHitLayer::Bottom:
		// Sink to the SViewport: the box must NOT swallow the cursor so it falls through to the SViewport
		// where the engine invokes FIm3DHitTestPath (engine-driven; only fires with nothing visible above).
		SetVisibility(EVisibility::HitTestInvisible);
		RegisterHitPath();
		break;
	case EImHitLayer::Top:
		// A full-screen Gateway (installed by the caller) sits on top and feeds events to the manager, which
		// routes to this box. The box itself must not also grab the cursor → HitTestInvisible. No custom-path.
		SetVisibility(EVisibility::HitTestInvisible);
		UnregisterHitPath();
		break;
	case EImHitLayer::Normal:
	default:
		// The box is its own hittable widget on its layer: it grabs the cursor over its rect and self-routes.
		SetVisibility(EVisibility::Visible);
		UnregisterHitPath();
		break;
	}
}

void SImSlate3DTransformBox::RegisterHitPath()
{
	if (bRegisteredHitPath)
	{
		return;
	}
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	TSharedPtr<SViewport> Viewport = Host.GetGameViewport ? Host.GetGameViewport() : nullptr;
	if (!Viewport.IsValid())
	{
		return;
	}
	// One shared FIm3DHitTestPath per viewport; create on first box, then register into it.
	TSharedPtr<ICustomHitTestPath> Existing = Viewport->GetCustomHitTestPath();
	TSharedPtr<FIm3DHitTestPath> Path = StaticCastSharedPtr<FIm3DHitTestPath>(Existing);
	if (!Path.IsValid())
	{
		Path = MakeShared<FIm3DHitTestPath>();
		Viewport->SetCustomHitTestPath(Path);
	}
	Path->RegisterBox(StaticCastSharedRef<SImSlate3DTransformBox>(AsShared()));
	bRegisteredHitPath = true;
}

void SImSlate3DTransformBox::UnregisterHitPath()
{
	if (!bRegisteredHitPath)
	{
		return;
	}
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	if (TSharedPtr<SViewport> Viewport = Host.GetGameViewport ? Host.GetGameViewport() : nullptr)
	{
		TSharedPtr<FIm3DHitTestPath> Path = StaticCastSharedPtr<FIm3DHitTestPath>(Viewport->GetCustomHitTestPath());
		if (Path.IsValid())
		{
			Path->UnregisterBox(this);
			if (Path->NumRegistered() == 0)
			{
				Viewport->SetCustomHitTestPath(nullptr);
			}
		}
	}
	bRegisteredHitPath = false;
}

FVector2f SImSlate3DTransformBox::GetGizmoAnchorLocal() const
{
	// Bottom-left corner, nudged inward so an arrow hanging off it isn't clipped at the very edge.
	const float Inset = 12.f;
	return FVector2f(Inset, FMath::Max(CachedWidgetSize.Y - Inset, 0.f));
}

bool SImSlate3DTransformBox::BuildProjector(FVector2f WidgetSize, FImProjector& OutProj) const
{
	// Camera projection comes through the host hook (the Engine-coupled query lives in the main module,
	// so this widget — and the whole 3D module — never links Engine directly).
	const FImSlate3DHost& Host = FImSlate3DHost::Get();
	if (!Host.GetCameraProjData)
	{
		return false;
	}
	const FImCameraProjData Data = Host.GetCameraProjData();
	if (!Data.bValid)
	{
		return false;
	}
	OutProj.Build(WidgetSize, Placement, Data.ViewProj, Data.ViewRect);
	return true;
}

int32 SImSlate3DTransformBox::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// No placement → behave as a plain passthrough container.
	if (!bHasPlacement)
	{
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	// 3D content size = the CHILD's desired size, NOT AllottedGeometry. When this panel is added via
	// AddViewportWidgetContent it lives in a viewport overlay slot that STRETCHES it to fill the whole
	// screen — so AllottedGeometry.GetLocalSize() is the full viewport (e.g. 2891x1080), not the inner
	// content (e.g. 400x300). Laying the child out at full-screen size scatters every element across a
	// huge local space; projected onto the small 3D quad they fly off-screen (button/title vanished).
	// The child's DesiredSize is the real content extent the 3D plane should represent.
	const FVector2f ChildDesired = (FVector2f)ChildSlot.GetWidget()->GetDesiredSize();
	const FVector2f AllottedSize = (FVector2f)AllottedGeometry.GetLocalSize();
	const FVector2f Size = (ChildDesired.X > 0.f && ChildDesired.Y > 0.f) ? ChildDesired : AllottedSize;
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return LayerId;
	}

	FImProjector Proj;
	if (!BuildProjector(Size, Proj) || !Proj.IsVisible())
	{
		bProjectorValid = false;
		return LayerId;  // no camera, or backface-culled
	}

	// Cache for hit-testing — same projection as this rendered frame.
	CachedProjector = Proj;
	CachedWidgetSize = Size;
	bProjectorValid = true;

	// --- Capture the child's draw elements into a throwaway list, in IDENTITY local space ---
	// We arrange the child against a default (identity) geometry so each captured element's
	// accumulated render transform maps its local points straight to child-local pixels
	// (origin = our top-left, scale = 1). We then re-project those pixels through the camera.
	FArrangedChildren MyArranged(EVisibility::Visible);
	this->ArrangeChildren(AllottedGeometry, MyArranged);  // resolves which child + visibility
	if (MyArranged.Num() != 1)
	{
		return LayerId;
	}

	// Paint the child against an IDENTITY geometry into a throwaway list, so each captured
	// element's points land in child-local pixels (origin = our top-left, scale = 1). No real
	// window is needed — we only read the collected element descriptions and re-emit ourselves.
	const FGeometry ChildGeom = FGeometry().MakeChild(Size, FSlateLayoutTransform());
	TSharedPtr<SWindow> NullWindow;
	FSlateWindowElementList CaptureList(NullWindow);
	const FSlateRect WideCull(FVector2f(-1e6f, -1e6f), FVector2f(1e6f, 1e6f));  // no culling in identity space
	MyArranged[0].Widget->Paint(Args.WithNewParent(this), ChildGeom, WideCull, CaptureList, 0, InWidgetStyle, bParentEnabled);

	// Single render path: custom GPU perspective shader (true per-pixel W, correct rounded-box + A8 font).
	// The old CPU MakeCustomVerts path was removed — it can't render font atlases correctly (custom-verts
	// batches are forced to ESlateShader::Custom, which never hits the grayscale-font branch, so PF_A8
	// glyphs come out black). See regressions.md R005 + design.md.
	const int32 NextLayer = ProjectCapturedElementsShader(CaptureList, Proj, OutDrawElements, LayerId);

	// imslate.3d.HitProbe: draw a cross where the live cursor maps onto the widget plane (cursor → viewport
	// px → Unproject → Project back). If it sits on the real cursor, the hit/projection round-trip is
	// self-consistent; drift reveals a coordinate bug.
	if (CVarImSlate3DHitProbe.GetValueOnGameThread() != 0 && bHasLastCursor)
	{
		// Verify the cursor → world-plane → back-to-screen round-trip. Local (Unproject) is the WIDGET-PLANE
		// coord (NOT a screen pos), so Project it back to screen, then convert that viewport px → paint local
		// to draw. CursorLocal = real OS cursor in paint local. The two crosses' separation = the offset.
		// SPACE FIX (P2): the cursor (LastCursorScreenPos) is a DESKTOP-space coord (Event.GetScreenSpacePosition).
		// AllottedGeometry is WINDOW space (DesktopGeometry = Allotted + WindowToDesktopTransform, see
		// SWidget.cpp). Feeding a desktop coord into AllottedGeometry.AbsoluteToLocal mismatches by
		// WindowToDesktop (was dCurVP≈15.5px). Use GetCachedGeometry() (= DesktopGeometry, desktop space) for
		// BOTH the cursor mapping AND the paint geometry so the cross lands on the real cursor — same space the
		// hit-test already uses (which is why the arrows are pixel-accurate).
		const FGeometry& ProbeGeom = GetCachedGeometry();
		const FVector2f CurVP = MapCursorToViewport(ProbeGeom, LastCursorScreenPos);
		const TOptional<FVector2f> Local = Proj.Unproject(CurVP);
		const FVector2f CursorLocal = (FVector2f)ProbeGeom.AbsoluteToLocal(LastCursorScreenPos);
		{
			{
				if (Local.IsSet())
				{
					const TOptional<FVector2f> BackVP = Proj.Project(*Local);
					// viewport px → widget local: INVERSE of MapCursorToViewport (= AbsoluteToLocal * Scale +
					// Min), so (BackVP - Min) / Scale. (Was ×LocalSize/ViewRect.Size — the fill-viewport
					// assumption; kept self-consistent with the forward map's *Scale fix.)
					const FIntRect VR = Proj.GetViewRect();
					const float ProbeScale = FMath::Max(ProbeGeom.Scale, UE_SMALL_NUMBER);
					const FVector2f BVP = BackVP.IsSet() ? *BackVP : CurVP;
					const FVector2f P(
						(BVP.X - (float)VR.Min.X) / ProbeScale,
						(BVP.Y - (float)VR.Min.Y) / ProbeScale);
					if (IsImSlate3DDebugVerbose())
					{
						const float Drift = (P - CursorLocal).Size();
						const bool bMoved = (LastCursorScreenPos - ProbeLastLoggedCursor).SizeSquared() > 4.f;
						if (bMoved)
						{
							ProbeLastLoggedCursor = LastCursorScreenPos;
							// KEY: does the cross's paint-local render back to the real cursor? If
							// LocalToAbsolute(CursorLocal) != desktop, paint↔screen isn't the inverse of
							// AbsoluteToLocal here (geometry changed between event & paint, or non-invertible).
							const FVector2f BackToDesktop = (FVector2f)ProbeGeom.LocalToAbsolute(CursorLocal);
							const float ScreenErr = (BackToDesktop - (FVector2f)LastCursorScreenPos).Size();
							// Compare the two geometries: hit uses GetCachedGeometry(), probe paints with
							// AllottedGeometry. If their AbsoluteToLocal differ for the SAME desktop point, that
							// mismatch IS the offset (cached vs allotted geometry diverge).
							const FVector2f CachedLocal = (FVector2f)GetCachedGeometry().AbsoluteToLocal(LastCursorScreenPos);
							// DIAG-P2: probe uses AllottedGeometry for CurVP; hit uses GetCachedGeometry(). Quantify
							// the divergence. CurVPcached = what the HIT path actually feeds Unproject. dCurVP =
							// how far probe's input pixel is from the hit input pixel (= the WindowToDesktop offset
							// pushed through the projector scale). roundVP = Project(Unproject(CurVP)) residual:
							// near 0 ⇒ projection闭合(偏不在投影,在geometry); 大 ⇒ 投影本身不闭合.
							const FVector2f CurVPcached = MapCursorToViewport(GetCachedGeometry(), LastCursorScreenPos);
							const float dCurVP = (CurVP - CurVPcached).Size();
							const float roundVP = (BVP - CurVP).Size();
							UE_LOG(LogTemp, Warning, TEXT("[ImSlate3D][probe] screenErr=%.1f desktop=(%.0f,%.0f) allottedLocal=(%.1f,%.1f) cachedLocal=(%.1f,%.1f) back2desktop=(%.0f,%.0f) CurVP=(%.1f,%.1f) CurVPcached=(%.1f,%.1f) dCurVP=%.2f roundVP=%.2f Drift=%.2f"),
								ScreenErr, LastCursorScreenPos.X, LastCursorScreenPos.Y, CursorLocal.X, CursorLocal.Y, CachedLocal.X, CachedLocal.Y, BackToDesktop.X, BackToDesktop.Y, CurVP.X, CurVP.Y, CurVPcached.X, CurVPcached.Y, dCurVP, roundVP, Drift);
						}
					}
					const float R = 16.f;
					// CursorLocal/P are widget-LOCAL coords computed via ProbeGeom (= GetCachedGeometry() =
					// DesktopGeometry). DesktopGeometry = AllottedGeometry ⊕ WindowOffset, so they share the SAME
					// widget-local origin — the local value is identical. But the PAINT CANVAS (OutDrawElements)
					// is WINDOW space, so PG must use AllottedGeometry (window), NOT ProbeGeom (desktop): painting
					// a window-space local through the desktop geometry's render transform double-adds WindowOffset
					// → the cross drifts off the cursor. (This was the residual probe offset after dCurVP hit 0.)
					const FPaintGeometry PG = AllottedGeometry.ToPaintGeometry();
					// Each marker = ONE right-angle corner (not a full cross), so even when the two overlap you
					// can still tell them apart. YELLOW = real OS cursor (top-left corner ⌐).
					// CYAN = cursor→world-plane→back (bottom-right corner ⌟).
					auto DrawCornerTL = [&](const FVector2f& C, const FLinearColor& Col)
					{
						TArray<FVector2f> L = { FVector2f(C.X, C.Y - R), FVector2f(C.X, C.Y), FVector2f(C.X + R, C.Y) };
						FSlateDrawElement::MakeLines(OutDrawElements, NextLayer, PG, L, ESlateDrawEffect::None, Col, true, 2.f);
					};
					auto DrawCornerBR = [&](const FVector2f& C, const FLinearColor& Col)
					{
						TArray<FVector2f> L = { FVector2f(C.X, C.Y + R), FVector2f(C.X, C.Y), FVector2f(C.X - R, C.Y) };
						FSlateDrawElement::MakeLines(OutDrawElements, NextLayer, PG, L, ESlateDrawEffect::None, Col, true, 2.f);
					};
					DrawCornerTL(CursorLocal, FLinearColor(1.f, 1.f, 0.f, 1.f));  // yellow ⌐ = real cursor (AbsoluteToLocal)
					DrawCornerBR(P,           FLinearColor(0.f, 1.f, 1.f, 1.f));  // cyan   ⌟ = mapped point
					// RED ⌐ = the raw desktop coord drawn AS paint-local (no AbsoluteToLocal). If RED sits on the
					// cursor but YELLOW doesn't, ToPaintGeometry rendering ≠ AbsoluteToLocal inverse (render-xf issue).
					DrawCornerTL((FVector2f)LastCursorScreenPos, FLinearColor(1.f, 0.f, 0.f, 1.f));
				}
			}
		}
		return NextLayer + 1;
	}
	return NextLayer;
}

// renderMode=shader: build clip-space quads (real perspective W) and submit one custom GPU element.
// First pass = solid color only (bUseTexture=false). Texture/text come in M6-d.
int32 SImSlate3DTransformBox::ProjectCapturedElementsShader(const FSlateWindowElementList& CaptureList,
	const FImProjector& Proj, FSlateWindowElementList& OutDrawElements, int32 BaseLayer) const
{
	// Reusable core (ImSlate3D module): capture list → projected screen quads, no widget/Engine deps.
	// FontCache comes from this (Slate-linked) module; the low-dependency ImSlate3D module takes it as a param.
	FSlateFontCache* FontCache = &FSlateApplication::Get().GetRenderer()->GetFontCache().Get();
	TArray<FIm3DQuad> Quads;
	ImSlate::EmitCapturedElementQuads(CaptureList, Proj, FontCache, Quads);

	// Apply the projector's effective alpha (Opacity sign-encoding: both-sides / back-only) uniformly to every
	// projected vertex — one place, covers all element types (box/glyph/line/...). GPU already alpha-blends
	// (SrcAlpha/InvSrcAlpha), so this just scales each vertex color's alpha (.W). 1.0 → no-op (fast path).
	const float EffAlpha = Proj.GetEffectiveAlpha();
	if (EffAlpha < 1.f)
	{
		for (FIm3DQuad& Q : Quads)
		{
			for (FImSlate3DVertex& V : Q.Verts) { V.Color.W *= EffAlpha; }
			Q.OutlineColor.W *= EffAlpha;
		}
	}

	if (Quads.Num() > 0)
	{
		// MUST keep a strong ref alive past OnPaint: the draw element holds CustomDrawer as a TWeakPtr,
		// so a local TSharedPtr would die here and the batcher's Pin() would null it out before
		// Draw_RenderThread runs. Store it on the widget; replaced next paint (prior frame already drawn).
		KeepAliveShaderElement = MakeShared<FIm3DShaderElement, ESPMode::ThreadSafe>(MoveTemp(Quads), Proj.GetViewRect());
		FSlateDrawElement::MakeCustom(OutDrawElements, BaseLayer, KeepAliveShaderElement);
	}
	else
	{
		KeepAliveShaderElement.Reset();
	}
	return BaseLayer + 1;
}

// ---------------- M4: true-perspective hit-test via ICustomHitTestPath ----------------

FGeometry SImSlate3DTransformBox::GetChildLayoutGeometry() const
{
	// Identity root: child-local pixels == the coordinates the children were laid out in (and the
	// space FImProjector unprojects into).
	return FGeometry().MakeChild(CachedWidgetSize, FSlateLayoutTransform());
}

bool SImSlate3DTransformBox::OwnsWidget(const SWidget& ChildWidget) const
{
	TSharedPtr<SWidget> Root = ChildSlot.GetWidget();
	if (!Root.IsValid())
	{
		return false;
	}
	// BFS over the child subtree.
	TArray<TSharedRef<SWidget>> Stack;
	Stack.Add(Root.ToSharedRef());
	for (int32 Guard = 0; Guard < 4096 && Stack.Num() > 0; ++Guard)
	{
		TSharedRef<SWidget> W = Stack.Pop();
		if (&W.Get() == &ChildWidget)
		{
			return true;
		}
		FChildren* Kids = W->GetChildren();
		if (Kids)
		{
			for (int32 i = 0; i < Kids->Num(); ++i)
			{
				Stack.Add(Kids->GetChildAt(i));
			}
		}
	}
	return false;
}

bool SImSlate3DTransformBox::BuildHitPath(FVector2f ViewportCursor, bool bIgnoreEnabledStatus,
	TArray<FWidgetAndPointer>& OutPath) const
{
	// Unified hit calculation: reuse the SAME BuildLocalPath the Top/Normal layers use (it already does the
	// shaped narrow-phase via AsPlaneShaped, so a gizmo arrow is precise on the Bottom/custom-path layer too —
	// fixes R013's coarse gizmo hit). Then wrap each FArrangedWidget as the engine's FWidgetAndPointer, with
	// the unprojected plane-local point as the virtual pointer so the engine routes hover/click there.
	// Virtual pointer = the unprojected plane-local point (same value the engine should route every widget in
	// the path at — it's the cursor's position in the box's identity child space, which BuildLocalPath uses).
	const FImPlaneHit Hit = CachedProjector.MapScreenToPlane(ViewportCursor);
	if (!Hit.bValid)
	{
		return false;
	}
	const FVector2D VPLocal(Hit.Local.X, Hit.Local.Y);
	const FVirtualPointerPosition VP(VPLocal, VPLocal);

	TArray<FArrangedWidget> Path;
	if (!BuildLocalPath(ViewportCursor, Path) || Path.Num() == 0)
	{
		return false;  // outside the projected quad / behind camera
	}
	for (const FArrangedWidget& A : Path)
	{
		OutPath.Add(FWidgetAndPointer(A, VP));
	}
	// FHittestGrid bubble order = leaf-first; BuildLocalPath returns root→leaf, so reverse.
	Algo::Reverse(OutPath);
	return OutPath.Num() > 0;
}

void SImSlate3DTransformBox::ArrangeForCustomHitTest(FArrangedChildren& ArrangedChildren) const
{
	TSharedPtr<SWidget> Child = ChildSlot.GetWidget();
	if (Child.IsValid())
	{
		ArrangedChildren.AddWidget(FArrangedWidget(Child.ToSharedRef(), GetChildLayoutGeometry()));
	}
}

TOptional<FVector2f> SImSlate3DTransformBox::TranslateCursor(const SWidget& ChildWidget, FVector2f ViewportCursor) const
{
	if (!bHasPlacement || !bProjectorValid || !OwnsWidget(ChildWidget))
	{
		return TOptional<FVector2f>();
	}
	return CachedProjector.Unproject(ViewportCursor);
}

// ---- Self route: box handles the event, unprojects, routes to the deepest child ----

bool SImSlate3DTransformBox::BuildLocalPath(FVector2f ViewportCursor, TArray<FArrangedWidget>& OutPath) const
{
	if (!bHasPlacement || !bProjectorValid)
	{
		return false;
	}
	// Use MapScreenToPlane (NOT Unproject): it returns bValid (ray hit the infinite sheet plane, Local solved,
	// may be <0 or >Size) separately from bInsideRect ([0..Size]). A shaped child (gizmo arrow) may extend
	// OUTSIDE the panel rect — it must still get a plane-local point to hit-test. Plain rect controls are kept
	// strictly inside the panel by checking bInsideRect per-child below.
	const FImPlaneHit Hit = CachedProjector.MapScreenToPlane(ViewportCursor);
	if (!Hit.bValid)
	{
		return false;  // ray missed the plane / behind camera / backface → pass through
	}
	const FVector2f LocalPt = Hit.Local;        // plane-local px, may be outside [0..Size]
	const bool bInsideRect = Hit.bInsideRect;   // true only when within the panel quad

	const FGeometry RootGeom = GetChildLayoutGeometry();
	const FVector2f AbsPoint = (FVector2f)RootGeom.LocalToAbsolute(LocalPt);

	// Depth-first descent WITH backtracking. A SelfHitTestInvisible wrapper (self not hittable but children
	// are — e.g. SBox/SBorder around an SButton) must be descended into; but the dormant SImSlate3DPlaneArrow is
	// ALSO SelfHitTestInvisible and full-screen, so a greedy "first child that contains the point" wrongly
	// picked the gizmo dead-end and never reached the button. DFS tries each containing child top-most first
	// and only commits a branch that actually reaches a hit-testable leaf (or a self-hittable widget).
	// Returns true if a usable path was appended to OutPath.
	TFunction<bool(const TSharedRef<SWidget>&, const FGeometry&)> Descend =
		[&](const TSharedRef<SWidget>& W, const FGeometry& Geom) -> bool
	{
		OutPath.Add(FArrangedWidget(W, Geom));
		FArrangedChildren Arranged(EVisibility::Visible);
		W->ArrangeChildren(Geom, Arranged);
		// Top-most (last arranged) first.
		for (int32 i = Arranged.Num() - 1; i >= 0; --i)
		{
			const EVisibility ChildVis = Arranged[i].Widget->GetVisibility();
			// Skip a child only if neither it nor its descendants can be hit.
			if (!ChildVis.IsHitTestVisible() && !ChildVis.AreChildrenHitTestVisible())
			{
				continue;
			}
			// Two hit regimes, by whether the child declares a custom (non-rect) shape:
			if (IImSlate3DPlaneShaped* Shaped = AsPlaneShaped(Arranged[i].Widget))
			{
				// Shaped control (gizmo arrow): per-pixel plane-local polygon, NO layout-rect / NO bInsideRect
				// gate — so an arrow extending OUTSIDE the panel quad is still hittable. Transparent gaps →
				// continue → fall through to the sibling below.
				if (!Shaped->HitTestPlaneLocal(LocalPt))
				{
					continue;
				}
			}
			else
			{
				// Plain rectangular control: must be INSIDE the panel quad (bInsideRect) and the child's
				// layout rect. This matches the old Unproject(bInsideRect) semantics — controls stay clipped
				// to the panel.
				if (!bInsideRect)
				{
					continue;
				}
				if (!Arranged[i].Geometry.GetLayoutBoundingRect().ContainsPoint(AbsPoint))
				{
					continue;
				}
			}
			// Self hittable → this is a valid descent target; commit and recurse for a deeper leaf.
			if (ChildVis.IsHitTestVisible())
			{
				if (Descend(Arranged[i].Widget, Arranged[i].Geometry)) { return true; }
				// Even if no deeper hit child, a self-hittable widget IS a valid path end.
				return true;
			}
			// Self NOT hittable but children are (SelfHitTestInvisible wrapper): only commit if descending
			// actually reaches a hit-testable widget; otherwise backtrack and try the next sibling.
			const int32 Mark = OutPath.Num();
			if (Descend(Arranged[i].Widget, Arranged[i].Geometry)) { return true; }
			// backtrack: this branch was a dead end (e.g. the dormant gizmo). RemoveAt (not SetNum — FArrangedWidget
			// has no default ctor, which SetNum's grow path requires at compile time).
			if (OutPath.Num() > Mark) { OutPath.RemoveAt(Mark, OutPath.Num() - Mark, EAllowShrinking::No); }
		}
		// No hit-testable child reached. This node is a valid end only if it is itself hit-testable.
		return W->GetVisibility().IsHitTestVisible();
	};

	TSharedPtr<SWidget> Root = ChildSlot.GetWidget();
	if (Root.IsValid())
	{
		Descend(Root.ToSharedRef(), RootGeom);
	}

	return OutPath.Num() > 0;
}

// (UpdateHover / ClearHoverIfAny removed: hover is owned wholly by FImSlate3DHitManager::UpdateGlobalHover.
//  The box no longer tracks any HoveredPath — it only supplies BuildHoverPath on request.)

bool SImSlate3DTransformBox::GetProjectedScreenCorners(FVector2f& OutTL, FVector2f& OutTR, FVector2f& OutBR, FVector2f& OutBL) const
{
	if (!bHasPlacement || !bProjectorValid || CachedWidgetSize.X <= 0.f || CachedWidgetSize.Y <= 0.f)
	{
		return false;
	}
	const TOptional<FVector2f> TL = CachedProjector.Project(FVector2f(0.f, 0.f));
	const TOptional<FVector2f> TR = CachedProjector.Project(FVector2f(CachedWidgetSize.X, 0.f));
	const TOptional<FVector2f> BR = CachedProjector.Project(FVector2f(CachedWidgetSize.X, CachedWidgetSize.Y));
	const TOptional<FVector2f> BL = CachedProjector.Project(FVector2f(0.f, CachedWidgetSize.Y));
	if (!(TL && TR && BR && BL))
	{
		return false;  // a corner behind camera → caller skips clipping
	}
	OutTL = *TL; OutTR = *TR; OutBR = *BR; OutBL = *BL;
	return true;
}

bool SImSlate3DTransformBox::GetContentLocalBounds(FVector2f& OutMin, FVector2f& OutMax) const
{
	if (!bHasPlacement || !bProjectorValid || CachedWidgetSize.X <= 0.f || CachedWidgetSize.Y <= 0.f)
	{
		return false;
	}
	// Start from the panel content rect [0..Size], then union each shaped child's declared plane-local extent.
	// Generic: the box doesn't know about gizmos/arrows — it asks every IImSlate3DPlaneShaped child via
	// GetPlaneLocalBounds(). A child whose shape sticks out past the panel edge grows this rect so the hit-clip
	// trapezoid + broad-phase AABB cover the overhang.
	OutMin = FVector2f(0.f, 0.f);
	OutMax = CachedWidgetSize;
	if (TSharedPtr<SWidget> Root = ChildSlot.GetWidget())
	{
		TArray<TSharedRef<SWidget>> Stack;
		Stack.Add(Root.ToSharedRef());
		for (int32 Guard = 0; Guard < 4096 && Stack.Num() > 0; ++Guard)
		{
			TSharedRef<SWidget> W = Stack.Pop();
			if (IImSlate3DPlaneShaped* Shaped = AsPlaneShaped(W))
			{
				FVector2f Mn, Mx;
				if (Shaped->GetPlaneLocalBounds(Mn, Mx))
				{
					OutMin.X = FMath::Min(OutMin.X, Mn.X); OutMin.Y = FMath::Min(OutMin.Y, Mn.Y);
					OutMax.X = FMath::Max(OutMax.X, Mx.X); OutMax.Y = FMath::Max(OutMax.Y, Mx.Y);
				}
			}
			if (FChildren* Kids = W->GetChildren())
			{
				for (int32 i = 0; i < Kids->Num(); ++i) { Stack.Add(Kids->GetChildAt(i)); }
			}
		}
	}
	return true;
}

bool SImSlate3DTransformBox::GetProjectedHitClipCorners(FVector2f& OutTL, FVector2f& OutTR, FVector2f& OutBR, FVector2f& OutBL) const
{
	FVector2f Mn, Mx;
	if (!GetContentLocalBounds(Mn, Mx))
	{
		return false;
	}
	const TOptional<FVector2f> TL = CachedProjector.Project(FVector2f(Mn.X, Mn.Y));
	const TOptional<FVector2f> TR = CachedProjector.Project(FVector2f(Mx.X, Mn.Y));
	const TOptional<FVector2f> BR = CachedProjector.Project(FVector2f(Mx.X, Mx.Y));
	const TOptional<FVector2f> BL = CachedProjector.Project(FVector2f(Mn.X, Mx.Y));
	if (!(TL && TR && BR && BL))
	{
		return false;
	}
	OutTL = *TL; OutTR = *TR; OutBR = *BR; OutBL = *BL;
	return true;
}

FVector2f SImSlate3DTransformBox::MapCursorToViewport(const FGeometry& MyGeometry,
	const UE::Slate::FDeprecateVector2DParameter& ScreenSpacePos) const
{
	// Delegates to the shared ImSlate::MapDesktopToViewportPx (single source of desktop→viewport-px). Behaviour
	// unchanged: this path passes THIS box's geometry, correct only when the box is fullscreen (the retained
	// path). The fix for non-fullscreen (immediate window) routes through a fullscreen gateway's geometry instead.
	if (bProjectorValid)
	{
		return MapScreenToViewportPx(MyGeometry, CachedProjector.GetViewRect(), ScreenSpacePos);
	}
	return (FVector2f)MyGeometry.AbsoluteToLocal(ScreenSpacePos);
}

FReply SImSlate3DTransformBox::RouteSelf(FVector2f ViewportCursor, const FPointerEvent& MouseEvent, ERouteMode Mode,
	TFunctionRef<FReply(const FArrangedWidget&, const FPointerEvent&)> Fn)
{
	TArray<FArrangedWidget> PathRootToLeaf;
	if (!BuildLocalPath(ViewportCursor, PathRootToLeaf) || PathRootToLeaf.Num() == 0)
	{
		return FReply::Unhandled();
	}
	switch (Mode)
	{
	case ERouteMode::TunnelStop:
		// root → leaf, first that Handles wins (Slate OnPreviewMouseButtonDown).
		for (int32 i = 0; i < PathRootToLeaf.Num(); ++i)
		{
			FReply R = Fn(PathRootToLeaf[i], MouseEvent);
			if (R.IsEventHandled()) { return R; }
		}
		return FReply::Unhandled();

	case ERouteMode::BubbleAll:
	{
		// leaf → root, invoke ALL, never stop (Slate move = FNoReply). Report Handled if any consumed.
		FReply Acc = FReply::Unhandled();
		for (int32 i = PathRootToLeaf.Num() - 1; i >= 0; --i)
		{
			FReply R = Fn(PathRootToLeaf[i], MouseEvent);
			if (R.IsEventHandled()) { Acc = R; }
		}
		return Acc;
	}

	case ERouteMode::BubbleStop:
	default:
		// leaf → root, first that Handles wins (Slate OnMouseButtonDown/Up/Wheel).
		for (int32 i = PathRootToLeaf.Num() - 1; i >= 0; --i)
		{
			FReply R = Fn(PathRootToLeaf[i], MouseEvent);
			if (R.IsEventHandled()) { return R; }
		}
		return FReply::Unhandled();
	}
}

FReply SImSlate3DTransformBox::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bHasPlacement || ResolvedHitLayer() != EImHitLayer::Normal)
	{
		return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
	}
	// Route through the shared 3D hit dispatch so the gizmo arrows (higher overlap order) get first chance,
	// then this panel. VP computed via THIS box's geometry (correct when the box is fullscreen — the retained
	// path; non-fullscreen routes through the gateway instead). (Old self-entry; removed once the gateway owns
	// the entry — phase 5-5.)
	const FVector2f VP = MapCursorToViewport(GetCachedGeometry(), MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonDown(MouseEvent, InVP); });
}

FReply SImSlate3DTransformBox::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bHasPlacement || ResolvedHitLayer() != EImHitLayer::Normal)
	{
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}
	const FVector2f VP = MapCursorToViewport(GetCachedGeometry(), MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseButtonUp(MouseEvent, InVP); });
}

FReply SImSlate3DTransformBox::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bHasPlacement || ResolvedHitLayer() != EImHitLayer::Normal)
	{
		return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
	}
	const FVector2f VP = MapCursorToViewport(GetCachedGeometry(), MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleEvent(MouseEvent, VP,
		[&MouseEvent](IImSlate3DHittable& H, FVector2f InVP) { return H.OnHitMouseMove(MouseEvent, InVP); });
}

FReply SImSlate3DTransformBox::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bHasPlacement || ResolvedHitLayer() != EImHitLayer::Normal)
	{
		return SCompoundWidget::OnMouseWheel(MyGeometry, MouseEvent);
	}
	// Route the wheel through the shared dispatch: the front-most 3D unit's leaf widget under the cursor gets it
	// (e.g. a ScrollBox inside a 3D window scrolls). Unhandled if none → falls through to lower 2D widgets.
	const FVector2f VP = MapCursorToViewport(GetCachedGeometry(), MouseEvent.GetScreenSpacePosition());
	return FImSlate3DHitManager::Get().HandleWheel(MouseEvent, VP);
}

void SImSlate3DTransformBox::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	// Cursor left the box rect → ask the manager to clear hover (it owns the single GlobalHoveredPath). Normally
	// the move that took the cursor out already recomputed an empty path; this is the belt-and-suspenders clear
	// for a teleport-out with no trailing move over a unit.
	FImSlate3DHitManager::Get().ClearHover(MouseEvent);
	SCompoundWidget::OnMouseLeave(MouseEvent);
}

void SImSlate3DTransformBox::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Drag is owned by each arrow widget (it captures the mouse), so nothing to clear here for the gizmo.
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

// ---------------- IImSlate3DHittable: panel as a 3D unit in the shared hit dispatch ----------------

FBox2D SImSlate3DTransformBox::GetScreenBoundsVP() const
{
	// Broad-phase AABB = the projected quad's 4 screen corners' min/max (viewport physical px).
	FVector2f TL, TR, BR, BL;
	if (!GetProjectedScreenCorners(TL, TR, BR, BL)) { return FBox2D(ForceInit); }
	FBox2D Box(ForceInit);
	Box += FVector2D(TL.X, TL.Y);
	Box += FVector2D(TR.X, TR.Y);
	Box += FVector2D(BR.X, BR.Y);
	Box += FVector2D(BL.X, BL.Y);
	// Include shaped children's overhang: their declared plane-local bounds may extend past the panel quad, so
	// the broad-phase AABB must cover them too (else the hit manager rejects the overhang before the per-pixel
	// test runs). Generic — projects the content bounds' 4 corners, same rect the hit-clip uses.
	FVector2f Mn, Mx;
	if (GetContentLocalBounds(Mn, Mx))
	{
		for (const FVector2f& C : { FVector2f(Mn.X, Mn.Y), FVector2f(Mx.X, Mn.Y), FVector2f(Mx.X, Mx.Y), FVector2f(Mn.X, Mx.Y) })
		{
			const TOptional<FVector2f> S = CachedProjector.Project(C);
			if (S.IsSet()) { Box += FVector2D(S->X, S->Y); }
		}
	}
	return Box.ExpandBy(4.0);
}

FVector2f SImSlate3DTransformBox::MapDesktopCursorToViewportPx(const UE::Slate::FDeprecateVector2DParameter& DesktopPos) const
{
	return MapCursorToViewport(GetCachedGeometry(), DesktopPos);
}

bool SImSlate3DTransformBox::IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const
{
	// Remember the last cursor this 3D UI saw (for the HitProbe cross — per-widget, not the global cursor).
	LastCursorScreenPos = (FVector2f)Event.GetScreenSpacePosition();
	bHasLastCursor = true;
	// The panel's "shape" = the projected widget quad. Cursor unprojects inside [0..Size] ⇔ inside the quad.
	if (!bHasPlacement || !bProjectorValid)
	{
		return false;
	}
	// CursorVP = viewport px, computed once by the entry (gateway/box) via MapScreenToViewportPx — DON'T
	// re-derive from our own geometry (would break for a non-fullscreen immediate-window box).
	const FVector2f Cursor = CursorVP;
	// Inside the panel quad → hit. (bInsideRect)
	if (CachedProjector.Unproject(Cursor).IsSet())
	{
		return true;
	}
	// OUTSIDE the quad: a shaped child may still extend here. Map without the bInsideRect gate and ask each
	// IImSlate3DPlaneShaped child's polygon — so an overhang past the panel edge still registers a hit on this
	// unit (else the hit manager skips us and the overhang is dead). Generic — box doesn't know about gizmos.
	const FImPlaneHit Hit = CachedProjector.MapScreenToPlane(Cursor);
	if (Hit.bValid)
	{
		bool bHit = false;
		if (TSharedPtr<SWidget> Root = ChildSlot.GetWidget())
		{
			TArray<TSharedRef<SWidget>> Stack;
			Stack.Add(Root.ToSharedRef());
			for (int32 Guard = 0; Guard < 4096 && Stack.Num() > 0 && !bHit; ++Guard)
			{
				TSharedRef<SWidget> W = Stack.Pop();
				if (IImSlate3DPlaneShaped* Shaped = AsPlaneShaped(W))
				{
					if (Shaped->HitTestPlaneLocal(Hit.Local)) { bHit = true; break; }
				}
				if (FChildren* Kids = W->GetChildren())
				{
					for (int32 i = 0; i < Kids->Num(); ++i) { Stack.Add(Kids->GetChildAt(i)); }
				}
			}
		}
		if (bHit) { return true; }
	}
	return false;
}

void SImSlate3DTransformBox::HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
	const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback)
{
	// Click dispatch only — hover is owned wholly by the manager (UpdateGlobalHover). If a higher unit already
	// won, bow out; otherwise if the cursor pixel-hits us, route the click to the deepest child.
	if (OutReply.IsEventHandled())
	{
		return;
	}
	if (IsPixelHovered(Event, CursorVP))
	{
		OutReply = Callback(*this, CursorVP);
	}
}

bool SImSlate3DTransformBox::BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const
{
	// The manager asks for our hovered widget chain (root→leaf). Reuse the same DFS the click router uses.
	const int32 Before = OutPath.Num();
	if (!BuildLocalPath(CursorVP, OutPath)) { return false; }
	return OutPath.Num() > Before;
}

FReply SImSlate3DTransformBox::OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP)
{
	// Slate parity: Tunnel (OnPreviewMouseButtonDown, root→leaf) first; if unhandled, Bubble (OnMouseButtonDown).
	FReply R = RouteSelf(CursorVP, Event, ERouteMode::TunnelStop, [](const FArrangedWidget& W, const FPointerEvent& E)
	{
		return W.Widget->OnPreviewMouseButtonDown(W.Geometry, E);
	});
	if (R.IsEventHandled()) { return R; }
	return RouteSelf(CursorVP, Event, ERouteMode::BubbleStop, [](const FArrangedWidget& W, const FPointerEvent& E)
	{
		return W.Widget->OnMouseButtonDown(W.Geometry, E);
	});
}

FReply SImSlate3DTransformBox::OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP)
{
	return RouteSelf(CursorVP, Event, ERouteMode::BubbleStop, [](const FArrangedWidget& W, const FPointerEvent& E)
	{
		return W.Widget->OnMouseButtonUp(W.Geometry, E);
	});
}

FReply SImSlate3DTransformBox::OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP)
{
	// Hover (Enter/Leave) is the manager's job (UpdateGlobalHover). Slate move = bubble to the WHOLE path
	// (FNoReply, all widgets), so a parent that watches move (drag-detect, slider container) also gets it.
	return RouteSelf(CursorVP, Event, ERouteMode::BubbleAll, [](const FArrangedWidget& W, const FPointerEvent& E)
	{
		return W.Widget->OnMouseMove(W.Geometry, E);
	});
}

FReply SImSlate3DTransformBox::OnHitMouseWheel(const FPointerEvent& Event, FVector2f CursorVP)
{
	// Slate wheel = bubble leaf→root, first that handles wins (nested ScrollBox scrolls via this).
	return RouteSelf(CursorVP, Event, ERouteMode::BubbleStop, [](const FArrangedWidget& W, const FPointerEvent& E)
	{
		return W.Widget->OnMouseWheel(W.Geometry, E);
	});
}

FCursorReply SImSlate3DTransformBox::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	// Gizmo drag is owned by the arrow widgets now (each captures the mouse). The box no longer manages the
	// high-precision cursor hide; arrows can implement OnCursorQuery themselves if needed (first version: no
	// high-precision scrub, absolute drag — cursor stays visible).
	return FCursorReply::Unhandled();
}

// ---------------- SImSlate3DHitClip: trapezoid hit-clip wrapper ----------------

void SImSlate3DHitClip::Construct(const FArguments& InArgs)
{
	Box = InArgs._Box;
	// SelfHitTestInvisible: the wrapper itself never grabs events; only its child (the box) does. The
	// wrapper exists purely to PUSH the trapezoid clip before the box paints+registers.
	SetVisibility(EVisibility::SelfHitTestInvisible);
	if (Box.IsValid())
	{
		ChildSlot[ Box.ToSharedRef() ];
	}
}

int32 SImSlate3DHitClip::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Push the box's projected trapezoid as a clip BEFORE painting the box, so the box records it as its
	// InitialClipState → the hit grid skips the box outside the trapezoid → clicks there fall through.
	FVector2f TL, TR, BR, BL;
	bool bPushed = false;
	// Clip to the arrow-INCLUSIVE bound (panel rect grown to enclose the gizmo arrow tips), NOT the bare panel
	// — else the engine hit-grid drops the box outside the panel trapezoid and an arrow sticking out past the
	// edge becomes unclickable (the overhang's events fall through before the box's per-pixel test runs).
	if (Box.IsValid() && Box->GetProjectedHitClipCorners(TL, TR, BR, BL))
	{
		// Corners are in VIEWPORT physical px (S3). Convert to this widget's LOCAL space (logical, S2). This is
		// the INVERSE of MapCursorToViewport (= AbsoluteToLocal * Scale + Min), so it must be (P - Min) / Scale
		// — NOT physical * LocalSize/ViewRect.Size (that assumed widget-fills-viewport, the twin of the forward
		// bug). Then LocalToAbsolute maps local→window for the clip zone.
		const FImProjector& Proj = Box->GetCachedProjector();
		const FIntRect VR = Proj.GetViewRect();
		const float Scale = AllottedGeometry.Scale;
		if (VR.Width() > 0 && VR.Height() > 0 && Scale > 0.f)
		{
			const FVector2f Min((float)VR.Min.X, (float)VR.Min.Y);
			// physical viewport px → this widget's LOCAL → ABSOLUTE/window space. PushClip stores the zone
			// verbatim (no geometry transform), and the hit grid tests against window-space coords, so the
			// zone corners must be in absolute/window space.
			auto ToWindow = [&](FVector2f P)
			{
				const FVector2f Local((P.X - Min.X) / Scale, (P.Y - Min.Y) / Scale);
				return (FVector2f)AllottedGeometry.LocalToAbsolute(Local);
			};
			// FSlateClippingZone(TopLeft, TopRight, BottomLeft, BottomRight) — note BL/BR order.
			FSlateClippingZone Zone(ToWindow(TL), ToWindow(TR), ToWindow(BL), ToWindow(BR));
			OutDrawElements.PushClip(Zone);
			bPushed = true;
		}
	}

	const int32 Result = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (bPushed)
	{
		OutDrawElements.PopClip();
	}
	return Result;
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
