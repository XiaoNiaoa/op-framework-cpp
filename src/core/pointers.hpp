#pragma once

#include <Server/Components/Pawn/pawn.hpp>

class OPFrameworkComponent;

struct OPCoreSharedPtr
{
    friend class OPFrameworkComponent;

private:
    inline static ICore* core_ = nullptr;
    inline static IComponentList* componentList_ = nullptr;

    inline static IPickupsComponent* pickups_ = nullptr;
    inline static IObjectsComponent* objects_ = nullptr;
    inline static ITextLabelsComponent* textlabels_ = nullptr;
    inline static IVehiclesComponent* vehicles_ = nullptr;
    inline static IActorsComponent* actors_ = nullptr;
    inline static IClassesComponent* classes_ = nullptr;
    inline static IConsoleComponent* console_ = nullptr;
    inline static ICustomModelsComponent* models_ = nullptr;
    inline static IMenusComponent* menus_ = nullptr;
    inline static ITextDrawsComponent* textdraws_ = nullptr;
    inline static IGangZonesComponent* gangzones_ = nullptr;
    inline static ICheckpointsComponent* checkpoints_ = nullptr;
    inline static IDialogsComponent* dialogs_ = nullptr;
    inline static INPCComponent* npcs_ = nullptr;
    inline static IPawnComponent* pawn_ = nullptr;
    inline static ITimersComponent* timers_ = nullptr;

    static void initSharedPtr(IComponentList* components);
    static void initCoreSharedPtr(ICore* core);
    static void registerPawnNatives(AMX* amx);
    static void freeSharedPtr();
    static void freeSharedPtr(IComponent* component);

    template <typename ComponentType>
    static ComponentType* getComponent()
    {
        return componentList_ ? componentList_->queryComponent<ComponentType>() : nullptr;
    }

public:
    static ICore* core() { return core_; }
    static IComponentList* componentList() { return componentList_; }

    static IPickupsComponent* pickups() { return pickups_; }
    static IObjectsComponent* objects() { return objects_; }
    static ITextLabelsComponent* textlabels() { return textlabels_; }
    static IVehiclesComponent* vehicles() { return vehicles_; }
    static IActorsComponent* actors() { return actors_; }
    static IClassesComponent* classes() { return classes_; }
    static IConsoleComponent* console() { return console_; }
    static ICustomModelsComponent* models() { return models_; }
    static IMenusComponent* menus() { return menus_; }
    static ITextDrawsComponent* textdraws() { return textdraws_; }
    static IGangZonesComponent* gangzones() { return gangzones_; }
    static ICheckpointsComponent* checkpoints() { return checkpoints_; }
    static IDialogsComponent* dialogs() { return dialogs_; }
    static INPCComponent* npcs() { return npcs_; }
    static IPawnComponent* pawn() { return pawn_; }
    static ITimersComponent* timers() { return timers_; }
};
