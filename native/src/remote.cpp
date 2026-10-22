#include "link/remote.hpp"
#include "link/core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace linkworld {
namespace {
[[noreturn]] void fail(const std::string& label,const std::string& message) {
  throw Error({label,1,1},message);
}
bool asciiAlnum(unsigned char c) {
  return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');
}
int hexDigit(char c) {
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return -1;
}
bool ipv4(std::string_view host) {
  std::size_t start=0;
  for(int part=0;part<4;++part) {
    const auto dot=host.find('.',start);
    const auto end=dot==std::string_view::npos?host.size():dot;
    if(end==start||end-start>3||(end-start>1&&host[start]=='0'))return false;
    unsigned value=0;
    for(auto i=start;i<end;++i) {
      if(host[i]<'0'||host[i]>'9')return false;
      value=value*10+static_cast<unsigned>(host[i]-'0');
    }
    if(value>255||(part<3&&dot==std::string_view::npos)||(part==3&&dot!=std::string_view::npos))return false;
    start=end+1;
  }
  return true;
}
bool validUtf8(std::string_view text) {
  for(std::size_t i=0;i<text.size();) {
    const auto first=static_cast<unsigned char>(text[i++]);
    if(first<0x80)continue;
    unsigned count=0,code=0,minimum=0;
    if(first>=0xC2&&first<=0xDF){count=1;code=first&0x1F;minimum=0x80;}
    else if(first>=0xE0&&first<=0xEF){count=2;code=first&0x0F;minimum=0x800;}
    else if(first>=0xF0&&first<=0xF4){count=3;code=first&0x07;minimum=0x10000;}
    else return false;
    if(count>text.size()-i)return false;
    while(count--) {
      const auto next=static_cast<unsigned char>(text[i++]);
      if((next&0xC0)!=0x80)return false;
      code=(code<<6)|(next&0x3F);
    }
    if(code<minimum||code>0x10FFFF||(code>=0xD800&&code<=0xDFFF))return false;
  }
  return true;
}
std::string resourcePath(const std::string& decoded,const std::string& label) {
  if(decoded.empty()||decoded.size()>2048)fail(label,"empty or oversized remote resource path");
  if(decoded.front()=='/'||decoded.back()=='/')fail(label,"remote resource must be a relative file path");
  for(const unsigned char c:decoded)
    if(c<32||c==127||c=='\\'||c==':'||c=='?'||c=='#'||c=='%'||c=='@')
      fail(label,"forbidden character in decoded remote resource path");
  if(!validUtf8(decoded))fail(label,"invalid UTF-8 remote resource path");
  std::string normalized;
  for(std::size_t start=0;start<decoded.size();) {
    const auto slash=decoded.find('/',start);
    const auto end=slash==std::string::npos?decoded.size():slash;
    const auto part=std::string_view(decoded).substr(start,end-start);
    if(part=="..")fail(label,"remote resource parent traversal is forbidden");
    if(!part.empty()&&part!=".") {
      if(!normalized.empty())normalized+='/';
      normalized.append(part);
    }
    start=end+1;
  }
  if(normalized.empty())fail(label,"remote resource must name a file");
  return normalized;
}
void cancelled(std::stop_token stop,const std::string& label) {
  if(stop.stop_requested())fail(label,"remote loading cancelled");
}

using Clock=std::chrono::steady_clock;
constexpr auto transferTimeout=std::chrono::seconds(20);
constexpr std::uint8_t hello=1,welcome=2,get=3,resource=4,errorMessage=5,goodbye=6;
constexpr std::size_t headerBytes=16,maxErrorBytes=1026,maxRequests=256;
std::uint32_t readU32(const char* bytes) {
  std::uint32_t result=0;
  for(unsigned i=0;i<4;++i)result=(result<<8)|static_cast<unsigned char>(bytes[i]);
  return result;
}
void writeU32(char* bytes,std::uint32_t value) {
  for(int i=3;i>=0;--i){bytes[i]=static_cast<char>(value&255);value>>=8;}
}
std::array<char,headerBytes> frameHeader(std::uint8_t type,std::uint32_t id,std::uint32_t bytes) {
  std::array<char,headerBytes> result{'L','I','N','K',0,1,static_cast<char>(type),0};
  writeU32(result.data()+8,id);writeU32(result.data()+12,bytes);
  return result;
}
void checkDeadline(Clock::time_point deadline,std::stop_token stop,const std::string& label) {
  cancelled(stop,label);
  if(Clock::now()>=deadline)fail(label,"remote LINK transfer deadline exceeded (20 seconds)");
}
const char* errorName(unsigned code) {
  switch(code) {
    case 1:return "BAD_REQUEST";case 2:return "NOT_FOUND";case 3:return "FORBIDDEN";
    case 4:return "TOO_LARGE";case 5:return "VERSION";case 6:return "BUSY";
    case 7:return "INTERNAL";case 8:return "LIMIT";default:return nullptr;
  }
}
RemoteLink checkedRemote(RemoteLink remote) {
  const auto link="link://"+remote.authority()+"/"+remote.world;
  const auto checked=parseRemoteLink(link);
  if(checked.world!=remote.world)fail(link,"remote world must already be decoded");
  return remote;
}
} // namespace
std::string RemoteLink::authority() const {return host+":"+std::to_string(port);}
RemoteLink parseRemoteLink(const std::string& address) {
  if(address.size()>2048||!address.starts_with("link://"))
    fail(address,"expected link://<IPv4>[:port]/<world>");
  const auto slash=address.find('/',7);
  if(slash==std::string::npos)fail(address,"remote LINK requires one world segment");
  const auto authority=std::string_view(address).substr(7,slash-7);
  const auto colon=authority.find(':');
  const auto host=authority.substr(0,colon);
  if(!ipv4(host))fail(address,"remote LINK authority must be a strict dotted-decimal IPv4 address");
  RemoteLink result;result.host=host;
  if(colon!=std::string_view::npos) {
    const auto port=authority.substr(colon+1);
    if(port.empty()||port.size()>5)fail(address,"remote LINK port must be between 1 and 65535");
    unsigned value=0;
    for(const auto c:port) {
      if(c<'0'||c>'9')fail(address,"remote LINK port must be between 1 and 65535");
      value=value*10+static_cast<unsigned>(c-'0');
    }
    if(!value||value>65535)fail(address,"remote LINK port must be between 1 and 65535");
    result.port=static_cast<std::uint16_t>(value);
  }
  for(std::size_t i=slash+1;i<address.size();++i) {
    unsigned char c=address[i];
    if(c=='%') {
      if(i+2>=address.size()||hexDigit(address[i+1])<0||hexDigit(address[i+2])<0)
        fail(address,"malformed percent encoding in remote LINK world");
      c=static_cast<unsigned char>(hexDigit(address[i+1])*16+hexDigit(address[i+2]));i+=2;
    }
    if(!asciiAlnum(c)&&c!='-')fail(address,"remote LINK world must contain only letters, digits or hyphens");
    result.world+=static_cast<char>(c);
  }
  if(result.world.empty())fail(address,"remote LINK requires one world segment");
  return result;
}

struct RemoteSession::Impl {
  RemoteLink remote;
  std::string address;
  std::uint32_t nextId=1;
  std::size_t transferred=0;
  bool ready=false,closed=false;
#ifdef _WIN32
  SOCKET socket=INVALID_SOCKET;
  bool winsock=false;
#endif
  explicit Impl(RemoteLink endpoint):remote(checkedRemote(std::move(endpoint))),
    address("link://"+remote.authority()+"/"+remote.world){}
  ~Impl(){close();}
  void close(bool graceful=true) noexcept {
#ifdef _WIN32
    if(socket!=INVALID_SOCKET) {
      // Destruction must never wait on the network. EOF is also a valid close.
      if(graceful&&ready) {
        const auto header=frameHeader(goodbye,nextId,0);
        (void)::send(socket,header.data(),static_cast<int>(header.size()),0);
      }
      (void)::shutdown(socket,SD_SEND);
      (void)::closesocket(socket);socket=INVALID_SOCKET;
    }
    if(winsock){WSACleanup();winsock=false;}
#else
    (void)graceful;
#endif
    ready=false;closed=true;
  }
#ifdef _WIN32
  [[noreturn]] void socketFailure(const std::string& label,const char* operation,int code) {
    fail(label,std::string("remote LINK TCP ")+operation+" failed (Winsock "+std::to_string(code)+")");
  }
  void waitReady(bool writing,Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    for(;;) {
      checkDeadline(deadline,stop,label);
      fd_set reads,writes,errors;
      FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);
      if(writing)FD_SET(socket,&writes);else FD_SET(socket,&reads);
      FD_SET(socket,&errors);
      const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline-Clock::now()).count();
      timeval timeout{};timeout.tv_usec=static_cast<long>(std::clamp<std::int64_t>(remaining,1,50000));
      const int count=::select(0,&reads,&writes,&errors,&timeout);
      if(count==SOCKET_ERROR)socketFailure(label,"wait",WSAGetLastError());
      checkDeadline(deadline,stop,label);
      if(FD_ISSET(socket,&errors)) {
        int code=0,length=sizeof(code);
        if(::getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&code),&length)==SOCKET_ERROR)
          code=WSAGetLastError();
        socketFailure(label,"connection",code?code:WSAECONNABORTED);
      }
      if(count)return;
    }
  }
  void sendAll(std::string_view bytes,Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    std::size_t offset=0;
    while(offset<bytes.size()) {
      checkDeadline(deadline,stop,label);
      const auto count=::send(socket,bytes.data()+offset,static_cast<int>(std::min<std::size_t>(bytes.size()-offset,16384)),0);
      if(count==SOCKET_ERROR) {
        const auto code=WSAGetLastError();
        if(code==WSAEWOULDBLOCK){waitReady(true,deadline,stop,label);continue;}
        socketFailure(label,"send",code);
      }
      if(!count)fail(label,"remote LINK connection closed during send");
      offset+=static_cast<std::size_t>(count);
    }
  }
  void receiveAll(char* bytes,std::size_t size,Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    std::size_t offset=0;
    while(offset<size) {
      checkDeadline(deadline,stop,label);
      const auto count=::recv(socket,bytes+offset,static_cast<int>(std::min<std::size_t>(size-offset,16384)),0);
      if(count==SOCKET_ERROR) {
        const auto code=WSAGetLastError();
        if(code==WSAEWOULDBLOCK){waitReady(false,deadline,stop,label);continue;}
        socketFailure(label,"receive",code);
      }
      if(!count)fail(label,"truncated remote LINK frame (connection closed before expected bytes)");
      offset+=static_cast<std::size_t>(count);
    }
  }
  void sendFrame(std::uint8_t type,std::uint32_t id,std::string_view payload,
                 Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    const auto header=frameHeader(type,id,static_cast<std::uint32_t>(payload.size()));
    sendAll(std::string_view(header.data(),header.size()),deadline,stop,label);
    sendAll(payload,deadline,stop,label);
  }
  std::string receiveFrame(std::uint8_t expected,std::uint32_t id,std::size_t cap,
                           Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    std::array<char,headerBytes> header{};
    receiveAll(header.data(),header.size(),deadline,stop,label);
    if(std::string_view(header.data(),4)!="LINK")
      fail(label,"expected LINK protocol magic; update the server (HTTP fallback is disabled)");
    if(header[4]!=0||header[5]!=1)fail(label,"unsupported remote LINK wire version (expected 0.1)");
    if(header[7]!=0)fail(label,"unsupported remote LINK frame flags");
    if(readU32(header.data()+8)!=id)fail(label,"remote LINK response request id does not match");
    const auto type=static_cast<std::uint8_t>(header[6]);
    const auto length=static_cast<std::size_t>(readU32(header.data()+12));
    if(type!=expected&&type!=errorMessage)fail(label,"unexpected remote LINK response type");
    if(type==errorMessage) {
      if(length<2||length>maxErrorBytes)fail(label,"invalid remote LINK ERROR payload length");
    }else if(length>cap) {
      fail(label,"remote resource byte budget exceeded (maximum "+std::to_string(cap)+")");
    }
    if(type==welcome&&length<3)fail(label,"invalid remote LINK WELCOME payload length");
    // Length is validated before allocating or reading any payload.
    std::string body(length,'\0');
    receiveAll(body.data(),body.size(),deadline,stop,label);
    checkDeadline(deadline,stop,label);
    if(type==errorMessage) {
      const auto code=(static_cast<unsigned>(static_cast<unsigned char>(body[0]))<<8)|
                       static_cast<unsigned char>(body[1]);
      const auto* name=errorName(code);
      const std::string_view message(body.data()+2,body.size()-2);
      if(!name||!validUtf8(message)||std::any_of(message.begin(),message.end(),[](unsigned char c){return c<32||c==127;}))
        fail(label,"invalid remote LINK ERROR code or UTF-8 message");
      fail(label,std::string("remote LINK error ")+name+" ("+std::to_string(code)+"): "+std::string(message));
    }
    return body;
  }
  void connect(Clock::time_point deadline,std::stop_token stop,const std::string& label) {
    checkDeadline(deadline,stop,label);
    WSADATA data{};
    const auto started=WSAStartup(MAKEWORD(2,2),&data);
    if(started)socketFailure(label,"startup",started);
    winsock=true;
    socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(socket==INVALID_SOCKET)socketFailure(label,"socket",WSAGetLastError());
    u_long nonblocking=1;
    if(ioctlsocket(socket,FIONBIO,&nonblocking)==SOCKET_ERROR)socketFailure(label,"nonblocking configuration",WSAGetLastError());
    const int noDelay=1;
    if(setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&noDelay),sizeof(noDelay))==SOCKET_ERROR)
      socketFailure(label,"TCP_NODELAY",WSAGetLastError());
    sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(remote.port);
    if(InetPtonA(AF_INET,remote.host.c_str(),&endpoint.sin_addr)!=1)fail(label,"invalid LINK IPv4 address");
    if(::connect(socket,reinterpret_cast<const sockaddr*>(&endpoint),sizeof(endpoint))==SOCKET_ERROR) {
      const auto code=WSAGetLastError();
      if(code!=WSAEWOULDBLOCK&&code!=WSAEINPROGRESS)socketFailure(label,"connect",code);
      waitReady(true,deadline,stop,label);
      int connected=0,length=sizeof(connected);
      if(getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&connected),&length)==SOCKET_ERROR)
        socketFailure(label,"connect status",WSAGetLastError());
      if(connected)socketFailure(label,"connect",connected);
    }
    std::string greeting("\0\1",2);greeting+=remote.world;
    sendFrame(hello,0,greeting,deadline,stop,label);
    if(receiveFrame(welcome,0,greeting.size(),deadline,stop,label)!=greeting)
      fail(label,"remote LINK WELCOME does not match the requested world and World Standards version");
    ready=true;
  }
  std::string fetch(const std::string& path,std::size_t cap,std::stop_token stop,const std::string& label) {
    if(closed)fail(label,"remote LINK session is closed");
    const auto deadline=Clock::now()+transferTimeout;
    if(!ready)connect(deadline,stop,label);
    if(nextId>maxRequests)fail(label,"remote LINK request count budget exceeded (maximum 256)");
    if(transferred>=Limits::remoteWorldBytes)fail(label,"world transfer byte budget exceeded");
    cap=std::min({cap,Limits::imageBytes,Limits::remoteWorldBytes-transferred});
    std::string request(4,'\0');writeU32(request.data(),static_cast<std::uint32_t>(cap));request+=path;
    sendFrame(get,nextId,request,deadline,stop,label);
    auto body=receiveFrame(resource,nextId,cap,deadline,stop,label);
    ++nextId;transferred+=body.size();
    return body;
  }
#endif
};

RemoteSession::RemoteSession(RemoteLink remote):impl_(std::make_unique<Impl>(std::move(remote))){}
RemoteSession::~RemoteSession()=default;
void RemoteSession::close() noexcept {impl_->close();}
std::string RemoteSession::fetch(const std::string& decodedRelative,std::size_t maxBytes,std::stop_token stop) {
  const auto label=impl_->address+"/"+decodedRelative;
  try {
    const auto path=resourcePath(decodedRelative,label);
    cancelled(stop,label);
    if(!maxBytes)fail(label,"remote resource byte budget must be positive");
#ifdef _WIN32
    return impl_->fetch(path,maxBytes,stop,label);
#else
    (void)path;(void)maxBytes;
    fail(label,"remote LINK TCP transport is supported only on Windows");
#endif
  }catch(...){impl_->close(false);throw;}
}
std::string fetchRemoteResource(const RemoteLink& remote,const std::string& decodedRelative,
                               std::size_t maxBytes,std::stop_token stop) {
  RemoteSession session(remote);
  return session.fetch(decodedRelative,maxBytes,stop);
}
} // namespace linkworld