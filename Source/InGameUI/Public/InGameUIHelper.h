// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/DataTable.h"
#include "InGameUIManager.h"
#include "InGameUIHelper.generated.h"

UCLASS(Transient, meta = (NeuronAction))
class INGAMEUI_API UInGameUIHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/*
	* @func: Initialise UI Manager
	* @param Table: FInGameUIRegisterInfo
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx", CustomDataTableFilter = "Table"))
	static bool InitInGameUIManager(UObject* Ctx, UPARAM(meta = (DataTableMetaStruct = "InGameUIRegisterInfo")) UDataTable* Table, TSubclassOf<UInGameMainPanelBase> OverrideCls = nullptr);

	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static bool RegisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo);
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static bool UnregisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo);

	/*
	* @func: Async create widget，and add to viewport
	* @paran InName: Reg Name
	* @param InPayload: Transport to OnShown
	*/
	UFUNCTION(BlueprintCallable, BlueprintInternalUseOnly, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx", NeuronAction, HideThen))
	static void ShowInGameWidget(UObject* Ctx, 
								FName InName, 
								FGMPStructUnion InPayload, 
								bool bBringToTop, 
								const FOnShownComplete& OnComplete, 
								const FOnShownFailed& OnFailed);

	/*
	* @func Hide Widget
	* @param InName: Reg Name
	* @param bDestroy: false to hide
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static void HideInGameWidget(UObject* Ctx, FName InName, bool bDestroy = false);

	/*
	* @func Hide All Widget
	* @param bDestroy: false to hide
	*/
	UFUNCTION(BlueprintCallable, Category = "InGame UI Manager (In Game)", meta = (WorldContext = "Ctx"))
	static void HideAllInGameWidget(UObject* Ctx, bool bDestroy = false);
};