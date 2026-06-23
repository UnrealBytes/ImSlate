// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class InGameUI : ModuleRules
{
	public InGameUI(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Public",
			// ... add public include paths required here ...
		});

		PrivateIncludePaths.AddRange(new string[] {
			ModuleDirectory + "/Private",
			// ... add other private include paths required here ...
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			//
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
			"ImSlate",
            "ApplicationCore",
            "AppFramework",
            "RenderCore"
			// ... add private dependencies that you statically link with here ...
		});
		DynamicallyLoadedModuleNames.AddRange(new string[] {
			// ... add any modules that your module loads dynamically here ...
		});
	}
}
