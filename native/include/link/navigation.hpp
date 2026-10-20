#pragma once
#include "runtime.hpp"
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
namespace linkworld {
class Navigator {
public:
  using Loader=std::function<std::shared_ptr<const World>(const std::string&,std::stop_token)>;
  explicit Navigator(std::filesystem::path worldsRoot,LogFn log={},Loader loader={});
  ~Navigator();
  void request(std::string address);
  bool poll();
  void cancel(); void stop();
  Runtime* current() const {return active_.get();}
  bool loading() const {return loading_;}
  const std::string& loadingAddress() const {return loadingAddress_;}
  const std::string& error() const {return error_;}
private:
  struct Job {std::size_t generation;std::string address;};
  struct Completion {std::size_t generation;std::shared_ptr<const World> world;std::string error;};
  Loader loader_; LogFn log_; std::unique_ptr<Runtime> active_;
  std::mutex mutex_; std::condition_variable condition_;
  std::optional<Job> next_; std::optional<Completion> completion_; std::stop_source cancellation_;
  std::size_t generation_=0; bool quitting_=false,loading_=false;
  std::string loadingAddress_,error_; std::jthread worker_;
  void work();
};
} // namespace linkworld
