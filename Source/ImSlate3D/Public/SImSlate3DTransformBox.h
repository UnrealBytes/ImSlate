// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ImSlate3D.h"

#if defined(IMSLATE3D_API)

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ImSlate3DHitManager.h"   // IImSlate3DHittable

namespace ImSlate
{
// Defined in the separate ImSlate3D module (Public/ImSlate3DShaderElement.h). Forward-declared
// here so we can hold a strong ref without pulling the render headers into this public header.
class FIm3DShaderElement;

/** Occlusion LAYER where this 3D box's hit-responder sits in the Slate event stack — i.e. WHO can occlude it
 *  and who it occludes. One box picks ONE layer; the hit-test math (BuildLocalPath) is shared across all three,
 *  only the stack position differs.
 *   Top    — a full-screen Gateway widget at the top of the viewport overlay: above ALL game UI, never occluded
 *            (and occludes everything below). Native capture → reliable drag. For gizmos / interactive UI.
 *   Normal — the box itself as a widget on its own layer: competes by z with its siblings (occluded by what's
 *            above, occludes what's below). Self-handled events. Lightweight embedding.
 *   Bottom — an ICustomHitTestPath on the game SViewport (bubble END): occluded by ANY visible widget above,
 *            only fires when the cursor bare-touches the SViewport. Engine-driven hover/tooltip + perfect
 *            pass-through. For display-only 3D UI with no overlay above and no drag. */
enum class EImHitLayer : uint8 { Top = 0, Normal = 1, Bottom = 2 };
/** Runtime debug override for ALL boxes: imslate.3d.ForceLayer (-1 = use each box's own HitLayer; 0/1/2 = force
 *  Top/Normal/Bottom). Returns INDEX_NONE (-1) when not forcing. */
IMSLATE3D_API int32 GetIm3DForcedLayer();

/**
 * SImSlate3DTransformBox (M2, minimal) — draws its child content as a flat sheet placed in
 * the 3D world, projected to the screen through the real scene camera and composited on the
 * top Slate layer (no RT, no depth). Non-invasive: wrap any widget; with no placement set it
 * paints normally (passthrough).
 *
 * M2 scope: projects the box BACKGROUND quad (and child passthrough) so the perspective is
 * visible end to end. Per-element child capture (full B2), text, hit-test and mesh subdivision
 * come in M3-M5.
 */
class IMSLATE3D_API SImSlate3DTransformBox : public SCompoundWidget, public IImSlate3DHittable
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DTransformBox) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SImSlate3DTransformBox();

	/** Place the sheet in the world. Until called, the box paints as a normal 2D passthrough. */
	void SetWorldPlacement(const FImWorldPlacement& InPlacement);
	void ClearWorldPlacement() { bHasPlacement = false; }

	/** Stacking order among overlapping 3D windows (higher = on top = gets the click first; gaps fall through
	 *  to lower windows). Default 0. The hit manager re-sorts per dispatch, so raising this at runtime (e.g.
	 *  on focus) takes effect immediately. Each 3D window's GetOverlapOrder = GizmoOverlapOrderBase + this. */
	void SetOverlapOrder(int32 InOrder) { WindowOverlapOrder = InOrder; }
	int32 GetWindowOverlapOrder() const { return WindowOverlapOrder; }

	/** Occlusion layer (Top/Normal/Bottom — see EImHitLayer). Re-applies visibility + hit-path registration.
	 *  Top requires the caller to have installed the shared Gateway (SImSlate3DGateway::Install). */
	void SetHitLayer(EImHitLayer InLayer);
	EImHitLayer GetHitLayer() const { return HitLayer; }

	/** Lightweight per-frame placement move used during gizmo drag: updates only WorldLocation, WITHOUT
	 *  touching visibility / custom-hit-path registration (which SetWorldPlacement does and which must not
	 *  run every drag frame). */
	void UpdatePlacementLocation(const FVector& NewWorldLocation) { Placement.WorldLocation = NewWorldLocation; }

	/** The four projected screen corners of the sheet (TL,TR,BR,BL) in VIEWPORT px, from the last paint's
	 *  projector. Used by SImSlate3DHitClip to push a trapezoid hit-clip so clicks outside fall through.
	 *  Returns false if no valid projection (then the wrapper clips nothing / passes through). */
	bool GetProjectedScreenCorners(FVector2f& OutTL, FVector2f& OutTR, FVector2f& OutBR, FVector2f& OutBL) const;

	/** The box's HIT plane in widget-local px = the content rect [0..Size] unioned with every shaped child's
	 *  declared GetPlaneLocalBounds() (which may stick out past the panel edge, e.g. a gizmo arrow head). The
	 *  box does NOT know about gizmos — it just asks each IImSlate3DPlaneShaped child for its extent.
	 *  SImSlate3DHitClip projects THIS rect (not the bare panel) into the trapezoid hit-clip, so an overhang
	 *  stays inside the box's hittable zone (else the engine hit-grid drops it before the per-pixel test).
	 *  Returns false if there's no valid projection / size. */
	bool GetContentLocalBounds(FVector2f& OutMin, FVector2f& OutMax) const;

	/** The four projected screen corners of GetContentLocalBounds() (TL,TR,BR,BL) in VIEWPORT px — the
	 *  shaped-child-inclusive twin of GetProjectedScreenCorners. */
	bool GetProjectedHitClipCorners(FVector2f& OutTL, FVector2f& OutTR, FVector2f& OutBR, FVector2f& OutBL) const;

	/** The projector cached from the last paint (same one used for rendering + hit-test). */
	const FImProjector& GetCachedProjector() const { return CachedProjector; }

	/** The content size (widget-local px) cached from the last paint = the projected plane's [0..Size] extent.
	 *  Shaped children (e.g. gizmo arrows) read this to anchor themselves relative to the panel. */
	FVector2f GetCachedWidgetSize() const { return CachedWidgetSize; }

	/** True if the last paint produced a valid projection (camera available, not back-faced). Independent
	 *  drag-arrow widgets check this before borrowing GetCachedProjector(). */
	bool HasValidProjection() const { return bProjectorValid; }

	/** Current world placement accessors used by the independent gizmo arrows (SImSlate3DArrow): they read
	 *  the anchor/scale to build their geometry and write WorldLocation back while dragging. */
	FVector GetWorldLocation() const { return Placement.WorldLocation; }
	float   GetWorldScale() const { return Placement.WorldScale; }

	/** Panel bottom-left anchor (widget-local px, nudged inward), where overlay arrows hang off. Generic — the
	 *  independent viewport arrow (SImSlate3DArrow) reads it to pivot its world segment. */
	FVector2f GetGizmoAnchorLocal() const;


	/** Convert a DESKTOP-space cursor to viewport physical px using THIS PANEL's geometry — the exact same
	 *  chain the panel's own hit-test/render uses. Independent gizmo arrows MUST use this (not their own
	 *  widget geometry) so their cursor→viewport mapping matches the projector exactly (else a per-distance
	 *  scale drift creeps in — the arrow widget's geometry need not share the panel's logical↔physical ratio). */
	FVector2f MapDesktopCursorToViewportPx(const UE::Slate::FDeprecateVector2DParameter& DesktopPos) const;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// --- M4: true-perspective hit-test via ICustomHitTestPath (FIm3DHitTestPath on the game viewport) ---
	// The viewport-level custom hit path calls these. We unproject the viewport-pixel cursor (real
	// perspective world-ray, the SAME FImProjector used to render); inside the projected quad we build
	// the deep child widget path, each entry's virtual pointer = the unprojected child-local pixel, so
	// the engine drives hover / click / tooltip automatically. No SVirtualWindow, no RT.

	// Build the bubble path (child subtree → leaf) for ViewportCursor. Returns false if the cursor is
	// outside the projected quad / behind the camera. Each FWidgetAndPointer carries the virtual cursor.
	bool BuildHitPath(FVector2f ViewportCursor, bool bIgnoreEnabledStatus, TArray<FWidgetAndPointer>& OutPath) const;

	// --- Primary path: the box handles events itself (reliable under an overlay, where the engine's
	// ICustomHitTestPath on the SViewport never fires). It unprojects the cursor, and if it lands inside
	// the projected quad routes the event to the deepest child; otherwise returns Unhandled to pass through.
	// Hover is tracked manually here (Enter/Leave) since no engine widget-path drives it on this route.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// Hide the cursor while dragging a gizmo axis under high precision (the cursor is pinned, so showing it
	// frozen would look wrong). Conditional on high precision actually being in effect — if it isn't (e.g.
	// PIE), keep the cursor visible so it isn't lost. (reference-ue-high-precision-mouse)
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	// Capture can be revoked by the system mid-drag (Alt+Tab / focus loss) — the matching ButtonUp never
	// arrives. Clear the drag state here so the cursor isn't left hidden and DragAxis stuck. (See memory
	// issue-slate-popup-capture-lost-cleanup; high precision is auto-released by the engine with the capture.)
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	// --- IImSlate3DHittable: the panel is a 3D unit in the shared hit dispatch (lowest overlap order, below
	// the gizmo arrows). Its per-pixel shape = the cursor unprojects inside the widget quad; its action =
	// route the event to the deepest child widget (the existing Self routing). ---
	virtual int32 GetOverlapOrder() const override { return GizmoOverlapOrderBase + WindowOverlapOrder; }
	virtual FBox2D GetScreenBoundsVP() const override;
	virtual bool IsPixelHovered(const FPointerEvent& Event, FVector2f CursorVP) const override;
	virtual void HandleHitEvent(FReply& OutReply, const FPointerEvent& Event, FVector2f CursorVP,
		const TFunctionRef<FReply(IImSlate3DHittable&, FVector2f)>& Callback) override;
	virtual FReply OnHitMouseButtonDown(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseMove(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseButtonUp(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual FReply OnHitMouseWheel(const FPointerEvent& Event, FVector2f CursorVP) override;
	virtual bool IsActivelyDragging() const override { return false; }  // the panel itself never drags (arrows do)
	// Hover is owned by the manager: it asks for our hovered widget chain via BuildHoverPath (= BuildLocalPath).
	virtual bool BuildHoverPath(FVector2f CursorVP, TArray<FArrangedWidget>& OutPath) const override;

	// The panel's overlap order base (negative so it sits below any positive-order 3D units).
	static constexpr int32 GizmoOverlapOrderBase = -100;

	// Expose our child to the custom-hit-test arrange pass (identity child geometry).
	void ArrangeForCustomHitTest(FArrangedChildren& ArrangedChildren) const;

	// If ChildWidget belongs to our subtree and the cursor hits our quad, return the unprojected local px.
	TOptional<FVector2f> TranslateCursor(const SWidget& ChildWidget, FVector2f ViewportCursor) const;

private:
	FImWorldPlacement Placement;
	bool bHasPlacement = false;
	int32 WindowOverlapOrder = 0;  // stacking among overlapping 3D windows (SetOverlapOrder); see GetOverlapOrder
	EImHitLayer HitLayer = EImHitLayer::Normal;  // occlusion layer (Top/Normal/Bottom)

	// Apply visibility + custom-hit-path registration for the current hit layer (Top/Normal/Bottom, honoring the
	// imslate.3d.ForceLayer debug override). Called on first placement and layer change only (NOT per drag frame).
	void ApplyHitLayer();

	// Effective hit layer = ForceLayer debug override if set, else this box's HitLayer. Used by OnMouse* to
	// decide whether the box self-routes (only the Normal layer does; Top→Gateway, Bottom→engine custom-path).
	EImHitLayer ResolvedHitLayer() const;

	// (No HoveredPath here — hover lives wholly in FImSlate3DHitManager::GlobalHoveredPath, the single source
	//  of truth. The box only supplies BuildHoverPath on demand.)

	// Unproject ViewportCursor and walk the child subtree, filling OutPath root→leaf with the widgets
	// under that point (each with its identity-space geometry). Returns false if the cursor is outside
	// the projected quad / behind the camera. Used by both BuildHoverPath (hover) and RouteSelf (clicks).
	bool BuildLocalPath(FVector2f ViewportCursor, TArray<FArrangedWidget>& OutPath) const;

	// Map a desktop-space cursor to viewport PHYSICAL pixels (the space FImProjector projects into).
	// AbsoluteToLocal yields widget-LOCAL (logical) px; we rescale by ConstrainedViewRect / GetLocalSize so
	// the result matches the projector's physical space (fixes the DPI/scale drift that grew toward the
	// bottom-right). Falls back to plain AbsoluteToLocal if the projector isn't ready.
	FVector2f MapCursorToViewport(const FGeometry& MyGeometry, const UE::Slate::FDeprecateVector2DParameter& ScreenSpacePos) const;

	// How to route a pointer event over the hit path (mirrors Slate's FEventRouter policies exactly):
	//  TunnelStop  — root→leaf, stop at first that Handles (OnPreviewMouseButtonDown phase).
	//  BubbleStop  — leaf→root, stop at first that Handles (OnMouseButtonDown/Up, OnMouseWheel).
	//  BubbleAll   — leaf→root, invoke ALL, never stop (OnMouseMove — FNoReply in Slate).
	enum class ERouteMode : uint8 { TunnelStop, BubbleStop, BubbleAll };

	// Route a self-handled pointer event over BuildLocalPath's hit chain using the given Slate-equivalent mode.
	// ViewportCursor must already be in viewport pixels (caller does MapCursorToViewport).
	FReply RouteSelf(FVector2f ViewportCursor, const FPointerEvent& MouseEvent, ERouteMode Mode,
		TFunctionRef<FReply(const FArrangedWidget&, const FPointerEvent&)> Fn);

	// Cached each paint so hit-test uses the exact same projection as the last rendered frame.
	mutable FImProjector CachedProjector;
	mutable bool bProjectorValid = false;
	mutable FVector2f CachedWidgetSize = FVector2f(0.f, 0.f);

	// Last DESKTOP cursor pos this widget saw via a mouse event (updated in IsPixelHovered, called for every
	// dispatched event). Used by the imslate.3d.HitProbe cross so the probe reflects what THIS 3D UI actually
	// received (per-widget), not the global cursor.
	mutable FVector2f LastCursorScreenPos = FVector2f(0.f, 0.f);
	mutable bool bHasLastCursor = false;
	mutable FVector2f ProbeLastLoggedCursor = FVector2f(-1.f, -1.f);  // HitProbe log throttle (one line per move)

	// renderMode=shader: the custom Slate element is held by the draw-element list only via a WEAK ptr
	// (FSlateCustomDrawerElement::CustomDrawer is TWeakPtr). If we let our local TSharedPtr die at the end
	// of OnPaint, the batcher's Pin() returns null next frame and Draw_RenderThread never runs. So keep a
	// STRONG ref alive across OnPaint→render here; replaced each paint (prior frame's draw is done).
	mutable TSharedPtr<FIm3DShaderElement, ESPMode::ThreadSafe> KeepAliveShaderElement;

	bool bRegisteredHitPath = false;
	void RegisterHitPath();
	void UnregisterHitPath();

	// Resolve the current scene camera (player 0) into a view-projection + view rect.
	// Returns false when no projection data is available (e.g. no player / commandlet).
	bool BuildProjector(FVector2f WidgetSize, FImProjector& OutProj) const;

	// Re-emit captured elements (box/rounded-box/border + shaped-text glyphs) as projected quads via a
	// custom GPU perspective shader: each corner is projected to clip space WITH the real camera W
	// (ProjectToClip), then one FIm3DShaderElement is submitted via MakeCustom. True per-pixel GPU
	// perspective, correct rounded corners, and PF_A8 font rendering. Returns the next layer.
	// (The old CPU MakeCustomVerts path was removed — it can't render font atlases correctly.)
	int32 ProjectCapturedElementsShader(const class FSlateWindowElementList& CaptureList,
		const FImProjector& Proj, class FSlateWindowElementList& OutDrawElements, int32 BaseLayer) const;

	// The identity-space root geometry the child is laid out in (child-local px == layout px).
	FGeometry GetChildLayoutGeometry() const;

	// Is ChildWidget anywhere in our child subtree?
	bool OwnsWidget(const SWidget& ChildWidget) const;
};

/**
 * SImSlate3DHitClip — thin wrapper that hosts ONE SImSlate3DTransformBox and, each paint, pushes a
 * TRAPEZOID clipping zone (the box's 4 projected screen corners) onto OutDrawElements BEFORE painting
 * the box. Because a widget records its InitialClipState (used for hit-testing, HittestGrid.cpp:983) at
 * the start of its own Paint — i.e. from the clip stack its PARENT established — the box ends up with a
 * trapezoid clip. The hit grid then skips the box for cursor points outside the trapezoid, so clicks
 * there fall through to the game UI behind. The box itself can no longer do this (its own clip is fixed
 * before its OnPaint runs), which is exactly why this parent wrapper exists. Pure event-driven, no polling.
 */
class IMSLATE3D_API SImSlate3DHitClip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImSlate3DHitClip) {}
		SLATE_ARGUMENT(TSharedPtr<SImSlate3DTransformBox>, Box)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedPtr<SImSlate3DTransformBox> Box;
};

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
