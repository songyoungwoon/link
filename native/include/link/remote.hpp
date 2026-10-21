#pragma once
#include <cstddef>
#include <cstdint>
#include <stop_token>
#include <string>

namespace linkworld {
// Initial LAN transport: numeric IPv4, plain HTTP, no DNS or authentication.
struct RemoteLink {
  std::string host,world;
  std::uint16_t port=8000;
  std::string authority() const;
};
RemoteLink parseRemoteLink(const std::string& address);
// The resource is already percent-decoded once by the document loader.
// Returns the exact response bytes, including any embedded NUL bytes.
std::string fetchRemoteResource(const RemoteLink&,const std::string& decodedRelative,
                               std::size_t maxBytes,std::stop_token stop={});
} // namespace linkworld