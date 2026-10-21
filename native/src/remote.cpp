#include "link/remote.hpp"
#include "link/core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
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
  static constexpr char digits[]="0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(normalized.size());
  for(const unsigned char c:normalized) {
    if(asciiAlnum(c)||c=='-'||c=='_'||c=='.'||c=='~'||c=='/')encoded+=static_cast<char>(c);
    else {encoded+='%';encoded+=digits[c>>4];encoded+=digits[c&15];}
  }
  return encoded;
}
void cancelled(std::stop_token stop,const std::string& label) {
  if(stop.stop_requested())fail(label,"remote loading cancelled");
}

#ifdef _WIN32
using Clock=std::chrono::steady_clock;
constexpr auto transferTimeout=std::chrono::seconds(20);
constexpr int operationTimeoutMs=3000;
struct HttpHandle {
  HINTERNET value=nullptr;
  explicit HttpHandle(HINTERNET handle):value(handle){}
  ~HttpHandle(){if(value)WinHttpCloseHandle(value);}
  HttpHandle(const HttpHandle&)=delete;
  HttpHandle& operator=(const HttpHandle&)=delete;
};
[[noreturn]] void httpFailure(const std::string& label,const char* operation,DWORD error) {
  fail(label,std::string("remote HTTP ")+operation+" failed (WinHTTP "+std::to_string(error)+")");
}
struct Transfer {
  std::mutex mutex;
  std::condition_variable changed;
  DWORD completion=0,error=0,bytes=0;
  // WinHTTP can still write after cancellation until HANDLE_CLOSING.
  std::array<char,16384> buffer{};
};
using Context=std::shared_ptr<Transfer>;
void CALLBACK statusCallback(HINTERNET,DWORD_PTR context,DWORD status,void* information,DWORD length) noexcept {
  if(!context)return;
  auto* binding=reinterpret_cast<Context*>(context);
  // HANDLE_CLOSING is the last notification for this handle. The binding owns
  // the buffer throughout asynchronous teardown, even if the caller has left.
  if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){delete binding;return;}
  const auto state=*binding;
  {
    std::lock_guard lock(state->mutex);
    if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
      state->error=information&&length>=sizeof(WINHTTP_ASYNC_RESULT)
        ?static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError:ERROR_WINHTTP_INTERNAL_ERROR;
      state->completion=status;
    } else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||
              status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE||
              status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
      state->bytes=status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE?length:0;
      state->completion=status;
    } else return;
  }
  state->changed.notify_one();
}
void checkDeadline(Clock::time_point deadline,std::stop_token stop,const std::string& label) {
  cancelled(stop,label);
  if(Clock::now()>=deadline)fail(label,"remote HTTP transfer deadline exceeded (20 seconds)");
}
void prepare(const Context& state,Clock::time_point deadline,std::stop_token stop,const std::string& label) {
  checkDeadline(deadline,stop,label);
  std::lock_guard lock(state->mutex);
  state->completion=0;state->error=0;state->bytes=0;
}
DWORD await(const Context& state,DWORD expected,Clock::time_point deadline,
            std::stop_token stop,const std::string& label,const char* operation) {
  std::unique_lock lock(state->mutex);
  while(!state->completion) {
    checkDeadline(deadline,stop,label);
    state->changed.wait_until(lock,std::min(deadline,Clock::now()+std::chrono::milliseconds(50)));
  }
  checkDeadline(deadline,stop,label);
  if(state->error)httpFailure(label,operation,state->error);
  if(state->completion!=expected)fail(label,"unexpected remote HTTP completion");
  return state->bytes;
}
void option(HINTERNET handle,DWORD name,DWORD value,const std::string& label) {
  if(!WinHttpSetOption(handle,name,&value,sizeof(value)))httpFailure(label,"configuration",GetLastError());
}
std::optional<std::size_t> contentLength(HINTERNET request,std::size_t maxBytes,const std::string& label) {
  wchar_t value[64]{};
  DWORD size=sizeof(value),index=0;
  if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,value,&size,&index)) {
    const auto error=GetLastError();
    if(error==ERROR_WINHTTP_HEADER_NOT_FOUND)return {};
    httpFailure(label,"Content-Length header",error);
  }
  const std::wstring_view number(value,size/sizeof(wchar_t));
  if(number.empty())fail(label,"invalid remote HTTP Content-Length");
  std::size_t length=0;
  for(const auto c:number) {
    if(c<L'0'||c>L'9')fail(label,"invalid remote HTTP Content-Length");
    const auto digit=static_cast<std::size_t>(c-L'0');
    if(length>maxBytes/10||(length==maxBytes/10&&digit>maxBytes%10))
      fail(label,"remote resource byte budget exceeded (maximum "+std::to_string(maxBytes)+")");
    length=length*10+digit;
  }
  size=sizeof(value);
  if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,value,&size,&index))
    fail(label,"duplicate remote HTTP Content-Length");
  if(GetLastError()!=ERROR_WINHTTP_HEADER_NOT_FOUND)httpFailure(label,"Content-Length header",GetLastError());
  return length;
}
#endif
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

std::string fetchRemoteResource(const RemoteLink& remote,const std::string& decodedRelative,
                               std::size_t maxBytes,std::stop_token stop) {
  const auto link="link://"+remote.authority()+"/"+remote.world;
  const auto checked=parseRemoteLink(link);
  // A manually constructed RemoteLink must already have a decoded world slug.
  if(checked.world!=remote.world)fail(link,"remote world must already be decoded");
  const auto label=link+"/"+decodedRelative;
  const auto path="/"+remote.world+"/"+resourcePath(decodedRelative,label);
  cancelled(stop,label);
#ifdef _WIN32
  const auto deadline=Clock::now()+transferTimeout;
  HttpHandle session(WinHttpOpen(L"LINK-Browser/0.1",WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
  if(!session.value)httpFailure(label,"session",GetLastError());
  if(!WinHttpSetTimeouts(session.value,operationTimeoutMs,operationTimeoutMs,operationTimeoutMs,operationTimeoutMs))
    httpFailure(label,"timeouts",GetLastError());
  const std::wstring host(remote.host.begin(),remote.host.end()),object(path.begin(),path.end());
  HttpHandle connection(WinHttpConnect(session.value,host.c_str(),remote.port,0));
  if(!connection.value)httpFailure(label,"connection",GetLastError());
  const wchar_t* accepts[]={L"*/*",nullptr};
  // The path is already escaped from UTF-8 bytes; keep '%' intact.
  HttpHandle request(WinHttpOpenRequest(connection.value,L"GET",object.c_str(),nullptr,
                      WINHTTP_NO_REFERER,accepts,WINHTTP_FLAG_ESCAPE_DISABLE|WINHTTP_FLAG_REFRESH));
  if(!request.value)httpFailure(label,"request",GetLastError());
  option(request.value,WINHTTP_OPTION_DISABLE_FEATURE,
         WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION,label);
  option(request.value,WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,operationTimeoutMs,label);
  option(request.value,WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,16384,label);
  auto state=std::make_shared<Transfer>();
  auto binding=std::make_unique<Context>(state);
  if(WinHttpSetStatusCallback(request.value,statusCallback,
     WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE|WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE|
     WINHTTP_CALLBACK_FLAG_READ_COMPLETE|WINHTTP_CALLBACK_FLAG_REQUEST_ERROR|WINHTTP_CALLBACK_FLAG_HANDLES,0)
     ==WINHTTP_INVALID_STATUS_CALLBACK)httpFailure(label,"callback",GetLastError());
  const auto context=reinterpret_cast<DWORD_PTR>(binding.get());
  if(!WinHttpSetOption(request.value,WINHTTP_OPTION_CONTEXT_VALUE,
                       const_cast<DWORD_PTR*>(&context),sizeof(context)))
    httpFailure(label,"callback context",GetLastError());
  binding.release(); // Released only by the last HANDLE_CLOSING callback.
  prepare(state,deadline,stop,label);
  constexpr std::wstring_view headers=L"Accept-Encoding: identity\r\n";
  if(!WinHttpSendRequest(request.value,headers.data(),static_cast<DWORD>(headers.size()),
                         WINHTTP_NO_REQUEST_DATA,0,0,context))
    httpFailure(label,"send",GetLastError());
  (void)await(state,WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline,stop,label,"send");
  prepare(state,deadline,stop,label);
  if(!WinHttpReceiveResponse(request.value,nullptr))httpFailure(label,"response",GetLastError());
  (void)await(state,WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline,stop,label,"response");
  DWORD status=0,size=sizeof(status);
  if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))
    httpFailure(label,"status",GetLastError());
  if(status!=200)fail(label,"remote HTTP status "+std::to_string(status)+" (expected 200; redirects are disabled)");
  const auto length=contentLength(request.value,maxBytes,label);
  std::string body;
  if(length)body.reserve(*length);
  for(;;) {
    prepare(state,deadline,stop,label);
    if(!WinHttpReadData(request.value,state->buffer.data(),static_cast<DWORD>(state->buffer.size()),nullptr))
      httpFailure(label,"read",GetLastError());
    const auto bytes=await(state,WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline,stop,label,"read");
    if(!bytes)break;
    if(bytes>maxBytes-body.size())fail(label,"remote resource byte budget exceeded (maximum "+std::to_string(maxBytes)+")");
    body.append(state->buffer.data(),bytes);
  }
  if(length&&body.size()!=*length)fail(label,"remote HTTP body does not match Content-Length");
  checkDeadline(deadline,stop,label);
  return body;
#else
  (void)path;(void)maxBytes;
  fail(label,"remote LINK HTTP transport is supported only on Windows");
#endif
}
} // namespace linkworld