// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// Self-contained "3D-ify any Slate" module: the custom global shader (VS/PS + ICustomSlateElement),
// the pure-math projector (FImProjector / FImWorldPlacement), the capture→reproject pipeline, the
// interaction layer (hit manager + custom hit-path + routing), AND the 3D widgets themselves
// (SImSlate3DTransformBox / SImSlate3DArrow / SImSlate3DPlaneArrow). Split from the main ImSlate module so it
// loads at LoadingPhase=PostConfigInit (global shader types register at DLL load, before
// CompileGlobalShaderMap). It depends ONLY on Core/SlateCore/Slate/RHI (NO GMP/UMG/Engine) so early load
// is safe. The few Engine-coupled operations (scene-camera query, game-viewport hit-path, input settings)
// are injected by the main module via FImSlate3DHost (dependency inversion) — this module never links Engine.
public class ImSlate3D : ModuleRules
{
	public ImSlate3D(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// No feature flag: this module is always built. Availability is detected downstream via the
		// UBT-generated IMSLATE3D_API macro (defined here + in dependents, undefined elsewhere).

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"SlateCore",   // ICustomSlateElement, FSlateDrawElement, SCompoundWidget/SLeafWidget, FontCache
			               // (the public widget headers derive from SlateCore widgets; no Slate types in headers)
			"RHI",         // FTextureRHIRef / FRDGBuilder appear in the public FIm3DQuad / Draw_RenderThread
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"CoreUObject",
			"RenderCore",
			"Projects",    // IPluginManager (resolve the ImSlate plugin Shaders dir)
			"Slate",       // FSlateApplication (high-precision mouse), SViewport, SOverlay — used only in the
			               // widget .cpp (not headers). Slate is an engine-core module available at
			               // PostConfigInit; we only CALL it at runtime (OnPaint/events), never in
			               // StartupModule, so loading this module early is safe.
		});
	}
}
