// Copyright ImSlate, Inc. All Rights Reserved.
#include "ImSlateTemplate/ImNumericWidget.h"

#include "PrivateFieldAccessor.h"

#define LOCTEXT_NAMESPACE "ImNumericWidget"

namespace ImSlateNumeric
{
#define GS_PRIVATEACCESS_SPINBOX_MEMBER(T, AliasType)                                    \
	using AliasType = SSpinBox<T>;                                                       \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnBeginSliderMovement, FSimpleDelegate);          \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnEndSliderMovement, AliasType::FOnValueChanged); \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnValueChanged, AliasType::FOnValueChanged);      \
	GS_PRIVATEACCESS_MEMBER(AliasType, OnValueCommitted, AliasType::FOnValueCommitted);

GS_PRIVATEACCESS_SPINBOX_MEMBER(float, SSpinBoxFloat)
GS_PRIVATEACCESS_SPINBOX_MEMBER(double, SSpinBoxDouble)
GS_PRIVATEACCESS_SPINBOX_MEMBER(int32, SSpinBoxInt32)
GS_PRIVATEACCESS_SPINBOX_MEMBER(uint32, SSpinBoxUInt32)
GS_PRIVATEACCESS_SPINBOX_MEMBER(int64, SSpinBoxInt64)
GS_PRIVATEACCESS_SPINBOX_MEMBER(uint64, SSpinBoxUInt64)

template<typename T>
SSpinBox<T>* ToSpinBoxPtr(TSharedPtr<SWidget>& In)
{
	return static_cast<SSpinBox<T>*>(In.Get());
}
}  // namespace ImSlateNumeric

namespace ImSlate{
#define Z_IMSLATE_DEFINE_NUMERIC_WIDGET(TYPE)                                                                                         \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetOnBeginSliderMovement(FSimpleDelegate InOnBeginSliderMovement)                        \
	{                                                                                                                                 \
		ImSlateNumeric::PrivateAccess::OnBeginSliderMovement(*ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox)) = InOnBeginSliderMovement; \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetOnValueChanged(FOnValueChanged InOnValueChanged)                                      \
	{                                                                                                                                 \
		OnValueChanged = InOnValueChanged;                                                                                            \
		ImSlateNumeric::PrivateAccess::OnValueChanged(*ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox)) = InOnValueChanged;               \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetOnValueCommitted(FOnValueCommitted InOnValueCommitted)                                \
	{                                                                                                                                 \
		OnValueCommitted = InOnValueCommitted;                                                                                        \
		ImSlateNumeric::PrivateAccess::OnValueCommitted(*ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox)) = InOnValueCommitted;           \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetOnEndSliderMovement(FOnValueChanged InOnEndSliderMovement)                            \
	{                                                                                                                                 \
		ImSlateNumeric::PrivateAccess::OnEndSliderMovement(*ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox)) = InOnEndSliderMovement;     \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetMinValue(const TAttribute<TOptional<TYPE>>& InMinValue)                               \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetMinValue(InMinValue);                                                                                          \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetMaxValue(const TAttribute<TOptional<TYPE>>& InMaxValue)                               \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetMaxValue(InMaxValue);                                                                                          \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetMaxSliderValue(const TAttribute<TOptional<TYPE>>& InMaxSliderValue)                   \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetMaxSliderValue(InMaxSliderValue);                                                                              \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetMinSliderValue(const TAttribute<TOptional<TYPE>>& InMinSliderValue)                   \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetMinSliderValue(InMinSliderValue);                                                                              \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetDelta(const TYPE& InDeltaValue)                                                       \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetDelta(InDeltaValue);                                                                                           \
	}                                                                                                                                 \
	template<>                                                                                                                        \
	IMSLATE_API void SImNumericWidget<TYPE>::SetSliderExponent(const TAttribute<float>& InSliderExponent)                             \
	{                                                                                                                                 \
		auto* SpinBoxPtr = ImSlateNumeric::ToSpinBoxPtr<TYPE>(SpinBox);                                                               \
		SpinBoxPtr->SetSliderExponent(InSliderExponent);                                                                              \
	}

Z_IMSLATE_DEFINE_NUMERIC_WIDGET(float)
Z_IMSLATE_DEFINE_NUMERIC_WIDGET(double)
Z_IMSLATE_DEFINE_NUMERIC_WIDGET(int32)
Z_IMSLATE_DEFINE_NUMERIC_WIDGET(uint32)
Z_IMSLATE_DEFINE_NUMERIC_WIDGET(int64)
Z_IMSLATE_DEFINE_NUMERIC_WIDGET(uint64)

}


template<typename T>
TSharedRef<ImSlate::SImNumericWidget<T>> UImSlateNumericWidget::ConstructImWidget() const
{
	TSharedRef<SWidget> LabelWidget = SNullWidget::NullWidget;
	if (!LabelText.IsEmpty())
	{
		LabelWidget = ImSlate::SImNumericWidget<T>::BuildLabel(LabelText, &LabelBrush, BorderForegroundColor, BorderBackgroundColor);
	}

	auto MyStealSpinBox = SNew(ImSlate::SImNumericWidget<T>)
							.LabelPadding(LabelPadding)
							.BorderForegroundColor(BorderForegroundColor)
							.BorderBackgroundColor(BorderBackgroundColor)
							.EditableTextBoxStyle(&EditableTextBoxStyle)
							.Font(ImSlate::ScaleImSlateFont(Font))
							.SpinBoxStyle(&SpinBoxStyle)
							.AllowSpin(true)
							.MinValue(TOptional<T>())
							.MaxValue(TOptional<T>())
							.MinSliderValue(TOptional<T>())  // No lower limit
							.MaxSliderValue(TOptional<T>())  // No upper limit
							.UndeterminedString(LOCTEXT("Multiple Value", "MultipleValue"))
							.BorderImageNormal(&BorderImageNormal)
							.BorderImageHovered(&BorderImageHovered)
							.BorderImageFocused(&BorderImageFocused)
							.Label()
							[
								LabelWidget
							];
#if WIDGET_INCLUDE_RELFECTION_METADATA
	// We only need to do this once, when the slate widget is created.
	MyStealSpinBox->template AddMetadata<FReflectionMetaData>(MakeShared<FReflectionMetaData>(GetFName(), GetClass(), (UObject*)this, GetSourceAssetOrClass()));
#endif

	return MyStealSpinBox;
}

void UImSlateNumericWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyNumericWidget.Reset();
}

TSharedRef<SWidget> UImSlateNumericWidget::RebuildWidget()
{
	return SNullWidget::NullWidget;
}

void UImSlateNumericWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
}

#define IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(T, C)               \
	TSharedRef<ImSlate::SImNumericWidget<T>> C::ConstructImWidget() const \
	{                                                            \
		return UImSlateNumericWidget::ConstructImWidget<T>();    \
	}

IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(float, UImNumericFloatWidget)
IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(double, UImNumericDoubleWidget)
IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(int32, UImNumericInt32Widget)
IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(int64, UImNumericInt64Widget)
IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(uint32, UImNumericUInt32Widget)
IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET(uint64, UImNumericUInt64Widget)

#undef IMSLATE_DEF_MUMERIC_CONSTRUCT_WIDGET
#undef LOCTEXT_NAMESPACE
