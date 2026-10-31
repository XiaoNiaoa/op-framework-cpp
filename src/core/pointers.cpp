#include <Server/Components/Pawn/Impl/pawn_natives.hpp>
#include <Server/Components/Pawn/Impl/pawn_impl.hpp>

#include "pointers.hpp"

void OPCoreSharedPtr::initSharedPtr(IComponentList* components)
{
    componentList_ = components;

    pickups_ = getComponent<IPickupsComponent>();
    objects_ = getComponent<IObjectsComponent>();
    textlabels_ = getComponent<ITextLabelsComponent>();
    vehicles_ = getComponent<IVehiclesComponent>();
    actors_ = getComponent<IActorsComponent>();
    classes_ = getComponent<IClassesComponent>();
    console_ = getComponent<IConsoleComponent>();
    models_ = getComponent<ICustomModelsComponent>();
    menus_ = getComponent<IMenusComponent>();
    textdraws_ = getComponent<ITextDrawsComponent>();
    gangzones_ = getComponent<IGangZonesComponent>();
    checkpoints_ = getComponent<ICheckpointsComponent>();
    dialogs_ = getComponent<IDialogsComponent>();
    npcs_ = getComponent<INPCComponent>();
    pawn_ = getComponent<IPawnComponent>();
    timers_ = getComponent<ITimersComponent>();

    if (pawn_)
    {
        setAmxFunctions(pawn_->getAmxFunctions());
        setAmxLookups(components);
    }
    else
    {
        setAmxFunctions();
    }
}

void OPCoreSharedPtr::initCoreSharedPtr(ICore* core)
{
    if (core)
    {
        core_ = core;
        setAmxLookups(core_);
    }
}

void OPCoreSharedPtr::registerPawnNatives(AMX* amx)
{
    pawn_natives::AmxLoad(amx);
}

void OPCoreSharedPtr::freeSharedPtr()
{
    setAmxFunctions();

    pickups_ = nullptr;
    objects_ = nullptr;
    textlabels_ = nullptr;
    vehicles_ = nullptr;
    actors_ = nullptr;
    classes_ = nullptr;
    console_ = nullptr;
    models_ = nullptr;
    menus_ = nullptr;
    textdraws_ = nullptr;
    gangzones_ = nullptr;
    checkpoints_ = nullptr;
    dialogs_ = nullptr;
    npcs_ = nullptr;
    pawn_ = nullptr;
    timers_ = nullptr;

    componentList_ = nullptr;
    core_ = nullptr;
}

void OPCoreSharedPtr::freeSharedPtr(IComponent* component)
{
    if (component == pawn_)
    {
        pawn_ = nullptr;
        setAmxFunctions();
        setAmxLookups();
    }

    if (component == pickups_) pickups_ = nullptr;
    if (component == objects_) objects_ = nullptr;
    if (component == textlabels_) textlabels_ = nullptr;
    if (component == vehicles_) vehicles_ = nullptr;
    if (component == actors_) actors_ = nullptr;
    if (component == classes_) classes_ = nullptr;
    if (component == console_) console_ = nullptr;
    if (component == models_) models_ = nullptr;
    if (component == menus_) menus_ = nullptr;
    if (component == textdraws_) textdraws_ = nullptr;
    if (component == gangzones_) gangzones_ = nullptr;
    if (component == checkpoints_) checkpoints_ = nullptr;
    if (component == dialogs_) dialogs_ = nullptr;
    if (component == npcs_) npcs_ = nullptr;
    if (component == timers_) timers_ = nullptr;
}
