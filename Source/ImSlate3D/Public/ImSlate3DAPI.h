// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// (Obsolete gate header — kept as an empty placeholder so existing includes don't break.)
//
// The old IMSLATE_3D_TRANSFORM feature flag is gone. The ImSlate3D module is always built; code that
// needs to know whether it's available uses `#if defined(IMSLATE3D_API)` — UBT defines IMSLATE3D_API
// in this module and any module that depends on it, and leaves it undefined elsewhere. No manual flag.
