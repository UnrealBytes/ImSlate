// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GenericSingletons.h"
#include "AssetRegistry/AssetData.h"

#include "ImAssetRegistryCache.generated.h"

UCLASS()
class IMSLATE_API UImAssetRegistryCache : public UObject
{
	GENERATED_BODY()

public:
	static UImAssetRegistryCache* Get(const UObject* WorldContextObject = nullptr)
	{
		return Cast<UImAssetRegistryCache>(UGenericSingletons::GetSingletonImpl(StaticClass(), WorldContextObject));
	}

	const TArray<FAssetData>& GetAssets(UClass* FilterClass);

	void SearchAssets(const FString& SearchText, UClass* FilterClass, TArray<FAssetData>& OutResults, int32 MaxResults = 32);

	void Invalidate();

private:
	TMap<FTopLevelAssetPath, TArray<FAssetData>> CachedByClass;
	TArray<FAssetData> CachedAll;
	bool bAllCached = false;
};
