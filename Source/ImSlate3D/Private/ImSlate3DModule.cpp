// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DAPI.h"

#include "Modules/ModuleManager.h"

#if defined(IMSLATE3D_API)
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#endif

// Minimal module whose ONLY job is to register the ImSlate 3D shader virtual directory + host the
// global shader types early enough. LoadingPhase=PostConfigInit (set in the .uplugin) so this loads
// before CompileGlobalShaderMap. It depends only on low-level render modules (no GMP), so loading it
// this early is safe — unlike the main ImSlate module which links PreDefault plugins.
class FImSlate3DModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if defined(IMSLATE3D_API)
		// Map "/Plugin/ImSlate" -> the ImSlate plugin's Shaders dir (the .usf stays under the main
		// plugin). Done here in StartupModule because PostConfigInit runs before global-shader compile.
		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ImSlate")))
		{
			const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/ImSlate"), ShaderDir);
		}
#endif
	}

	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FImSlate3DModule, ImSlate3D)
