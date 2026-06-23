#include "InGameUIHelper.h"

bool UInGameUIHelper::InitInGameUIManager(UObject* Ctx, UDataTable* Table, TSubclassOf<UInGameMainPanelBase> OverrideCls)
{
	TArray<FInGameUIRegisterInfo*> RegisterInfos;
	if (Table && Table->GetRowStruct() == TBaseStructure<FInGameUIRegisterInfo>::Get())
	{
		static const FString ContextString(TEXT("UInGameUIManager::InGameRegisterInfoTable"));
		UE_LOG(LogInGameUIManager, Log, TEXT("%s"), *ContextString);		
		Table->GetAllRows<FInGameUIRegisterInfo>(ContextString, RegisterInfos);
	}

	return UInGameUIManager::Get(Ctx, true)->InitUIManager(RegisterInfos, OverrideCls);;
}

bool UInGameUIHelper::RegisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo)
{
	UInGameUIManager* UIManager = UInGameUIManager::Get(Ctx, false);
	return UIManager && UIManager->RegisterWidget(RegInfo);
}

bool UInGameUIHelper::UnregisterInGameUI(UObject* Ctx, FInGameUIRegisterInfo RegInfo)
{
	UInGameUIManager* UIManager = UInGameUIManager::Get(Ctx, false);
	return UIManager && UIManager->UnregisterWidget(RegInfo);
}

void UInGameUIHelper::ShowInGameWidget(UObject* Ctx, FName InName, FGMPStructUnion InPayload, bool bBringToTop, const FOnShownComplete& OnComplete, const FOnShownFailed& OnFailed)
{
	UInGameUIManager* UIManager = UInGameUIManager::Get(Ctx, false);
	if (IsValid(UIManager))
	{
		UIManager->ShowWidgetByName(InName, InPayload, bBringToTop, OnComplete, OnFailed);
	}
}

void UInGameUIHelper::HideInGameWidget(UObject* Ctx, FName InName, bool bDestroy)
{
	UInGameUIManager* UIManager = UInGameUIManager::Get(Ctx, false);
	if (IsValid(UIManager) && !UIManager->IsUICreated(InName))
	{
		UIManager->SetWidgetVisibility(InName, ESlateVisibility::Hidden);
		if (bDestroy)
		{
			UIManager->DestroyWidget(InName);
		}
	}
}

void UInGameUIHelper::HideAllInGameWidget(UObject* Ctx, bool bDestroy)
{
	UInGameUIManager* UIManager = UInGameUIManager::Get(Ctx, false);
	if (IsValid(UIManager))
	{
		UIManager->HiddenAll();
		if (bDestroy)
		{
			UIManager->DestroyAllWidget();
		}
	}
	
}

