// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ImSlate : ModuleRules
{
	public ImSlate(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// --- ImSlate 3D ---
		// No feature flag. The 3D code is gated by `#if defined(IMSLATE3D_API)`, which is true precisely
		// because this module depends on the always-built ImSlate3D module (see PublicDependencyModuleNames).

		PublicIncludePaths.AddRange(new string[] {
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			// ... add other private include paths required here ...
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			// --- ImSlate 3D (self-contained "3D-ify any Slate" module) ---
			// The custom global shader + ICustomSlateElement + FImProjector + capture→reproject pipeline +
			// pure interaction layer (hit manager / routing / cursor math) live in the separate ImSlate3D
			// module (it must load at PostConfigInit so its shader types register before global-shader
			// compile; the main module can't, as it links PreDefault plugins like GMP). The main module
			// keeps only the Engine-coupled bits (camera query → matrices, SViewport hit-path) and feeds
			// them in. PUBLIC because the main module's PUBLIC headers (SImSlate3DTransformBox.h / Arrow / Gizmo)
			// #include ImSlate3D.h for FImProjector / FImWorldPlacement and gate on `#if defined(IMSLATE3D_API)`.
			"ImSlate3D",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			// ... add other public dependencies that you statically link with here ...
			"GMP",
			"GenericStorages",
			"Slate",
			"SlateCore",
			"InputCore",
			"UMG",
			"Json",
			"ApplicationCore",
			"AppFramework",
			"RenderCore",
			"RHI",
			"EnhancedInput",
			"AssetRegistry",
            //"AnimatedTexture",
			// ... add private dependencies that you statically link with here ...
		});
		if (Target.Type == TargetRules.TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
				"LevelEditor",
				"PropertyEditor",
			});
		}
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});
	}
}
