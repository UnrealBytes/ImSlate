#include "InGameUIManager.h"
#include "Components/CanvasPanelSlot.h"
#include "GenericSingletons.h"
//#include "ResourceLoader.h"
#include "Engine/GameViewportClient.h"


DEFINE_LOG_CATEGORY(LogInGameUIManager);


UInGameUIManager* UInGameUIManager::Get(UObject* Ctx, bool bCreate)
{
	return UGenericSingletons::GetSingleton<UInGameUIManager>(Ctx, bCreate);
}

/*
* UInGameUIManager
*/

bool UInGameUIManager::InitUIManager(const TArray<FInGameUIRegisterInfo*>& RowItems, TSubclassOf<UInGameMainPanelBase> OverrideCls)
{
	if (MainPanel)
	{
		UE_LOG(LogInGameUIManager, Log, TEXT("Already Inited InGameUIManager!"));
		return false;
	}

	for (const auto& Item : RowItems)
	{
		RegisterWidget(*Item);
	}
	// WidgetPool.SetWorld(Ctx->GetWorld());

	MainPanel = NewObject<UInGameMainPanelBase>(GetWorld(),  GetManPanelClass(OverrideCls), "Main Panel");
	MainPanel->AddToGame(GetWorld(), DefaultZOrder);
	UE_LOG(LogInGameUIManager, Log, TEXT("Success Initial InGameUIManager And Create CanvasPanel To Viewport!"));
	return true;
}

bool UInGameUIManager::ShowWidgetByName(FName InName, const FGMPStructUnion& InPayload, bool bBringToTop, const FOnShownComplete& OnComplete, const FOnShownFailed& OnFailed)
{
	if (IsUICreated(InName))
	{
		UE_LOG(LogInGameUIManager, Log, TEXT("Widget(%s) is Already Created!"), *InName.ToString());
		
		SetWidgetVisibility(InName, ESlateVisibility::Visible);
		if (bBringToTop)
		{
			BringWidgetToTop(InName);
		}
		if (ensure(CreatedWidgetMap[InName]->GetClass()->ImplementsInterface(UInGameUIInc::StaticClass())))
		{
			IInGameUIInc::Execute_OnShown(CreatedWidgetMap[InName], InPayload);
		}
		OnComplete.ExecuteIfBound();
		return true;
	}

	if (!IsUIRegisted(InName))
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("Widget(%s) is not Register!"), *InName.ToString());
		OnFailed.ExecuteIfBound(TEXT("Widget Is Not Register!"));
		return false;
	}

	const FSoftObjectPath& ObjPath = GlobalUIMap[InName].ToSoftObjectPath();
	UGenericSingletons::AsyncLoadCls(ObjPath.ToString(), this, [this, InPayload, InName, OnComplete, OnFailed](UClass* WidgetClass) { 
		if (WidgetClass && ensure(WidgetClass->ImplementsInterface(UInGameUIInc::StaticClass())))
		{
			UE_LOG(LogInGameUIManager, Log, TEXT("WidgetClass(%s) AsyncLoad Success"), *WidgetClass->GetName());
			// UWidget* NewChild = WidgetPool.GetOrCreateInstance(*WidgetClass);
			UWidget* NewChild = MainPanel->ConstructWidget<UWidget>(WidgetClass, InName);
			if (!IsValid(NewChild))
			{
				UE_LOG(LogInGameUIManager, Warning, TEXT("Create Widget(%s) Failed!"), *InName.ToString());
				OnFailed.ExecuteIfBound(TEXT("Construct Widget Failed!"));
				return;
			}

			if(ensure(NewChild->GetFName() == InName))
			{
				MainPanel->AddChildToCanvas(NewChild);
				CreatedWidgetMap.Add(InName, NewChild);
				ActivateWidgetStack.Add(InName);
				UE_LOG(LogInGameUIManager, Log, TEXT("Success Create Widget(%s)!"), *InName.ToString());

				IInGameUIInc::Execute_OnShown(CreatedWidgetMap[InName], InPayload);
				OnComplete.ExecuteIfBound();
			}
			else
			{
				UE_LOG(LogInGameUIManager, Error, TEXT("Widget Class(%s) With InValid Widget Name(%s)"), *WidgetClass->GetName(), *NewChild->GetName());
				OnFailed.ExecuteIfBound(TEXT("Widget Class With InValid Widget Name!"));
			}
		}
		else
		{
			UE_LOG(LogInGameUIManager, Error, TEXT("Failed To AsyncLoad Widget Class(%s)"), *WidgetClass->GetName());
			OnFailed.ExecuteIfBound(TEXT("Failed To AsyncLoad Widget Class!"));
		}
	});
	return true;
}

bool UInGameUIManager::RegisterWidget(const FInGameUIRegisterInfo& RegInfo)
{
	if (IsUIRegisted(RegInfo.RegName))
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("[Register Failed] Widget(%s) Already Registed!"), *RegInfo.RegName.ToString());
		return false;
	}
	GlobalUIMap.Add(RegInfo.RegName, RegInfo.WidgetClass);
	return true;
}
bool UInGameUIManager::UnregisterWidget(const FInGameUIRegisterInfo& RegInfo)
{
	if (!IsUIRegisted(RegInfo.RegName))
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("[Unregister Failed] Widget(%s) Not Registed!"), *RegInfo.RegName.ToString());
		return false;
	}
	DestroyWidget(RegInfo.RegName);
	GlobalUIMap.Remove(RegInfo.RegName);
	return true;
}


void UInGameUIManager::SetWidgetVisibility(FName InName, ESlateVisibility InVisibility)
{
	if (!IsUICreated(InName))
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("[Set Visibility Failed] UMG has not been created!"));
		return;
	}
	CreatedWidgetMap[InName]->SetVisibility(InVisibility);
}

bool UInGameUIManager::DestroyWidget(FName InName)
{
	if (!IsUICreated(InName))
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("[Destroy Failed] UMG has not been created!"));
		return false;
	}

	UClass* UMGClass = CreatedWidgetMap[InName]->GetClass();
	CreatedWidgetMap[InName]->RemoveFromParent();
	return true;
}

bool UInGameUIManager::BringWidgetToTop(FName InName)
{
	bool bSelected = false;
	if (CreatedWidgetMap.Find(InName) && CreatedWidgetMap[InName])
	{
		for (auto& WName : ActivateWidgetStack)
		{
			if (WName == InName)
			{
				ActivateWidgetStack.Remove(InName);
				ActivateWidgetStack.Add(InName);
				bSelected = true;
			}
		}
	}
	UpdateAllWidgetZOrder();
	return bSelected;
}

bool UInGameUIManager::DestroyAllWidget()
{
	auto TempWidgetNameList = ActivateWidgetStack;
	for (const auto& item : TempWidgetNameList)
	{
		DestroyWidget(item);
	}
	return true;
}

void UInGameUIManager::HiddenAll()
{
	for (const auto& item : CreatedWidgetMap)
	{
		SetWidgetVisibility(item.Key, ESlateVisibility::Hidden);
	}
}

UClass* UInGameUIManager::GetManPanelClass(TSubclassOf<UInGameMainPanelBase> InCls)
{
	if (InCls)
	{
		return InCls.Get();
	}
	if (!MainPanelClassHolder)
	{
		MainPanelClassHolder = MainPanelClass.LoadSynchronous();
		if (!MainPanelClassHolder)
		{
			MainPanelClassHolder = UInGameMainPanelBase::StaticClass();
		}
	}
	return MainPanelClassHolder;
}

void UInGameUIManager::UpdateAllWidgetZOrder()
{
	int32 ZOrderOffset = 1;
	for (int32 i = 0; i < ActivateWidgetStack.Num(); i++)
	{
		auto* CurrWidget = CreatedWidgetMap[ActivateWidgetStack[i]];
		if (IsValid(CurrWidget))
		{
			Cast<UCanvasPanelSlot>(CurrWidget->Slot)->SetZOrder(i);
		}
	}
}

void UInGameMainPanelBase::AddToGame(UWorld* InWorld, int32 ZOrder)
{
	if (UPanelWidget* ParentPanel = GetParent())
	{
		UE_LOG(LogInGameUIManager, Warning, TEXT("The widget %s already has a parent widget.  It can't also be added to the viewport!"), *GetClass()->GetName());
		return;
	}
	if (InWorld && InWorld->IsGameWorld())
	{
		if (UGameViewportClient* ViewportClient = InWorld->GetGameViewport())
		{
			ViewportClient->AddViewportWidgetContent(RebuildWidget(), ZOrder + 10);
			FWorldDelegates::OnWorldBeginTearDown.RemoveAll(this);

			FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UInGameMainPanelBase::OnLeaveWorld);
		}
	}
}

void UInGameMainPanelBase::OnSlotAdded(UPanelSlot* InSlot)
{
	Super::OnSlotAdded(InSlot);
}

void UInGameMainPanelBase::OnSlotRemoved(UPanelSlot* InSlot)
{
	/*
	* Avoid Widget reference RemoveFromParent
	* Force refresh UI Manager info
	*/
	UE_LOG(LogInGameUIManager, Warning, TEXT("Slot Remove, Slot Content Is %s"), *FString(InSlot->Content->GetClass()->GetName()));
	UClass* UMGClass = InSlot->Content->GetClass();

	if (ensure(UMGClass->ImplementsInterface(UInGameUIInc::StaticClass())))
	{
		FName WidgetName = InSlot->Content->GetFName();
		UInGameUIManager* UIManager = UInGameUIManager::Get(GetOuter(), false);
		if (ensure(UIManager && UIManager->IsUICreated(WidgetName)))
		{
			UE_LOG(LogInGameUIManager, Log, TEXT("Slot Remove, The Widget(%s) Removed Success"), *WidgetName.ToString());
			IInGameUIInc::Execute_OnDestroyed(UIManager->CreatedWidgetMap[WidgetName]);
			UIManager->ActivateWidgetStack.Remove(WidgetName);
			UIManager->CreatedWidgetMap.Remove(WidgetName);
			
			// UIManager->WidgetPool.Release(InSlot.Content);
		}
		else
		{
			UE_LOG(LogInGameUIManager, Error, TEXT("Slot Remove, UnKnown Error"), *WidgetName.ToString());
		}
	}
	
	Super::OnSlotRemoved(InSlot);
}

void UInGameMainPanelBase::OnWidgetBecomeTop(UWidget* NewTopWidget, UWidget* OldTop)
{
	
}

void UInGameMainPanelBase::OnLeaveWorld(UWorld* InWorld)
{
	if (InWorld && !HasAnyFlags(RF_BeginDestroyed))
	{
		if (GetWorld() && GetWorld()->IsGameWorld())
		{
			if(auto UIManager = UInGameUIManager::Get(InWorld, false))
			{
				UIManager->DestroyAllWidget();
			}

			if (UGameViewportClient* ViewportClient = InWorld->GetGameViewport())
			{
				ViewportClient->RemoveViewportWidgetContent(MyCanvas.ToSharedRef());
				FWorldDelegates::OnWorldBeginTearDown.RemoveAll(this);
			}
			else
			{
				MyCanvas.Reset();
			}
		}
	}
}
