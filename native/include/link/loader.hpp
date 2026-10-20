#pragma once
#include "core.hpp"
#include <stop_token>
namespace linkworld {
std::filesystem::path resolveLink(const std::string& address,const std::filesystem::path& worldsRoot);
std::filesystem::path resolveResource(const std::filesystem::path& directory,const std::string& relative);
std::shared_ptr<const World> loadWorld(const std::string& address,const std::filesystem::path& worldsRoot,std::stop_token stop={});
} // namespace linkworld
