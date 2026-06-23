// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UObject/NoExportTypes.h"
#include "GMPHelper.h"
#include "ImSlateWidgetPool.h"
#include "Components/CanvasPanel.h"
#include "InGameUI.h"
#include "InGameUIManager.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogInGameUIManager, Log, All);
DECLARE_DYNAMIC_DELEGATE(FOnShownComplete);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnShownFailed, const FString&, ErrorMsg);

USTRUCT(BlueprintType)
struct FInGameUIRegisterInfo : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FName RegName;

	UPROPERTY(EditAnywhere, meta = (MustImplement = "/Script/InGameUI.InGameUIInc"))
	TSoftClassPtr<UWidget> WidgetClass;
};

/*
* custom CanvasPanel，control lifetime of widget and visibility
*/
UCLASS()
class INGAMEUI_API UInGameMainPanelBase : public UCanvasPanel
{
	GENERATED_BODY()
public:
	virtual void AddToGame(UWorld* InWorld, int32 ZOrder = 0);

	template<typename WidgetT>
	FORCEINLINE_DEBUGGABLE WidgetT* ConstructWidget(TSubclassOf<UWidget> WidgetClass = WidgetT::StaticClass(), FName WidgetName = NAME_None)
	{
		static_assert(TIsDerivedFrom<WidgetT, UWidget>::IsDerived, "Custom ConstructWidget can only create UWidget objects.");

		if (WidgetClass->IsChildOf<UUserWidget>())
		{
			UUserWidget* NewWidget = NewObject<UUserWidget>(this, *WidgetClass, WidgetName, RF_Transactional);
			NewWidget->Initialize();
			return Cast<WidgetT>(NewWidget);
		}

		return NewObject<WidgetT>(this, WidgetClass, WidgetName, RF_Transactional);
	}

protected:
	virtual void OnWidgetBecomeTop(UWidget* NewTopWidget, UWidget* OldTop);
	virtual void OnLeaveWorld(UWorld* InWorld);
	virtual void OnSlotAdded(UPanelSlot* Slot) override;
	virtual void OnSlotRemoved(UPanelSlot* Slot) override;
};

/**
 * InGame UI Manager
 */
UCLASS(config = Game, defaultConfig)
class UInGameUIManager : public UObject
{
	GENERATED_BODY()
	friend UInGameMainPanelBase;

public:
	static UInGameUIManager* Get(UObject* Ctx, bool bCreate = true);
	bool InitUIManager(const TArray<FInGameUIRegisterInfo*>& RowItems, TSubclassOf<UInGameMainPanelBase> OverrideCls);
	bool RegisterWidget(const FInGameUIRegisterInfo& RegInfo);
	bool UnregisterWidget(const FInGameUIRegisterInfo& RegInfo);

	bool ShowWidgetByName(FName InName, const FGMPStructUnion& InPayLoad, bool bBringToTop, const FOnShownComplete& OnComplete, const FOnShownFailed& OnFailed);
	bool BringWidgetToTop(FName InName);

	void SetWidgetVisibility(FName InName, ESlateVisibility InVisibility);

	FORCEINLINE bool IsUIRegisted(FName InName) const { return !!GlobalUIMap.Find(InName); }
	FORCEINLINE bool IsUICreated(FName InName) const { return CreatedWidgetMap.Find(InName) && IsValid(CreatedWidgetMap[InName]); };
	FORCEINLINE bool IsUITop(FName InName) const { return GetTopWidgetName() == InName; }

	bool DestroyAllWidget();
	void HiddenAll();
	bool DestroyWidget(FName InName);
	FName GetTopWidgetName() const { return ActivateWidgetStack.Num() ? ActivateWidgetStack.Last() : NAME_None; }

protected:
	UPROPERTY(Config, EditAnywhere)
	TSoftClassPtr<UInGameMainPanelBase> MainPanelClass;
	UPROPERTY(Transient)
	UClass* MainPanelClassHolder = nullptr;

	UClass* GetManPanelClass(TSubclassOf<UInGameMainPanelBase> InClass);

private:
	void UpdateAllWidgetZOrder();

	TArray<FName> ActivateWidgetStack;

	UPROPERTY(Transient)
	TMap<FName, UWidget*> CreatedWidgetMap;

	// UPROPERTY(Transient)
	// FUWidgetPool WidgetPool;

	UPROPERTY(Transient)
	TMap<FName, TSoftClassPtr<UWidget>> GlobalUIMap;

	UPROPERTY(Transient)
	UInGameMainPanelBase* MainPanel = nullptr;

	// ZOrder of MainPanel in Parent
	const int32 DefaultZOrder = 10;
};
