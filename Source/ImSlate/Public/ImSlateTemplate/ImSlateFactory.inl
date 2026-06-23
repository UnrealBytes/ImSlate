// Copyright ImSlate, Inc. All Rights Reserved.
#include "CoreMinimal.h"

#include "Components/Widget.h"
#include "ImSlateWidgetPool.h"

namespace ImWidget
{

template<class S = SWidget, typename U = UWidget>
auto ImFactoryCreate(TSubclassOf<U> Type)
{
	auto Inst = UGenericSingletons::CreateInstance<U>(ImSlate::GetWorldChecked(), ImSlate::GetTypeClass(Type));
	return StaticCastSharedRef<S>(Inst->TakeWidget());
}

template<class S = SWidget, typename U = UWidget>
auto ImFactoryCreate(TSubclassOf<U> Type, FUWidgetPool& InPool)
{
	auto Inst = InPool.GetOrCreateInstance<U>(ImSlate::GetTypeClass(Type));
	return StaticCastSharedRef<S>(Inst->TakeWidget());
}
}  // namespace ImWidget

namespace ImSlate
{
template<class S = SWidget, typename U = UWidget, typename = TEnableIf<std::is_base_of<UWidget, U>::value && !std::is_base_of<UUserWidget, U>::value>>
auto ImFactoryCreate(TSubclassOf<U> Type, FUWidgetPool* InPool)
{
	return InPool ? ImWidget::ImFactoryCreate<S, U>(Type, *InPool) : ImWidget::ImFactoryCreate<S, U>(Type);
}
}  // namespace ImSlate

#if !UE_SERVER
#include "Blueprint/UserWidget.h"
#include "Blueprint/UserWidgetPool.h"
#include "Slate/SObjectWidget.h"

namespace ImUserWidget
{
template<typename S>
S* ConstructWidgetMethod(UUserWidget* Widget, TSharedRef<SWidget> Content)
{
	static_assert(std::is_base_of<SObjectWidget, S>::value, "err");
	return SNew(S, Widget)
	[
		Content
	];
}

template<class S = SObjectWidget, typename U = UUserWidget>
auto ImFactoryCreate(TSubclassOf<U> Type, UWidget::ConstructMethodType ConstructMethod = ConstructWidgetMethod<S>)
{
	auto Inst = UGenericSingletons::CreateInstance<U>(ImSlate::GetWorldChecked(), ImSlate::GetTypeClass(Type));
	return Inst->template TakeDerivedWidget<S>(ConstructMethod);
}

template<class S = SObjectWidget, typename U = UUserWidget>
auto ImFactoryCreate(TSubclassOf<U> Type, FUserWidgetPool& InPool, UWidget::ConstructMethodType ConstructMethod = ConstructWidgetMethod<S>)
{
	auto Inst = InPool.GetOrCreateInstance<U>(ImSlate::GetTypeClass(Type));
	return Inst->template TakeDerivedWidget<S>(ConstructMethod);
}
}  // namespace ImUserWidget

namespace ImSlate
{
// UserWidgets
template<class S = SObjectWidget, typename U = UUserWidget, typename = TEnableIf<std::is_base_of<UUserWidget, U>::value>>
auto ImFactoryCreate(TSubclassOf<U> Type, FUserWidgetPool* InPool)
{
	return InPool ? ImUserWidget::ImFactoryCreate<S, U>(Type, *InPool) : ImUserWidget::ImFactoryCreate<S, U>(Type);
}

}  // namespace ImSlate
#endif
