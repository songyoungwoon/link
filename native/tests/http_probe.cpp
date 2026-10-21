#include "link/remote.hpp"
#include "link/core.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stop_token>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
namespace {
std::string utf8(const wchar_t* value){
  const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,nullptr,0,nullptr,nullptr);
  if(size<=0)throw std::runtime_error("Invalid Unicode argument");
  std::string result(static_cast<std::size_t>(size),'\0');
  WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,result.data(),size,nullptr,nullptr);
  result.pop_back();return result;
}
}
int wmain(int argc,wchar_t** argv){
  if(argc<4||argc>5){std::cerr<<"Usage: link_http_probe LINK relative-resource maximum-bytes [cancel-after-ms]\n";return 1;}
  const auto started=std::chrono::steady_clock::now();
  try{
    const auto endpoint=linkworld::parseRemoteLink(utf8(argv[1]));
    const auto resource=utf8(argv[2]);const auto limit=std::stoull(utf8(argv[3]));
    std::stop_source stop;std::jthread timer;
    if(argc==5){const auto delay=std::stoul(utf8(argv[4]));timer=std::jthread([&stop,delay]{std::this_thread::sleep_for(std::chrono::milliseconds(delay));stop.request_stop();});}
    const auto body=linkworld::fetchRemoteResource(endpoint,resource,limit,stop.get_token());
    std::cout<<"bytes="<<body.size()<<"\nhex=";
    for(std::size_t i=0;i<std::min<std::size_t>(32,body.size());++i)std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(static_cast<unsigned char>(body[i]));
    std::cout<<"\n";return 0;
  }catch(const std::exception& error){
    std::cerr<<error.what()<<"\nelapsedMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()<<'\n';return 2;
  }
}