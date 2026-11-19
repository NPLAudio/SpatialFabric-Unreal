// Copyright (c) 2026 SpatialFabric Contributors. MIT.
#pragma once
#include "Widgets/SCompoundWidget.h"
#include "AudioRoutingAuthoring.h"
class SAudioRoutingGrid;
class SScrollBar;
class SPATIALFABRICEDITOR_API SAudioRoutingPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAudioRoutingPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
#if WITH_DEV_AUTOMATION_TESTS
    AudioRouting::FMatrix& TestMatrix() { return Matrix; }
    // Same model/click invalidation as explicit refresh, without device enumeration.
    void TestRefreshCachedMatrix() { Capture(true); }
#endif
private:
    AudioRouting::FCache Cache;
    AudioRouting::FMatrix Matrix;
    TWeakObjectPtr<UAudioLoomComponent> Selected;
    TSharedPtr<SAudioRoutingGrid> Grid;
    FText Message;
    void Capture(bool bCancelPendingPress = false);
    TSharedRef<SWidget> InterfaceMenu();
    TSharedRef<SWidget> ChannelMenu();
    void Apply(const FString& Id, int32 Channel);
};
