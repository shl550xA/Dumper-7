#include "Platform.h"

#include <iostream>
#include <chrono>
#include <fstream>

#include "Generators/CppGenerator.h"
#include "Generators/MappingGenerator.h"
#include "Generators/IDAMappingGenerator.h"
#include "Generators/DumpspaceGenerator.h"

#include "Generators/Generator.h"

#if defined(PLATFORM_WINDOWS)
#include <Windows.h>

enum class EFortToastType : uint8
{
        Default                        = 0,
        Subdued                        = 1,
        Impactful                      = 2,
        EFortToastType_MAX             = 3,
};
#else
#include <thread>

#if defined(PLATFORM_ANDROID)
#include <android/log.h>
#include <sstream>

extern "C" const char* __progname;

// Redirect std::cerr to Android logcat. Native stderr goes to /dev/null in
// app processes; this streambuf routes writes through __android_log_print so
// every existing std::cerr << "..." shows up in 'adb logcat -s Dumper-7'.
class AndroidLogBuf : public std::streambuf
{
protected:
	int overflow(int c) override
	{
		if (c == '\n' || c == EOF)
		{
			if (!Buffer.empty())
			{
				__android_log_print(ANDROID_LOG_INFO, "Dumper-7", "%s", Buffer.c_str());
				Buffer.clear();
			}
		}
		else
		{
			Buffer += static_cast<char>(c);
		}
		return c;
	}

	std::streamsize xsputn(const char* s, std::streamsize n) override
	{
		for (std::streamsize i = 0; i < n; ++i)
			overflow(s[i]);
		return n;
	}

private:
	std::string Buffer;
};

static AndroidLogBuf g_LogBuf;
#endif // PLATFORM_ANDROID
#endif // PLATFORM_WINDOWS

static void RunDump()
{
#if defined(PLATFORM_ANDROID)
	std::cerr.rdbuf(&g_LogBuf);

	// Build writable SDK path from the host app's package name.
	// __progname is the process name, which on Android is the package name.
	{
		std::string DumperDir = std::string("/data/data/") + __progname + "/Dumper-7";
		Settings::Generator::SDKGenerationPath = strdup(DumperDir.c_str());
	}
#endif

	std::cerr << "Started Generation [Dumper-7]!\n";

	Settings::Config::Load();

	if (Settings::Config::SleepTimeout > 0)
	{
		std::cerr << "Sleeping for " << Settings::Config::SleepTimeout << "ms...\n";
		Sleep(Settings::Config::SleepTimeout);
	}

	auto DumpStartTime = std::chrono::high_resolution_clock::now();

	Generator::InitEngineCore();
	Generator::InitInternal();

	if (Settings::Generator::GameName.empty() && Settings::Generator::GameVersion.empty())
	{
		// Only Possible in Main()
		FString Name;
		FString Version;
		UEClass Kismet = ObjectArray::FindClassFast("KismetSystemLibrary");
		UEFunction GetGameName = Kismet.GetFunction("KismetSystemLibrary", "GetGameName");
		UEFunction GetEngineVersion = Kismet.GetFunction("KismetSystemLibrary", "GetEngineVersion");

		Kismet.ProcessEvent(GetGameName, &Name);
		Kismet.ProcessEvent(GetEngineVersion, &Version);

		Settings::Generator::GameName = Name.ToString();
		Settings::Generator::GameVersion = Version.ToString();
	}

	std::cerr << "GameName: " << Settings::Generator::GameName << "\n";
	std::cerr << "GameVersion: " << Settings::Generator::GameVersion << "\n\n";

	std::cerr << "FolderName: " << (Settings::Generator::GameVersion + '-' + Settings::Generator::GameName) << "\n\n";

	Generator::Generate<CppGenerator>();
	Generator::Generate<MappingGenerator>();
	Generator::Generate<IDAMappingGenerator>();
	Generator::Generate<DumpspaceGenerator>();

	auto DumpFinishTime = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> DumpTime = DumpFinishTime - DumpStartTime;

	std::cerr << "\n\nGenerating SDK took (" << DumpTime.count() << "ms)\n\n\n";
}

#if defined(PLATFORM_WINDOWS)

DWORD MainThread(HMODULE Module)
{
	AllocConsole();
	FILE* Dummy;
	freopen_s(&Dummy, "CONOUT$", "w", stderr);
	freopen_s(&Dummy, "CONIN$", "r", stdin);

	RunDump();

	while (true)
	{
		if (GetAsyncKeyState(VK_F6) & 1)
		{
			fclose(stderr);
			if (Dummy) fclose(Dummy);
			FreeConsole();

			FreeLibraryAndExitThread(Module, 0);
		}

		Sleep(100);
	}

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0);
		break;
	}

	return TRUE;
}

#else // PLATFORM_WINDOWS

/*
 * Android entry points.
 *
 *   Dumper7_Run   - exported C symbol; call via dlsym after injection to
 *                   trigger dumping synchronously on the caller's thread.
 *                   Ideal if the caller already blocks until the game has
 *                   finished engine init.
 *
 *   __attribute__((constructor)) Dumper7_OnLoad - runs at dlopen time, before
 *                   the game has necessarily set up UE reflection. Spawns a
 *                   detached thread that waits for Settings::Config::SleepTimeout
 *                   milliseconds (configurable via /data/local/tmp/Dumper-7/Dumper-7.ini
 *                   before injection) and then runs the dump. Default is 0ms;
 *                   most callers will want to set a sleep or call Dumper7_Run
 *                   explicitly.
 */
extern "C" __attribute__((visibility("default"))) void Dumper7_Run()
{
	RunDump();
}

__attribute__((constructor)) static void Dumper7_OnLoad()
{
	std::thread([]() { RunDump(); }).detach();
}

#endif // PLATFORM_WINDOWS
