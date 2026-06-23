// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/UserWidgetPool.h"
#include "GMPCore.h"
#include "ImSlateWidgetPool.h"
#include "Modules/ModuleManager.h"
#include "Tickable.h"

#include "InGameToast.generated.h"

UINTERFACE(Blueprintable)
class UInGameToastWidgetInc : public UInterface
{
	GENERATED_BODY()
};

class INGAMEUI_API IInGameToastWidgetInc
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent)
	void OnSetData(const FGMPStructUnion& Union);

	UFUNCTION(BlueprintNativeEvent)
	void OnShowToast(const FGMPStructUnion& Union, UWidget* CurWidget);

	UFUNCTION(BlueprintNativeEvent)
	void OnHideToast(const FGMPStructUnion& Union, UWidget* CurWidget);

	UFUNCTION(BlueprintNativeEvent)
	void GetFadingDuration(float& OutDuration);

	static void ShowToastWidget(UWidget* CurWidget);
	static void HideToastWidget(UWidget* CurWidget);

private:
	virtual void OnSetData_Implementation(const FGMPStructUnion& Union) {}
	virtual void GetFadingDuration_Implementation(float& OutDuration) {}
	virtual void OnShowToast_Implementation(const FGMPStructUnion& Union, UWidget* CurWidget);
	virtual void OnHideToast_Implementation(const FGMPStructUnion& Union, UWidget* CurWidget);
};

USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct FInGameToastItem
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "InGameToast")
	FGMPStructUnion StructUnion;
	UPROPERTY(BlueprintReadOnly, Category = "InGameToast")
	float LeftDuration = 3.f;
	UPROPERTY(BlueprintReadWrite, Category = "InGameToast")
	float FadingDuration = 1.f;
	UPROPERTY()
	TObjectPtr<UWidget> Widget = nullptr;
};

UCLASS(Abstract, Blueprintable)
class UInGameToastManager : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "InGameToast", meta = (CustomClassPinPicker, WorldContext = "InWorldContext"))
	static UInGameToastManager* GetSingleton(UObject* InWorldContext, UPARAM(meta = (AllowAbstract = false)) TSubclassOf<UInGameToastManager> InClass = nullptr);

	UFUNCTION(BlueprintCallable, Category = "InGameToast", meta = (AutoCreateRefTerm = "Union", AdvancedDisplay = "Union"))
	void ShowToast(const FGMPStructUnion& Union);

	virtual void PostInitProperties() override;

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGameToast", Meta = (MetaClass = "/Script/UMG.Widget", MustImplement = "/Script/InGameToast.XToastWidgetInc"))
	FSoftObjectPath ToastWidgetPath;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGameToast")
	float DefaultDuration = 3.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InGameToast")
	float QueueCount = 1;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "InGameToast")
	void OnShowWidget(UWidget* OutWidget, const FGMPStructUnion& Union);
	UFUNCTION(BlueprintNativeEvent, Category = "InGameToast")
	void OnHideWidget(UWidget* InWidget, const FGMPStructUnion& Union);

	UFUNCTION(BlueprintNativeEvent, Category = "InGameToast")
	void OnAllocWidget(UWidget*& OutWidget);
	UFUNCTION(BlueprintNativeEvent, Category = "InGameToast")
	void OnFreeWidget(UWidget* InWidget);

	UFUNCTION(BlueprintCallable, Category = "InGameToast")
	UWidget* GerWidgetFromPool(TSubclassOf<UWidget> InWidgetClass);
	UFUNCTION(BlueprintCallable, Category = "InGameToast")
	void ReleaseWidgetToPool(UWidget* Widget);

	UFUNCTION(BlueprintNativeEvent, Category = "InGameToast")
	void OnWidgetClassLoaded(TSubclassOf<UWidget> InWidgetClass);
	UPROPERTY(Transient, BlueprintReadWrite)
	TSubclassOf<UWidget> WidgetClass = nullptr;

protected:
	void Init();
	void Tick(float DeltaSeconds);
	bool IsValid() const;
	bool EnqueueToast();
	bool DequeueToast(float DeltaSeconds);

private:
	UPROPERTY(Transient)
	FUWidgetPool WidgetPool;
	UPROPERTY(Transient)
	int32 ToastCursor = -1;

	struct FTickableProxy : public FTickableGameObject
	{
		FTickableProxy(UInGameToastManager* In)
			: Impl(In)
		{
		}

	protected:
		UInGameToastManager* Impl = nullptr;
		virtual UWorld* GetTickableGameObjectWorld() const override { return Impl->GetWorld(); }
		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UInGameToastManager, STATGROUP_Tickables); }
		virtual bool IsTickable() const override { return Impl->IsValid(); }
		void Tick(float DeltaSeconds) override { Impl->Tick(DeltaSeconds); }
	};
	TUniquePtr<FTickableProxy> TickHandle;

	TIndirectArray<FInGameToastItem> ToastQueue;
};
