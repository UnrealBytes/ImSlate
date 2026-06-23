// Copyright ImSlate, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Slate.h"

#include "Components/Widget.h"
#include "GenericSingletons.h"
#include "ProtectFieldAccessor.h"
#include "Types/ReflectionMetadata.h"

template<typename T>
class SImSlateWrapper : public T
{
public:
	TSharedPtr<void> ImSlateMeta;
};

namespace ImSlate
{
struct IImSlateCtxBase
{
	TWeakObjectPtr<UObject> ImSlateWorldCtx;
};
namespace Factory
{
	FORCEINLINE void SetImSlateWorldContext(IImSlateCtxBase* Inc, UObject* InCtx) { Inc->ImSlateWorldCtx = InCtx; }
	template<typename T>
	FORCEINLINE void SetImSlateWorldContext(T*, UObject* InCtx)
	{
	}
}  // namespace Factory

inline UObject* GetWorldCtxChecked(const IImSlateCtxBase* InCtx)
{
	return InCtx->ImSlateWorldCtx.Get();
}
}  // namespace ImSlate

#if WITH_EDITOR
#define ImEnsure(C) ensureWorld(ImSlate::GetWorldCtxChecked(this), (C))
#define ImEnsureMsgf(C, F, ...) ensureWorldMsgf(ImSlate::GetWorldCtxChecked(this), (C), F, ##__VA_ARGS__)
#else
#define ImEnsure(C) ensure((C))
#define ImEnsureMsgf(C, F, ...) ensureMsgf((C), F, ##__VA_ARGS__)
#endif

template<typename S>
class TImFactory
{
public:
	template<typename U>
	TSharedRef<S> ConstructWidget(const U* InUMGWidget, UObject* InCtx = nullptr)
	{
		static_assert(std::is_base_of<UWidget, U>::value && std::is_base_of<TImFactory, U>::value, "err");
		auto Ret = InUMGWidget->ConstructImWidget();
		using namespace ImSlate::Factory;
		SetImSlateWorldContext(&Ret.Get(), InCtx);
		return Ret;
	}
};

namespace ImSlate
{
IMSLATE_API FText GetPaletteCategory();
#if WITH_EDITOR
#define IM_SLATE_PALETTECATEGORY() \
	virtual const FText GetPaletteCategory() override { return ImSlate::GetPaletteCategory(); }
#else
#define IM_SLATE_PALETTECATEGORY()
#endif
IMSLATE_API UWorld* GetWorldChecked(const UObject* InCtx = nullptr);
namespace Factory
{
	IMSLATE_API UClass* GetDefaultImpl(TSubclassOf<UWidget> InClass);
}

template<typename U>
FORCEINLINE UClass* GetDefaultClass()
{
	return Factory::GetDefaultImpl(U::StaticClass());
}

template<typename U>
FORCEINLINE_DEBUGGABLE TSubclassOf<U> GetTypeClass(TSubclassOf<U> Type)
{
	static_assert(std::is_base_of<UWidget, U>::value, "err");
	auto TypeClass = Type.Get() ? Type.Get() : GetDefaultClass<U>();
	check(TypeClass);
	return TypeClass;
}

// Custom Widgets
#if UE_SERVER
template<typename U, typename = void>
#else
template<typename U, typename = TEnableIf<!std::is_base_of<UUserWidget, U>::value>>
#endif
auto ImFactoryCreate(TSubclassOf<U> Type, const UObject* InCtx = nullptr)
{
	auto WorldCtx = GetWorldChecked(InCtx);
	auto CDO = static_cast<U*>(UGenericSingletons::GetSingletonImpl(GetTypeClass(Type), WorldCtx));
	return CDO->ConstructWidget(CDO, WorldCtx);
}
#if UE_SERVER
template<typename U, typename = void>
#else
template<typename U, typename = TEnableIf<!std::is_base_of<UUserWidget, U>::value>>
#endif
auto ImFactoryCreate(const UObject* InCtx = nullptr)
{
	auto WorldCtx = GetWorldChecked(InCtx);
	auto CDO = static_cast<U*>(UGenericSingletons::GetSingletonImpl(GetDefaultClass<U>(), WorldCtx));
	return CDO->ConstructWidget(CDO, WorldCtx);
}
// Scale helper for all ImSlate controls
IMSLATE_API float GetImSlateEffectiveScale();

// Keyboard-specific scale = GetImSlateEffectiveScale() × imslate.KeyboardScale. The virtual
// keyboard (layout AND the key widgets' ComputeDesiredSize / gesture thresholds) must all use
// THIS so the keys, spacing and hit areas scale together. Defined in ImVirtualKeyboard.cpp.
IMSLATE_API float GetImSlateKeyboardScale();

inline FSlateFontInfo GetImSlateDefaultFont(int32 BaseSize = 10)
{
	int32 ScaledSize = FMath::RoundToInt(BaseSize * FMath::Max(GetImSlateEffectiveScale(), 1.f));
	return FCoreStyle::GetDefaultFontStyle("Regular", ScaledSize);
}

inline FSlateFontInfo ScaleImSlateFont(const FSlateFontInfo& InFont)
{
	if (!InFont.HasValidFont())
		return GetImSlateDefaultFont();
	float Scale = GetImSlateEffectiveScale();
	if (Scale > 1.f)
	{
		FSlateFontInfo Scaled = InFont;
		Scaled.Size = FMath::RoundToInt(Scaled.Size * Scale);
		return Scaled;
	}
	return InFont;
}

}  // namespace ImSlate

#if !UE_SERVER
#include "ImSlateTemplate/ImSlateFactory.inl"
#endif
