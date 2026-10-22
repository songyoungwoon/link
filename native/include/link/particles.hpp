#pragma once
#include "link/core.hpp"
#include <cstdint>
#include <memory>

namespace linkworld {
struct Particle {
  std::uint32_t id=0;
  Vec3 position{0,0,0},velocity{0,0,0};
  double radius=.04,age=0;
  std::string color="#c4d9d0";
  Value::Map state;
};
Value particleValue(const Particle&);

// Local-space, deterministic simulation. Reads remain frozen for an entire
// phase; writes are staged, then committed together before rendering.
class ParticleEngine {
public:
  explicit ParticleEngine(const World&);
  ~ParticleEngine();
  ParticleEngine(const ParticleEngine&)=delete;
  ParticleEngine& operator=(const ParticleEngine&)=delete;
  void beginStep();
  void endStep(double dt);
  void abortStep() noexcept;
  std::optional<std::uint32_t> spawn(const std::string& system,const Value::Map& specification);
  void remove(const std::string& system,std::uint32_t id);
  void set(const std::string& system,std::uint32_t id,const Value::Map& patch);
  std::optional<Particle> get(const std::string& system,std::uint32_t id) const;
  std::vector<std::uint32_t> ids(const std::string& system) const;
  std::vector<std::uint32_t> neighbors(const std::string& system,std::uint32_t id,double radius,std::size_t limit);
  std::size_t count(const std::string& system) const;
  const std::map<std::uint32_t,Particle>& live(std::size_t nodeIndex) const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace linkworld