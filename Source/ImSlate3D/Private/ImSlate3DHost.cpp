// Copyright Epic Games, Inc. All Rights Reserved.
#include "ImSlate3DHost.h"

#if defined(IMSLATE3D_API)

namespace ImSlate
{

FImSlate3DHost& FImSlate3DHost::Get()
{
	static FImSlate3DHost GHost;
	return GHost;
}

void FImSlate3DHost::Install(FImSlate3DHost&& InHooks)
{
	Get() = MoveTemp(InHooks);
}

}  // namespace ImSlate

#endif  // defined(IMSLATE3D_API)
