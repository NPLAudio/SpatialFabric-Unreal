// Copyright (c) 2026 AudioLoom Contributors.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/** Details customization for UAudioLoomLiveInputComponent: input/output device + channel selection, explicit refresh and Start/Stop. */
class FAudioLoomLiveInputDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
