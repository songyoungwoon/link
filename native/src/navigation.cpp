#include "link/navigation.hpp"
#include "link/loader.hpp"
namespace linkworld {
Navigator::Navigator(std::filesystem::path root,LogFn log,Loader loader):loader_(std::move(loader)),log_(std::move(log)) {
  if(!loader_) loader_=[root=std::move(root)](const std::string& address,std::stop_token stop){return loadWorld(address,root,stop);};
  worker_=std::jthread([this]{work();});
}
Navigator::~Navigator() {
  stop();
  {std::lock_guard lock(mutex_);quitting_=true;cancellation_.request_stop();}
  condition_.notify_one();
  if(worker_.joinable())worker_.join();
}
void Navigator::request(std::string address) {
  {std::lock_guard lock(mutex_);++generation_;cancellation_.request_stop();next_=Job{generation_,address};completion_.reset();}
  loadingAddress_=std::move(address);error_.clear();loading_=true;
  condition_.notify_one();
}
void Navigator::cancel() {
  {std::lock_guard lock(mutex_);++generation_;cancellation_.request_stop();next_.reset();completion_.reset();}
  loading_=false;error_.clear();
}
void Navigator::stop(){cancel();active_.reset();}
void Navigator::work() {
  for(;;) {
    Job job;std::stop_token token;
    {std::unique_lock lock(mutex_);condition_.wait(lock,[this]{return quitting_||next_.has_value();});
      if(quitting_)return;
      job=std::move(*next_);next_.reset();cancellation_=std::stop_source{};token=cancellation_.get_token();
    }
    Completion result{job.generation,{},{}};
    try {result.world=loader_(job.address,token);if(!result.world)throw std::runtime_error("Loader returned no world");}
    catch(const std::exception& error){result.error=error.what();}
    catch(...){result.error="Unknown loader failure";}
    {std::lock_guard lock(mutex_);if(!quitting_&&job.generation==generation_&&!token.stop_requested())completion_=std::move(result);}
  }
}
bool Navigator::poll() {
  std::optional<Completion> result;
  {std::lock_guard lock(mutex_);if(!completion_)return false;result=std::move(completion_);completion_.reset();if(result->generation!=generation_)return false;}
  loading_=false;
  if(!result->error.empty()) {error_=std::move(result->error);if(log_)log_(true,error_);return true;}
  active_.reset();
  try {active_=std::make_unique<Runtime>(std::move(result->world),log_);active_->start();error_.clear();}
  catch(const std::exception& error){active_.reset();error_=std::string("Activation failed: ")+error.what();if(log_)log_(true,error_);}
  return true;
}
} // namespace linkworld
