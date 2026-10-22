#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

namespace linkworld {
// LINK TCP v0.1: numeric IPv4, a framed persistent stream; no HTTP or TLS.
struct RemoteLink {
  std::string host,world;
  std::uint16_t port=8000;
  std::string authority() const;
};
RemoteLink parseRemoteLink(const std::string& address);
// One sequential connection per world load. Connects lazily after validating the
// first resource, observes cancellation, and closes immediately on any failure.
// Not thread safe: a loader owns its session exclusively.
class RemoteSession {
public:
  explicit RemoteSession(RemoteLink);
  ~RemoteSession();
  RemoteSession(const RemoteSession&)=delete;
  RemoteSession& operator=(const RemoteSession&)=delete;
  std::string fetch(const std::string& decodedRelative,std::size_t maxBytes,std::stop_token stop={});
  void close() noexcept;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Convenience for a single resource. Exact bytes, including embedded NULs.
// The resource is already percent-decoded once by the document loader.
std::string fetchRemoteResource(const RemoteLink&,const std::string& decodedRelative,
                               std::size_t maxBytes,std::stop_token stop={});
} // namespace linkworld