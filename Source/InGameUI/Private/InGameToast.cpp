#include "InGameToast.h"

#include "Blueprint/UserWidget.h"
#include "GenericSingletons.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogInGameToast, Log, All);

UInGameToastManager* UInGameToastManager::GetSingleton(UObject* InWorldContext, TSubclassOf<UInGameToastManager> InClass)
{
	if (!InWorldContext)
	{
		UE_LOG(LogInGameToast, Warning, TEXT("GetSingleton: InWorldContext is null."));
		return (UInGameToastManager*)nullptr;
	}
	return UGenericSingletons::GetSingleton<UInGameToastManager>(InWorldContext, true, InClass);
}

UWidget* UInGameToastManager::GerWidgetFromPool(TSubclassOf<UWidget> InWidgetClass)
{
	return WidgetPool.GetOrCreateInstance(InWidgetClass ? InWidgetClass : WidgetClass);
}

void UInGameToastManager::ReleaseWidgetToPool(UWidget* InWidget)
{
	WidgetPool.Release(InWidget);
}

void UInGameToastManager::OnWidgetClassLoaded_Implementation(TSubclassOf<UWidget> InWidgetClass)
{
	WidgetClass = InWidgetClass;
}

void UInGameToastManager::OnAllocWidget_Implementation(UWidget*& OutWidget)
{
	OutWidget = GerWidgetFromPool(WidgetClass);
}

void UInGameToastManager::OnFreeWidget_Implementation(UWidget* InWidget)
{
	ReleaseWidgetToPool(InWidget);
}

void UInGameToastManager::OnShowWidget_Implementation(UWidget* InWidget, const FGMPStructUnion& StructUnion)
{
	if (ensure(InWidget->Implements<UInGameToastWidgetInc>()))
	{
		IInGameToastWidgetInc::Execute_OnShowToast(InWidget, StructUnion, InWidget);
	}
	else
	{
		IInGameToastWidgetInc::ShowToastWidget(InWidget);
	}
}

void UInGameToastManager::OnHideWidget_Implementation(UWidget* InWidget, const FGMPStructUnion& StructUnion)
{
	float Fading = 0.f;
	if (ensure(InWidget->Implements<UInGameToastWidgetInc>()))
	{
		IInGameToastWidgetInc::Execute_OnHideToast(InWidget, StructUnion, InWidget);
		IInGameToastWidgetInc::Execute_GetFadingDuration(InWidget, Fading);
	}
	else
	{
		IInGameToastWidgetInc::HideToastWidget(InWidget);
	}

	if (Fading > 0.f)
	{
		DelayExec(
			this,
			[this, InWidget] { OnFreeWidget(InWidget); },
			Fading);
	}
	else
	{
		OnFreeWidget(InWidget);
	}
}

void UInGameToastManager::Init()
{
	if (ensure(IsValid()))
	{
		ToastQueue.Empty();
		WidgetPool.SetWorld(GetWorld());
		UGenericSingletons::AsyncLoadCls(ToastWidgetPath.ToString(), this, [this](UClass* InWidgetClass) {
			OnWidgetClassLoaded(InWidgetClass);
			if (!InWidgetClass)
			{
				UE_LOG(LogInGameToast, Warning, TEXT("OnAsyncLoadAsset: WidgetClass is Invalid %s"), *ToastWidgetPath.ToString());
			}
			else
			{
				ensure(InWidgetClass->ImplementsInterface(UInGameToastWidgetInc::StaticClass()));
				if (!TickHandle && !!ToastQueue.Num())
				{
					TickHandle = MakeUnique<FTickableProxy>(this);
				}
			}
		});
	}
}

bool UInGameToastManager::IsValid() const
{
	return GetWorld() && GetWorld()->IsGameWorld();
}

void UInGameToastManager::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		Init();
	}
}

void UInGameToastManager::ShowToast(const FGMPStructUnion& Union)
{
	if (!IsValidLowLevel())
		return;

	if (ToastWidgetPath.ToString().IsEmpty())
	{
		UE_LOG(LogInGameToast, Warning, TEXT("ShowToast: ToastWidgetPath is empty."));
		return;
	}

	auto ItemPtr = new FInGameToastItem();
	ItemPtr->StructUnion = Union;
	ToastQueue.Add(ItemPtr);

	if (!TickHandle && WidgetClass)
	{
		TickHandle = MakeUnique<FTickableProxy>(this);
	}
}

void UInGameToastManager::Tick(float Delta)
{
	if (ToastCursor >= 0)
	{
		while (DequeueToast(Delta))
		{
			--ToastCursor;
		}
	}

	if (!ToastQueue.Num())
	{
		TickHandle.Reset();
		UE_LOG(LogInGameToast, Log, TEXT("toast queue is empty"));
		return;
	}

	if (ToastCursor < QueueCount)
	{
		while (EnqueueToast())
		{
			if (++ToastCursor >= QueueCount)
				break;
		}
	}
}

bool UInGameToastManager::EnqueueToast()
{
	if (ToastCursor > 0 && ToastQueue.Num() > ToastCursor)
	{
		FInGameToastItem* ItemPtr = &ToastQueue[ToastCursor];
		UWidget* Widget = nullptr;
		OnAllocWidget(Widget);
		if (Widget)
		{
			ItemPtr->Widget = Widget;
			ItemPtr->LeftDuration = DefaultDuration;
			OnShowWidget(Widget, ItemPtr->StructUnion);
		}
		return true;
	}
	return false;
}

bool UInGameToastManager::DequeueToast(float DeltaSeconds)
{
	check(ToastQueue.Num() > 0);
	FInGameToastItem* ItemPtr = &ToastQueue[0];
	ItemPtr->LeftDuration -= DeltaSeconds;
	if (ItemPtr->LeftDuration <= 0.f)
	{
		auto Widget = ItemPtr->Widget;
		OnHideWidget(Widget, ItemPtr->StructUnion);
		ToastQueue.RemoveAt(0);
		return true;
	}
	return false;
}

void UInGameToastManager::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	for (auto& Item : CastChecked<UInGameToastManager>(InThis)->ToastQueue)
	{
		Item.StructUnion.AddStructReferencedObjects(Collector);
		Collector.AddReferencedObject(Item.Widget);
	}
}

void IInGameToastWidgetInc::OnShowToast_Implementation(const FGMPStructUnion& Union, UWidget* CurWidget)
{
	IInGameToastWidgetInc::Execute_OnSetData(CurWidget, Union);
	ShowToastWidget(CurWidget);
}

void IInGameToastWidgetInc::OnHideToast_Implementation(const FGMPStructUnion& Union, UWidget* CurWidget)
{
	HideToastWidget(CurWidget);
}

void IInGameToastWidgetInc::ShowToastWidget(UWidget* CurWidget)
{
	if (auto UserWidget = Cast<UUserWidget>(CurWidget))
		UserWidget->AddToViewport();
}

void IInGameToastWidgetInc::HideToastWidget(UWidget* CurWidget)
{
	if (auto UserWidget = Cast<UUserWidget>(CurWidget))
		UserWidget->RemoveFromParent();
}
