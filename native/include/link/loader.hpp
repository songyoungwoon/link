#pragma once
#include "core.hpp"
#include <stop_token>
namespace linkworld {
std::filesystem::path resolveLink(const std::string& address,const std::filesystem::path& worldsRoot);
std::filesystem::path resolveResource(const std::filesystem::path& directory,const std::string& relative);
// Resolve an absolute LINK portal or a /world target on the current authority.
std::string resolvePortalAddress(const std::string& address,const std::string& currentAddress,const std::filesystem::path& worldsRoot);
std::shared_ptr<const World> loadWorld(const std::string& address,const std::filesystem::path& worldsRoot,std::stop_token stop={});
// Validate a local candidate without touching its source or other resources.
std::shared_ptr<const World> loadWorldElementOverride(const std::string& address,const std::filesystem::path& worldsRoot,const std::string& elements,std::stop_token stop={});
} // namespace linkworld
