#include "appframework/IAppSystem.h"
#include "common.h"
#include "icvar.h"
#include "interface.h"
#include "tier0/dbg.h"
#include "MinHook.h"
#include <Psapi.h>
#include "interfaces/cs2_interfaces.h"

#include "tier0/memdbgon.h"

HMODULE g_hTier0 = nullptr;
MODULEINFO g_Tier0Info;

void Message(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[1024] = {};
	V_vsnprintf(buf, sizeof(buf) - 1, msg, args);

	ConColorMsg(Color(255, 0, 255, 255), buf);

#ifdef USE_DEBUG_CONSOLE
	if (!CommandLine()->HasParm("-dedicated"))
		printf(buf);
#endif

	va_end(args);
}

void Panic(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[1024] = {};
	V_vsnprintf(buf, sizeof(buf) - 1, msg, args);

	if (CommandLine()->HasParm("-dedicated"))
		Warning(buf);
	else
		MessageBoxA(nullptr, buf, "Warning", 0);

	va_end(args);
}

CON_COMMAND_F(unlock_cvars, "Unlock all cvars", FCVAR_NONE)
{
	UnlockConVars();
}

CON_COMMAND_F(unlock_commands, "Unlock all commands", FCVAR_NONE)
{
	UnlockConCommands();
}

CON_COMMAND_F(toggle_logs, "Toggle printing most logs and warnings", FCVAR_NONE)
{
	ToggleLogs();
}

CON_COMMAND_F(set_max_players, "Set max players through a patch", FCVAR_NONE)
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: set_max_players <maxplayers>\n");
		return;
	}

	SetMaxPlayers(atoi(args.Arg(1)));
}

void Init()
{
	static bool attached = false;

	if (attached)
		return;
	
	attached = true;

#ifdef USE_DEBUG_CONSOLE
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
#endif

	HANDLE hProcess = GetCurrentProcess();

	// Find tier0
	g_hTier0 = LoadLibraryA("tier0");
	if (!g_hTier0)
	{
		Plat_FatalErrorFunc("Failed to get tier0.dll's address");
		return;
	}

	GetModuleInformation(GetCurrentProcess(), g_hTier0, &g_Tier0Info, sizeof(g_Tier0Info));

	MH_Initialize();

#ifdef HOOK_CONVARS
	HookConVars();
#endif
#ifdef HOOK_CONCOMMANDS
	HookConCommands();
#endif
	addresses::Initialize();
	interfaces::Initialize();

	g_pCVar->RegisterConCommand(&unlock_cvars_command);
	g_pCVar->RegisterConCommand(&unlock_commands_command);
	g_pCVar->RegisterConCommand(&toggle_logs_command);
	g_pCVar->RegisterConCommand(&set_max_players_command);

	InitPatches();
	InitLoggingDetours();
}

#ifdef USE_TICKRATE
void *g_pfnGetTickInterval = nullptr;

float GetTickIntervalHook()
{
	return 1.0f / CommandLine()->ParmValue("-tickrate", 64.0f);
}
#endif

DLL_EXPORT void *CreateInterface(const char *pName, int *pReturnCode)
{
	Init();

#ifdef USE_DEBUG_CONSOLE
	printf("CreateInterface: %s %i\n", pName, *pReturnCode);
#endif

	static CreateInterfaceFn pfnCreateInterface;

	if (!pfnCreateInterface)
	{
		char szServerDLLPath[MAX_PATH];
		V_strncpy(szServerDLLPath, Plat_GetGameDirectory(), MAX_PATH);
		V_strcat(szServerDLLPath, GAMEBIN "server.dll", MAX_PATH);

		pfnCreateInterface = (CreateInterfaceFn)Plat_GetProcAddress(szServerDLLPath, "CreateInterface");
	}

	void *pInterface = pfnCreateInterface(pName, pReturnCode);

#ifdef USE_TICKRATE
	if (!V_stricmp(pName, SOURCE2SERVERCONFIG_INTERFACE_VERSION))
	{
		void** vtable = *(void***)pInterface;

		MH_CreateHook(vtable[13], GetTickIntervalHook, &g_pfnGetTickInterval);
		MH_EnableHook(vtable[13]);
	}
#endif

	return pInterface;
}
