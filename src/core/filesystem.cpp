#include "filesystem.h"
#include "variable.h"

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

} // RGL