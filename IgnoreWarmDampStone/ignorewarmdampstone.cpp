// Control which conditions (warm/damp) removes digging designations.
#include "pch.h"

#include <string>
#include <vector>

#include "modules/Persistence.h"
#include "modules/World.h"

#include "df/job_type.h"
#include "df/map_block.h"
#include "df/tile_designation.h"
#include "df/world.h"

#include "Core.h"
#include "Debug.h"
#include "PluginManager.h"

using namespace df;
using namespace DFHack;
using namespace HookEngineLib;

using std::string;
using std::vector;

DFHACK_PLUGIN("ignorewarmdampstone");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(world);

// logging levels can be dynamically controlled with the `debugfilter` command.
namespace DFHack {
    // for configuration-related logging
    DBG_DECLARE(ignorewarmdampstone, status, DebugCategory::LINFO);
}

static const string CONFIG_KEY = string(plugin_name) + "/config";
static PersistentDataItem config;
enum ConfigValues {
    CONFIG_IS_ENABLED = 0,
    CONFIG_SKIP_DAMP_CANCELLATION = 1,
    CONFIG_SKIP_WARM_CANCELLATION = 2,
};
static int get_config_val(int index) {
    if (!config.isValid())
        return -1;
    return config.ival(index);
}
static bool get_config_bool(int index) {
    return get_config_val(index) == 1;
}
static void set_config_val(int index, int value) {
    if (config.isValid())
        config.ival(index) = value;
}
static void set_config_bool(int index, bool value) {
    set_config_val(index, value ? 1 : 0);
}
static bool toggle_option(int option) {
    bool value = !get_config_bool(option);

    set_config_bool(option, value);

    return value;
}

HookEngine HookManager;

constexpr const char* CHECK_REVEALED_PATTERN = "##42??????????????83??7046";
constexpr const char* CHECK_REVEALED_HOOK = "CheckRevealedHook";
constexpr int CHECK_REVEALED_OFFSET = -96;

// hooked method signature
typedef bool (__stdcall* fnCheckRevealed)(map_block *pMapBlock, __int16 i, __int16 j);

int __stdcall CheckRevealedHook(map_block* pMapBlock_in, __int16 i, __int16 j)
{
    // retrieve the digging designation before calling the game method
    auto digFlag = pMapBlock_in->designation[i][j].bits.dig;
    // call the game method ASAP and avoid calling any other method before as it could mess up registers
    // specifically `test r10b, r10b` will fail if r10 >= 0x10, as it only tests the least significant bit
    auto result = ((fnCheckRevealed)HookManager.GetTrampolineFunc(CHECK_REVEALED_HOOK))(pMapBlock_in, i, j);
    
    if (digFlag)
    {
        auto checkDamp = get_config_bool(CONFIG_SKIP_DAMP_CANCELLATION);
        auto checkWarm = get_config_bool(CONFIG_SKIP_WARM_CANCELLATION);
        bool resetDig = false;

        // check if we need to do anything
        if (digFlag && (checkDamp || checkWarm))
        {
            auto warm = pMapBlock_in->temperature_1[i][j] >= 10075;
            // assume the tile is damp if it's not warm if the dig designation was removed
            resetDig = (warm && checkWarm) || (!warm && checkDamp);
        }

        // check if the dig flag was set then reset and if we should do something about it
        if (digFlag && resetDig && digFlag != pMapBlock_in->designation[i][j].bits.dig)
        {
            // reset the flag
            pMapBlock_in->designation[i][j].bits.dig = digFlag;
        }
    }

    return result;
}

static void install_hook(color_ostream& out)
{
    if (HookManager.IsHookInstalled(CHECK_REVEALED_HOOK) == false)
    {
        if (HookManager.BeginTransaction())
        {
            if (HookManager.InstallHook(CHECK_REVEALED_HOOK))
            {
                DEBUG(status, out).print("`%s` hook successfully installed: 0x%08X -> 0x%08X.\n", CHECK_REVEALED_HOOK, HookManager.GetTrampolineFunc(CHECK_REVEALED_HOOK), HookManager.GetHookFunc(CHECK_REVEALED_HOOK));
            }

            if (HookManager.CommitTransaction() == false)
            {
                DEBUG(status, out).printerr("Failed to commit the HookManager transaction!\n");
            }
        }
        else
        { 
            DEBUG(status, out).printerr("Failed to start the HookManager transaction!\n");
        }
    }
    else
    {
        DEBUG(status, out).print("The `%s` hook is already installed.\n", CHECK_REVEALED_HOOK);
    }
}

static void uninstall_hook(color_ostream& out)
{
    if (HookManager.IsHookInstalled(CHECK_REVEALED_HOOK))
    {
        if (HookManager.BeginTransaction())
        {
            if (HookManager.UninstallHook(CHECK_REVEALED_HOOK))
            {
                DEBUG(status, out).print("`%s` hook successfully uninstalled.\n", CHECK_REVEALED_HOOK);
            }

            if (HookManager.CommitTransaction() == false)
            {
                DEBUG(status, out).printerr("Failed to commit the HookManager transaction!\n");
            }
        }
        else
        {
            DEBUG(status, out).printerr("Failed to start the HookManager transaction!\n");
        }
    }
    else
    {
        DEBUG(status, out).print("The `%s` hook is not installed.\n", CHECK_REVEALED_HOOK);
    }
}

static command_result toggle_ignore_warm(color_ostream &out, vector<string> &parameters);
static command_result toggle_ignore_damp(color_ostream& out, vector<string>& parameters);

DFhackCExport command_result plugin_init(color_ostream &out, std::vector <PluginCommand> &commands) {
    DEBUG(status,out).print("initializing %s\n", plugin_name);
    
    // provide a configuration interface for the plugin
    commands.push_back(PluginCommand("toggle-dig-ignore-warm", "Toggle ignoring warm stone when digging", toggle_ignore_warm));
    commands.push_back(PluginCommand("toggle-dig-ignore-damp", "Toggle ignoring damp stone when digging", toggle_ignore_damp));

    if (HookManager.IsHookRegistered(CHECK_REVEALED_HOOK) == false)
    {
        HookManager.RegisterHook(CHECK_REVEALED_HOOK, SIGSCAN_GAME_PROCESSA, CHECK_REVEALED_PATTERN, CHECK_REVEALED_OFFSET, &CheckRevealedHook);
    }

    return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out, bool enable) {

    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("Cannot enable %s without a loaded world.\n", plugin_name);
        return CR_FAILURE;
    }

    if (enable != is_enabled) {
        is_enabled = enable;
        DEBUG(status,out).print("%s from the API; persisting\n",
                                is_enabled ? "enabled" : "disabled");
        set_config_bool(CONFIG_IS_ENABLED, is_enabled);
        // install or remove the hook
        enable ? install_hook(out) : uninstall_hook(out);
    } else {
        DEBUG(status,out).print("%s from the API, but already %s; no action\n",
                                is_enabled ? "enabled" : "disabled",
                                is_enabled ? "enabled" : "disabled");
    }

    if (is_enabled)
    {
        out.print(get_config_bool(CONFIG_SKIP_WARM_CANCELLATION) ? "Warm stone will be ignored.\n" : "Warm stone will cancel digging.\n");
        out.print(get_config_bool(CONFIG_SKIP_DAMP_CANCELLATION) ? "Damp stone will be ignored.\n" : "Damp stone will cancel digging.\n");
    }

    return CR_OK;
}

DFhackCExport command_result plugin_shutdown (color_ostream &out) {
    DEBUG(status,out).print("shutting down %s\n", plugin_name);
    uninstall_hook(out);

    return CR_OK;
}

DFhackCExport command_result plugin_load_data (color_ostream &out) {
    config = World::GetPersistentData(CONFIG_KEY);

    if (!config.isValid()) {
        DEBUG(status,out).print("no config found in this save; initializing\n");
        config = World::AddPersistentData(CONFIG_KEY);
        set_config_bool(CONFIG_IS_ENABLED, is_enabled);
        set_config_bool(CONFIG_SKIP_DAMP_CANCELLATION, false);
        set_config_bool(CONFIG_SKIP_WARM_CANCELLATION, false);
    }

    // we have to copy our enabled flag into the global plugin variable, but
    // all the other state we can directly read/modify from the persistent
    // data structure.
    is_enabled = get_config_bool(CONFIG_IS_ENABLED);
    DEBUG(status,out).print("loading persisted enabled state: %s\n",
                            is_enabled ? "true" : "false");
    return CR_OK;
}

DFhackCExport command_result plugin_onstatechange(color_ostream &out, state_change_event event) {
    if (event == DFHack::SC_WORLD_UNLOADED) {
        if (is_enabled) {
            DEBUG(status,out).print("world unloaded; disabling %s\n",
                                    plugin_name);
            is_enabled = false;
            uninstall_hook(out);
        }
    }
    return CR_OK;
}

static command_result toggle_ignore_warm(color_ostream &out, vector<string> &parameters) {
    // be sure to suspend the core if any DF state is read or modified
    CoreSuspender suspend;

    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("Cannot run %s without a loaded world.\n", plugin_name);
        return CR_FAILURE;
    }

    out.print(toggle_option(CONFIG_SKIP_WARM_CANCELLATION) ? "Warm stone will be ignored.\n" : "Warm stone will cancel digging.\n");
    
    return CR_OK;
}

static command_result toggle_ignore_damp(color_ostream& out, vector<string>& parameters) {
    // be sure to suspend the core if any DF state is read or modified
    CoreSuspender suspend;

    if (!Core::getInstance().isWorldLoaded()) {
        out.printerr("Cannot run %s without a loaded world.\n", plugin_name);
        return CR_FAILURE;
    }

    out.print(toggle_option(CONFIG_SKIP_DAMP_CANCELLATION) ? "Damp stone will be ignored.\n" : "Damp stone will cancel digging.\n");

    return CR_OK;
}