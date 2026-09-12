#include "filesystem.h"
#include "variable.h"

#include "container_types.h"

#include <thread>

#define DEFAULT_ROOT      "./"
#define DEFAULT_RESOURCES "./resources/"

namespace RGL
{

Variable var_resourcePath("path.resoures", DEFAULT_RESOURCES);
Variable var_rootPath("path.root", DEFAULT_ROOT);

fs::path FileSystem::rootPath()
{
	return fs::path(var_rootPath.string());
}

fs::path FileSystem::getResourcesPath()
{
	// TODO: assert that resources path is contained by root path?
	return fs::path(var_resourcePath.string());
}

bool FileSystem::directoryExists(const fs::path& path, fs::file_status status)
{
	return fs::status_known(status) ? fs::exists(status) : fs::exists(path);
}

void FileSystem::createDirectory(const fs::path& directory_name)
{
	fs::create_directories(directory_name);
}

struct MonitorEntry
{
	bool dirty;
	std::function<void()> callback;
};
static dense_map<fs::path, std::vector<MonitorEntry>> _fileMonitors;
static void monitorThreadFunc();
static std::thread monitorThread;
static bool monitorsAdded { false };

void FileSystem::monitor(const fs::path &filepath, std::function<void ()> callback)
{
	// TODO: if 'filepath' is written to (or created?)
	//   wait a bit
	//   call `callback`

	const auto start_now = _fileMonitors.empty();

	_fileMonitors[filepath].push_back({ false, std::move(callback) });
	monitorsAdded = true; // should probably use a conditional or something?

	if(start_now)
	{
		// TODO: start thread with inotify
		//   set 'dirty' flag
		monitorThread = std::thread(monitorThreadFunc);
	}
}

void FileSystem::pollMonitors()
{
	for(auto &[filepath, entries]: _fileMonitors)
	{
		for(auto &entry: entries)
		{
			if(entry.dirty)
				entry.callback();
			entry.dirty = true;
		}
	}
}

static void monitorThreadFunc()
{
	// TODO: inotify everything in `_fileMonitors`
}


} // RGL