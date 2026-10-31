#pragma once

#include <sdk.hpp>
#include <Server/Components/Pawn/pawn.hpp>

class OPFrameworkComponent final
    : public IComponent
    , public PawnEventHandler
    , public CoreEventHandler
{
private:
    inline static OPFrameworkComponent* instance_ = nullptr;

    OPFrameworkComponent() = default;

public:
    static OPFrameworkComponent* getInstance();
    ~OPFrameworkComponent() override;

    PROVIDE_UID(0xD860DB2E83C6CE66);
    StringView componentName() const override;
    SemanticVersion componentVersion() const override;

    void provideConfiguration(ILogger& logger, IEarlyConfig& config, bool defaults) override;
    void onLoad(ICore* core) override;
    void onInit(IComponentList* components) override;
    void onReady() override;
    void onFree(IComponent* component) override;
    void free() override;
    void reset() override;

    void onAmxLoad(IPawnScript& script) override;
    void onAmxUnload(IPawnScript& script) override;
    void onTick(Microseconds elapsed, TimePoint now) override;
};
