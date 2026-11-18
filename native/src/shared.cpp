#include "link/shared.hpp"
#include "link/remote.hpp"
#include "link/text.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <stop_token>
#include <string_view>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace linkworld {
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::size_t bodyLimit=65536,actionLimit=32;
constexpr double integerLimit=9007199254740991.;
constexpr std::uint8_t joinMessage=16,joinedMessage=17,stepMessage=18,stateMessage=19,leaveMessage=20,failMessage=21;
struct SharedError:std::runtime_error{using std::runtime_error::runtime_error;};
[[noreturn]] void fail(const char* text){throw SharedError(text);}
bool utf8(std::string_view text){
  for(std::size_t i=0;i<text.size();){unsigned c=static_cast<unsigned char>(text[i++]);if(c<128)continue;unsigned count=0,point=0,minimum=0;
    if(c>=0xc2&&c<=0xdf){count=1;point=c&31;minimum=0x80;}else if(c>=0xe0&&c<=0xef){count=2;point=c&15;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){count=3;point=c&7;minimum=0x10000;}else return false;
    if(count>text.size()-i)return false;while(count--){c=static_cast<unsigned char>(text[i++]);if((c&0xc0)!=0x80)return false;point=(point<<6)|(c&63);}if(point<minimum||point>0x10ffff||(point>=0xd800&&point<=0xdfff))return false;
  }return true;
}
bool textOkay(std::string_view value,std::size_t maximum,bool empty=false){return (empty||!value.empty())&&value.size()<=maximum&&utf8(value)&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;});}
bool tokenOkay(std::string_view value,bool empty){return (empty||!value.empty())&&value.size()<=512&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=33&&c<=126;});}
void appendPoint(std::string& result,unsigned point){
  if(point<0x80)result+=static_cast<char>(point);
  else if(point<0x800){result+=static_cast<char>(0xc0|(point>>6));result+=static_cast<char>(0x80|(point&63));}
  else if(point<0x10000){result+=static_cast<char>(0xe0|(point>>12));result+=static_cast<char>(0x80|((point>>6)&63));result+=static_cast<char>(0x80|(point&63));}
  else{result+=static_cast<char>(0xf0|(point>>18));result+=static_cast<char>(0x80|((point>>12)&63));result+=static_cast<char>(0x80|((point>>6)&63));result+=static_cast<char>(0x80|(point&63));}
}
// JSON has a stricter lexical grammar than LINK documents. Decode JSON into the
// existing Cursor token model, validating quoted keys and punctuation first.
// Cursor then supplies bounded Value construction and duplicate-key rejection.
Value::Map parseJson(const std::string& source){
  try{
    if(source.empty()||source.size()>bodyLimit||!utf8(source))fail("Shared LINK returned invalid JSON.");
    std::vector<Token> tokens;std::size_t at=0;
    auto take=[&](){if(at==source.size())fail("Shared LINK returned invalid JSON.");return source[at++];};
    auto hex=[&](){unsigned point=0;for(int i=0;i<4;++i){const auto c=take();const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(n<0)fail("Shared LINK returned invalid JSON.");point=(point<<4)|static_cast<unsigned>(n);}return point;};
    auto push=[&](TokenKind kind,std::string text){if(tokens.size()>=12000)fail("Shared LINK JSON value budget exceeded.");tokens.push_back({kind,std::move(text),{"<shared-json>",1,1}});};
    while(at<source.size()){
      const auto c=source[at];if(c==' '||c=='\t'||c=='\n'||c=='\r'){++at;continue;}
      if(std::string_view("{}[]:,").find(c)!=std::string_view::npos){++at;push(TokenKind::Symbol,std::string(1,c));continue;}
      if(c=='"'){
        ++at;std::string text;bool closed=false;
        while(at<source.size()){
          auto next=take();if(next=='"'){closed=true;break;}if(static_cast<unsigned char>(next)<32)fail("Shared LINK returned invalid JSON.");
          if(next=='\\'){
            switch(take()){
              case '"':text+='"';break;case '\\':text+='\\';break;case '/':text+='/';break;
              case 'n':text+='\n';break;case 'r':text+='\r';break;case 't':text+='\t';break;case 'b':text+='\b';break;case 'f':text+='\f';break;
              case 'u':{auto point=hex();if(point>=0xd800&&point<=0xdbff){if(take()!='\\'||take()!='u')fail("Shared LINK returned invalid JSON.");const auto low=hex();if(low<0xdc00||low>0xdfff)fail("Shared LINK returned invalid JSON.");point=0x10000+((point-0xd800)<<10)+(low-0xdc00);}else if(point>=0xdc00&&point<=0xdfff)fail("Shared LINK returned invalid JSON.");appendPoint(text,point);break;}
              default:fail("Shared LINK returned invalid JSON.");
            }
          }else text+=next;
          if(text.size()>Limits::stringBytes)fail("Shared LINK JSON string budget exceeded.");
        }
        if(!closed)fail("Shared LINK returned invalid JSON.");push(TokenKind::String,std::move(text));continue;
      }
      if(c=='-'||(c>='0'&&c<='9')){
        const auto begin=at;if(source[at]=='-')++at;
        if(at==source.size())fail("Shared LINK returned invalid JSON.");
        if(source[at]=='0')++at;else{if(source[at]<'1'||source[at]>'9')fail("Shared LINK returned invalid JSON.");while(at<source.size()&&source[at]>='0'&&source[at]<='9')++at;}
        if(at<source.size()&&source[at]=='.'){++at;const auto digits=at;while(at<source.size()&&source[at]>='0'&&source[at]<='9')++at;if(at==digits)fail("Shared LINK returned invalid JSON.");}
        if(at<source.size()&&(source[at]=='e'||source[at]=='E')){++at;if(at<source.size()&&(source[at]=='+'||source[at]=='-'))++at;const auto digits=at;while(at<source.size()&&source[at]>='0'&&source[at]<='9')++at;if(at==digits)fail("Shared LINK returned invalid JSON.");}
        const auto number=source.substr(begin,at-begin);double value=0;const auto parsed=std::from_chars(number.data(),number.data()+number.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||!std::isfinite(value))fail("Shared LINK returned invalid JSON.");push(TokenKind::Number,number);continue;
      }
      bool word=false;for(const auto literal:{"true","false","null"}){const auto size=std::char_traits<char>::length(literal);if(source.compare(at,size,literal)==0){at+=size;push(TokenKind::Identifier,literal);word=true;break;}}if(!word)fail("Shared LINK returned invalid JSON.");
    }
    tokens.push_back({TokenKind::End,"",{"<shared-json>",1,1}});std::size_t index=0;
    auto symbol=[&](const char* value){if(tokens[index].kind==TokenKind::Symbol&&tokens[index].text==value){++index;return true;}return false;};
    std::function<void(std::size_t)> validate=[&](std::size_t depth){
      if(depth>32)fail("Shared LINK JSON nesting budget exceeded.");
      if(symbol("{")){if(symbol("}"))return;for(;;){if(tokens[index].kind!=TokenKind::String)fail("Shared LINK returned invalid JSON.");++index;if(!symbol(":"))fail("Shared LINK returned invalid JSON.");validate(depth+1);if(symbol("}"))return;if(!symbol(","))fail("Shared LINK returned invalid JSON.");}}
      if(symbol("[")){if(symbol("]"))return;for(;;){validate(depth+1);if(symbol("]"))return;if(!symbol(","))fail("Shared LINK returned invalid JSON.");}}
      if(tokens[index].kind==TokenKind::String||tokens[index].kind==TokenKind::Number||tokens[index].kind==TokenKind::Identifier){++index;return;}fail("Shared LINK returned invalid JSON.");
    };
    validate(0);if(tokens[index].kind!=TokenKind::End)fail("Shared LINK returned invalid JSON.");
    Cursor cursor(std::move(tokens));auto value=cursor.value();if(!cursor.done())fail("Shared LINK returned invalid JSON.");return value.map();
  }catch(...){fail("Shared LINK returned malformed or excessive JSON.");}
}
std::string json(const Value& value,std::size_t depth=0){
  if(depth>32)fail("Shared LINK JSON nesting budget exceeded.");
  if(std::holds_alternative<std::nullptr_t>(value.data))return "null";
  if(const auto* boolean=std::get_if<bool>(&value.data))return *boolean?"true":"false";
  if(const auto* number=std::get_if<double>(&value.data)){if(!std::isfinite(*number))fail("Shared LINK requires finite numbers.");std::array<char,64> buffer{};const auto encoded=std::to_chars(buffer.data(),buffer.data()+buffer.size(),*number,std::chars_format::general,std::numeric_limits<double>::max_digits10);if(encoded.ec!=std::errc{})fail("Shared LINK could not encode a number.");return {buffer.data(),encoded.ptr};}
  if(const auto* string=std::get_if<std::string>(&value.data)){std::string result="\"";constexpr char hex[]="0123456789abcdef";for(unsigned char c:*string){if(c=='"'||c=='\\'){result+='\\';result+=static_cast<char>(c);}else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}else result+=static_cast<char>(c);}return result+'"';}
  if(const auto* array=std::get_if<Value::Array>(&value.data)){std::string result="[";for(const auto& item:*array){if(result.size()>1)result+=',';result+=json(item,depth+1);}return result+']';}
  std::string result="{";for(const auto& [key,item]:value.map()){if(result.size()>1)result+=',';result+=json(Value(key),depth+1)+':'+json(item,depth+1);}return result+'}';
}
const Value& required(const Value::Map& map,const char* key){const auto found=map.find(key);if(found==map.end())fail("Shared LINK snapshot is missing required fields.");return found->second;}
void keys(const Value::Map& map,std::initializer_list<const char*> allowed){if(map.size()!=allowed.size())fail("Shared LINK returned an unexpected snapshot shape.");for(const auto* name:allowed)required(map,name);}
double number(const Value::Map& map,const char* key,double low,double high,bool integer=false){const auto value=required(map,key).number();if(value<low||value>high||(integer&&std::floor(value)!=value))fail("Shared LINK snapshot number is outside its contract.");return value;}
std::string text(const Value::Map& map,const char* key,std::size_t limit,bool empty=false){const auto& value=required(map,key).string();if(!textOkay(value,limit,empty))fail("Shared LINK snapshot text is invalid.");return value;}
void position(const Value::Map& map,bool ground=false){
  const auto p=valueVec3(required(map,"position"));
  if(p[0]<-120.000001||p[0]>120.000001||p[2]<-140.000001||p[2]>110.000001||p[1]<-0.000001||p[1]>4.000001||(ground&&std::abs(p[1])>0.000001))
    fail("Shared LINK snapshot position is outside world bounds.");
}
void pose(const Value::Map& map,bool monster=false){position(map,monster);number(map,"yaw",-1000000,1000000);}
bool indexedId(std::string_view id,std::string_view prefix){
  if(!id.starts_with(prefix))return false;const auto suffix=id.substr(prefix.size());
  if(suffix.empty()||(suffix.size()>1&&suffix.front()=='0'))return false;
  unsigned index=0;const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),index);
  return parsed.ec==std::errc{}&&parsed.ptr==suffix.data()+suffix.size()&&index<24;
}
bool itemKind(std::string_view id,bool harvestable=false){
  constexpr std::array<std::string_view,6> items{"moonleaf","sunbloom","wildberry","slime_gel","wolf_fang","ancient_shard"};
  const auto end=items.begin()+(harvestable?3:6);return std::find(items.begin(),end,id)!=end;
}
void exploration(const Value::Map& map){
  if(const auto found=map.find("inventory");found!=map.end()){
    const auto& inventory=found->second.array();if(inventory.size()>16)fail("Shared LINK inventory exceeds its slot budget.");std::set<std::string> ids;
    for(const auto& item:inventory){const auto& entry=item.map();keys(entry,{"item","count"});const auto id=text(entry,"item",32);
      if(!itemKind(id)||!ids.insert(id).second)fail("Shared LINK inventory has an invalid or duplicate item.");number(entry,"count",1,99,true);}
  }
  if(const auto found=map.find("resources");found!=map.end()){
    const auto& resources=found->second.array();if(resources.size()>24)fail("Shared LINK resources exceed their entity budget.");std::set<std::string> ids;
    for(const auto& item:resources){const auto& resource=item.map();keys(resource,{"id","kind","position","active","respawnIn"});const auto id=text(resource,"id",32);
      if(!indexedId(id,"herb_")||!ids.insert(id).second||!itemKind(text(resource,"kind",32),true))fail("Shared LINK resource identity or kind is invalid.");
      required(resource,"active").boolean();position(resource,true);number(resource,"respawnIn",0,86400);}
  }
}
void player(const Value::Map& map){
  keys(map,{"id","slot","active","name","position","yaw","hp","maxHp","level","xp","gold","weapon","kills","quest","attack","respawn"});
  const bool active=required(map,"active").boolean();text(map,"id",64,!active);text(map,"name",128,!active);number(map,"slot",0,7,true);pose(map);
  const auto maximum=number(map,"maxHp",1,1000000000);number(map,"hp",0,maximum);number(map,"level",1,1000000,true);
  for(const auto* field:{"xp","gold","kills","attack","respawn"})number(map,field,0,integerLimit,true);number(map,"quest",0,2,true);
  const auto weapon=text(map,"weapon",16);if(weapon!="starter"&&weapon!="iron"&&weapon!="star")fail("Shared LINK snapshot weapon is unsupported.");
}
Value::Map snapshot(const Value::Map& map,const std::string& playerId){
  constexpr std::array<std::string_view,8> fields{"tick","time","self","players","monsters","notice","inventory","resources"};
  for(const auto* field:{"tick","time","self","players","monsters","notice"})required(map,field);
  for(const auto& [key,value]:map){(void)value;if(std::find(fields.begin(),fields.end(),key)==fields.end())fail("Shared LINK returned an unexpected snapshot shape.");}
  number(map,"tick",0,integerLimit,true);number(map,"time",0,integerLimit);text(map,"notice",4096,true);exploration(map);
  const auto& self=required(map,"self").map();player(self);if(!required(self,"active").boolean()||required(self,"id").string()!=playerId)fail("Shared LINK snapshot identity does not match the joined player.");
  const auto& players=required(map,"players").array();if(players.size()!=8)fail("Shared LINK snapshot requires eight player slots.");std::set<int> slots;std::set<std::string> ids;bool foundSelf=false;
  for(const auto& item:players){const auto& p=item.map();player(p);if(!slots.insert(static_cast<int>(required(p,"slot").number())).second)fail("Shared LINK snapshot has duplicate player slots.");const auto& id=required(p,"id").string();if(!id.empty()&&!ids.insert(id).second)fail("Shared LINK snapshot has duplicate player identities.");if(id==playerId){if(!(item==Value(self)))fail("Shared LINK self and player slot disagree.");foundSelf=true;}}
  if(!foundSelf)fail("Shared LINK snapshot omits the joined player slot.");
  const auto& monsters=required(map,"monsters").array();if(monsters.size()>24)fail("Shared LINK snapshot exceeds its monster budget.");
  std::set<std::string> monsterIds;
  for(const auto& item:monsters){
    const auto& m=item.map();keys(m,{"id","active","position","yaw","hp","maxHp","kind","attack","respawnIn"});const auto id=text(m,"id",32),kind=text(m,"kind",16);
    if((kind!="slime"&&kind!="wolf"&&kind!="golem"&&kind!="warden")||!indexedId(id,kind+"_")||!monsterIds.insert(id).second)fail("Shared LINK snapshot has invalid monster identities.");
    required(m,"active").boolean();pose(m,true);const auto maximum=number(m,"maxHp",1,1000000000);number(m,"hp",0,maximum);number(m,"attack",0,integerLimit,true);number(m,"respawnIn",0,86400);
  }
  return map;
}
// NPC observation is deliberately independent from the RPG player contract.
// The server owns simulation and transfer. Scripts only present local model slots.
bool npcKey(std::string_view value,std::size_t maximum){
  return !value.empty()&&value.size()<=maximum&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c==':';});
}
Value::Map npcSnapshot(const Value::Map& map,const std::string& world){
  keys(map,{"tick","time","world","npcs"});number(map,"tick",0,integerLimit,true);number(map,"time",0,integerLimit);
  if(text(map,"world",128)!=world)fail("NPC LINK snapshot belongs to another world.");
  const auto& npcs=required(map,"npcs").array();if(npcs.size()>16)fail("NPC LINK snapshot exceeds its entity budget.");
  std::set<std::string> ids;
  for(const auto& value:npcs){
    const auto& npc=value.map();keys(npc,{"id","kind","name","position","yaw","animation","epoch","journey"});
    const auto id=text(npc,"id",128),kind=text(npc,"kind",64);
    if(!npcKey(id,128)||!npcKey(kind,64)||!ids.insert(id).second)fail("NPC LINK snapshot identity or kind is invalid.");
    text(npc,"name",128);text(npc,"animation",64);
    const auto position=valueVec3(required(npc,"position"));
    if(std::any_of(position.begin(),position.end(),[](double v){return !std::isfinite(v)||std::abs(v)>1000000;}))fail("NPC LINK position is outside its contract.");
    number(npc,"yaw",-1000000,1000000);number(npc,"epoch",1,integerLimit,true);number(npc,"journey",0,integerLimit,true);
  }
  return map;
}
std::uint32_t readU32(const char* bytes){std::uint32_t value=0;for(int i=0;i<4;++i)value=(value<<8)|static_cast<unsigned char>(bytes[i]);return value;}
void writeU32(char* bytes,std::uint32_t value){for(int i=3;i>=0;--i){bytes[i]=static_cast<char>(value&255);value>>=8;}}
std::array<char,16> header(std::uint8_t type,std::uint32_t id,std::size_t length){std::array<char,16> bytes{'L','I','N','K',0,1,static_cast<char>(type),0};writeU32(bytes.data()+8,id);writeU32(bytes.data()+12,static_cast<std::uint32_t>(length));return bytes;}
#ifdef _WIN32
using Socket=SOCKET;constexpr Socket invalidSocket=INVALID_SOCKET;
int socketError(){return WSAGetLastError();}bool wouldBlock(int code){return code==WSAEWOULDBLOCK||code==WSAEINPROGRESS||code==WSAEALREADY;}
void closeSocket(Socket value){closesocket(value);}
#else
using Socket=int;constexpr Socket invalidSocket=-1;
int socketError(){return errno;}bool wouldBlock(int code){return code==EWOULDBLOCK||code==EAGAIN||code==EINPROGRESS||code==EALREADY;}
void closeSocket(Socket value){::close(value);}
#endif
}

struct SharedSession::Impl {
  RemoteLink remote;std::string name;SharedProfile profile=SharedProfile::Game;
  mutable std::mutex mutex;std::condition_variable wake;std::jthread worker;
  std::string bearer,id,failure;bool online=false,stopped=false;
  Vec3 position{0,1.65,28};double yaw=0;bool suppliedPose=false;
  std::optional<Value::Map> latest;std::deque<Value::Map> actions;std::uint64_t nextAction=1;
  Socket socket=invalidSocket;
#ifdef _WIN32
  bool winsock=false;
#endif
  void checkpoint(std::stop_token stop,Clock::time_point deadline){if(stop.stop_requested())fail("Shared LINK session stopped.");if(Clock::now()>=deadline)fail("Shared LINK connection timed out.");}
  void waitReady(bool writing,Clock::time_point deadline,std::stop_token stop){
    for(;;){checkpoint(stop,deadline);fd_set reads,writes,errors;FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors);if(writing)FD_SET(socket,&writes);else FD_SET(socket,&reads);FD_SET(socket,&errors);
      const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline-Clock::now()).count();timeval timeout{};timeout.tv_usec=static_cast<long>(std::clamp<std::int64_t>(remaining,1,50000));
#ifdef _WIN32
      const int result=::select(0,&reads,&writes,&errors,&timeout);
#else
      const int result=::select(socket+1,&reads,&writes,&errors,&timeout);
#endif
      if(result<0){
#ifndef _WIN32
        if(errno==EINTR)continue;
#endif
        fail("Shared LINK socket wait failed.");}
      checkpoint(stop,deadline);if(FD_ISSET(socket,&errors))fail("Shared LINK TCP connection failed.");if(result)return;
    }
  }
  void sendAll(std::string_view body,Clock::time_point deadline,std::stop_token stop){
    std::size_t offset=0;while(offset<body.size()){checkpoint(stop,deadline);
#ifdef _WIN32
      const auto count=::send(socket,body.data()+offset,static_cast<int>(body.size()-offset),0);
#else
#ifdef MSG_NOSIGNAL
      constexpr int flags=MSG_NOSIGNAL;
#else
      constexpr int flags=0;
#endif
      const auto count=::send(socket,body.data()+offset,body.size()-offset,flags);
#endif
      if(count<0){const auto code=socketError();
#ifndef _WIN32
        if(code==EINTR)continue;
#endif
        if(wouldBlock(code)){waitReady(true,deadline,stop);continue;}fail("Shared LINK send failed.");}
      if(count==0)fail("Shared LINK server disconnected.");offset+=static_cast<std::size_t>(count);
    }
  }
  void receiveAll(char* target,std::size_t size,Clock::time_point deadline,std::stop_token stop){
    std::size_t offset=0;while(offset<size){checkpoint(stop,deadline);
#ifdef _WIN32
      const auto count=::recv(socket,target+offset,static_cast<int>(size-offset),0);
#else
      const auto count=::recv(socket,target+offset,size-offset,0);
#endif
      if(count<0){const auto code=socketError();
#ifndef _WIN32
        if(code==EINTR)continue;
#endif
        if(wouldBlock(code)){waitReady(false,deadline,stop);continue;}fail("Shared LINK receive failed.");}
      if(count==0)fail("Shared LINK server disconnected.");offset+=static_cast<std::size_t>(count);
    }
  }
  void sendFrame(std::uint8_t type,std::uint32_t request,const Value::Map& payload,Clock::time_point deadline,std::stop_token stop){const auto body=json(Value(payload));if(body.size()>bodyLimit)fail("Shared LINK request exceeds its byte budget.");const auto bytes=header(type,request,body.size());sendAll({bytes.data(),bytes.size()},deadline,stop);sendAll(body,deadline,stop);}
  Value::Map receiveFrame(std::uint8_t expected,std::uint32_t request,Clock::time_point deadline,std::stop_token stop){
    std::array<char,16> bytes{};receiveAll(bytes.data(),bytes.size(),deadline,stop);
    if(std::string_view(bytes.data(),4)!="LINK"||bytes[4]!=0||bytes[5]!=1||bytes[7]!=0||readU32(bytes.data()+8)!=request)fail("Shared LINK response header or request ID is invalid.");
    const auto type=static_cast<std::uint8_t>(bytes[6]);const auto size=readU32(bytes.data()+12);if((type!=expected&&type!=failMessage)||!size||size>bodyLimit)fail("Shared LINK response type or size is invalid.");
    std::string body(size,'\0');receiveAll(body.data(),body.size(),deadline,stop);auto result=parseJson(body);
    if(type==failMessage){keys(result,{"message"});text(result,"message",4096);fail("Shared LINK server rejected the session or action.");}return result;
  }
  void connect(std::stop_token stop){
    const auto deadline=Clock::now()+std::chrono::seconds(4);checkpoint(stop,deadline);
#ifdef _WIN32
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))fail("Shared LINK socket initialization failed.");winsock=true;
#endif
    socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(socket==invalidSocket)fail("Shared LINK could not create a socket.");
#ifdef _WIN32
    u_long enabled=1;if(ioctlsocket(socket,FIONBIO,&enabled)!=0)fail("Shared LINK could not configure a nonblocking socket.");
#else
    const auto flags=fcntl(socket,F_GETFL,0);if(flags<0||fcntl(socket,F_SETFL,flags|O_NONBLOCK)<0)fail("Shared LINK could not configure a nonblocking socket.");
    if(socket>=FD_SETSIZE)fail("Shared LINK socket exceeds select capacity.");
#ifdef SO_NOSIGPIPE
    const int noSignal=1;setsockopt(socket,SOL_SOCKET,SO_NOSIGPIPE,&noSignal,sizeof(noSignal));
#endif
#endif
    const int noDelay=1;
#ifdef _WIN32
    if(setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&noDelay),sizeof(noDelay))!=0)fail("Shared LINK socket configuration failed.");
#else
    if(setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay))!=0)fail("Shared LINK socket configuration failed.");
#endif
    sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons(remote.port);
#ifdef _WIN32
    if(InetPtonA(AF_INET,remote.host.c_str(),&endpoint.sin_addr)!=1)fail("Shared LINK requires a numeric IPv4 address.");
#else
    if(inet_pton(AF_INET,remote.host.c_str(),&endpoint.sin_addr)!=1)fail("Shared LINK requires a numeric IPv4 address.");
#endif
    if(::connect(socket,reinterpret_cast<const sockaddr*>(&endpoint),sizeof(endpoint))!=0){if(!wouldBlock(socketError()))fail("Shared LINK TCP connection failed.");waitReady(true,deadline,stop);int error=0;
#ifdef _WIN32
      int size=sizeof(error);if(getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&size)!=0||error)fail("Shared LINK TCP connection failed.");
#else
      socklen_t size=sizeof(error);if(getsockopt(socket,SOL_SOCKET,SO_ERROR,&error,&size)!=0||error)fail("Shared LINK TCP connection failed.");
#endif
    }
  }
  void close(std::uint32_t nextId) noexcept {
    if(socket!=invalidSocket){const auto leave=header(leaveMessage,nextId,0);
#ifdef _WIN32
      (void)::send(socket,leave.data(),static_cast<int>(leave.size()),0);::shutdown(socket,SD_BOTH);
#else
#ifdef MSG_NOSIGNAL
      (void)::send(socket,leave.data(),leave.size(),MSG_NOSIGNAL);
#else
      (void)::send(socket,leave.data(),leave.size(),0);
#endif
      ::shutdown(socket,SHUT_RDWR);
#endif
      closeSocket(socket);socket=invalidSocket;}
#ifdef _WIN32
    if(winsock){WSACleanup();winsock=false;}
#endif
  }
  void run(std::stop_token stop) noexcept {
    std::uint32_t nextId=1;
    try{
      connect(stop);std::string secret;{std::lock_guard lock(mutex);secret=bearer;}
      auto deadline=Clock::now()+std::chrono::seconds(4);const bool observer=profile==SharedProfile::NpcTravel;
      const Value::Map join=observer?Value::Map{{"version",1},{"world",remote.world},{"profile","npc-travel-v1"}}:Value::Map{{"version",1},{"world",remote.world},{"token",secret},{"name",name}};
      sendFrame(joinMessage,0,join,deadline,stop);const auto joined=receiveFrame(joinedMessage,0,deadline,stop);
      Value::Map initial;std::string joinedToken,joinedId;
      if(observer){keys(joined,{"snapshot"});initial=npcSnapshot(required(joined,"snapshot").map(),remote.world);}
      else{keys(joined,{"token","playerId","snapshot"});joinedToken=required(joined,"token").string();joinedId=text(joined,"playerId",64);if(!tokenOkay(joinedToken,false))fail("Shared LINK returned an invalid bearer credential.");initial=snapshot(required(joined,"snapshot").map(),joinedId);}
      auto previousTick=number(initial,"tick",0,integerLimit,true);
      {std::lock_guard lock(mutex);bearer=joinedToken;id=joinedId;online=true;latest=initial;if(!observer&&!suppliedPose){position=valueVec3(required(required(initial,"self").map(),"position"));yaw=required(required(initial,"self").map(),"yaw").number();}}
      auto next=Clock::now()+std::chrono::milliseconds(50);
      while(!stop.stop_requested()){
        Vec3 posePosition;double poseYaw;Value action;
        {std::unique_lock lock(mutex);wake.wait_until(lock,next,[&]{return stop.stop_requested();});if(stop.stop_requested())break;posePosition=position;poseYaw=yaw;if(!actions.empty()){action=std::move(actions.front());actions.pop_front();}}
        const auto sent=Clock::now();deadline=sent+std::chrono::seconds(2);
        const Value::Map step=observer?Value::Map{}:Value::Map{{"position",vecValue(posePosition)},{"yaw",poseYaw},{"action",std::move(action)}};
        sendFrame(stepMessage,nextId,step,deadline,stop);
        const auto received=receiveFrame(stateMessage,nextId,deadline,stop);
        auto state=observer?npcSnapshot(received,remote.world):snapshot(received,joinedId);const auto tick=number(state,"tick",0,integerLimit,true);if(tick<previousTick)fail("Shared LINK snapshot tick moved backwards.");previousTick=tick;
        {std::lock_guard lock(mutex);latest=std::move(state);}
        if(nextId==std::numeric_limits<std::uint32_t>::max())fail("Shared LINK request sequence is exhausted.");++nextId;next=sent+std::chrono::milliseconds(50);
      }
    }catch(const SharedError& error){if(!stop.stop_requested()){std::lock_guard lock(mutex);failure=error.what();}}
    catch(...){if(!stop.stop_requested()){std::lock_guard lock(mutex);failure="Shared LINK received an invalid response or encountered a connection failure.";}}
    close(nextId);{std::lock_guard lock(mutex);online=false;stopped=true;actions.clear();}
  }
};
SharedSession::SharedSession(std::string address,std::string token,std::string name,SharedProfile profile):impl_(std::make_unique<Impl>()){
  try{impl_->remote=parseRemoteLink(address);}catch(...){throw std::invalid_argument("Shared LINK requires link://IPv4[:port]/world.");}
  if(profile!=SharedProfile::Game&&profile!=SharedProfile::NpcTravel)throw std::invalid_argument("Shared LINK profile is unsupported.");
  if(profile==SharedProfile::NpcTravel&&!token.empty())throw std::invalid_argument("NPC observation does not accept player credentials.");
  if(!tokenOkay(token,true))throw std::invalid_argument("Shared LINK bearer credential is invalid.");if(!textOkay(name,128))throw std::invalid_argument("Shared LINK player name is invalid.");impl_->bearer=std::move(token);impl_->name=std::move(name);impl_->profile=profile;
  impl_->worker=std::jthread([state=impl_.get()](std::stop_token stop){state->run(stop);});
}
SharedSession::~SharedSession(){stop();}
void SharedSession::updatePose(Vec3 position,double yaw){if(impl_->profile==SharedProfile::NpcTravel)return;if(!std::isfinite(yaw)||std::abs(yaw)>1000000||std::any_of(position.begin(),position.end(),[](double n){return !std::isfinite(n)||std::abs(n)>1000000;}))throw std::invalid_argument("Shared LINK pose must be finite and bounded.");std::lock_guard lock(impl_->mutex);impl_->position=position;impl_->yaw=yaw;impl_->suppliedPose=true;}
bool SharedSession::sendAction(const std::string& kind,Value::Map payload){
  if(impl_->profile==SharedProfile::NpcTravel)return false;
  if(kind!="attack"&&kind!="equip"&&kind!="rest"&&kind!="claim"&&kind!="gather")throw std::invalid_argument("Shared LINK action kind is unsupported.");
  const auto found=payload.find("target");if(payload.size()!=1||found==payload.end()||!std::holds_alternative<std::string>(found->second.data)||!textOkay(found->second.string(),128))throw std::invalid_argument("Shared LINK action requires one bounded target.");
  std::lock_guard lock(impl_->mutex);if(impl_->stopped||impl_->worker.get_stop_token().stop_requested()||impl_->actions.size()>=actionLimit||impl_->nextAction>static_cast<std::uint64_t>(integerLimit))return false;
  payload.emplace("kind",kind);payload.emplace("seq",static_cast<double>(impl_->nextAction++));impl_->actions.push_back(std::move(payload));return true;
}
std::optional<Value::Map> SharedSession::poll(){std::lock_guard lock(impl_->mutex);auto result=std::move(impl_->latest);impl_->latest.reset();return result;}
std::string SharedSession::token()const{std::lock_guard lock(impl_->mutex);return impl_->bearer;}
std::string SharedSession::playerId()const{std::lock_guard lock(impl_->mutex);return impl_->id;}
std::string SharedSession::error()const{std::lock_guard lock(impl_->mutex);return impl_->failure;}
bool SharedSession::connected()const{std::lock_guard lock(impl_->mutex);return impl_->online;}
void SharedSession::stop()noexcept{if(!impl_)return;impl_->worker.request_stop();impl_->wake.notify_all();if(impl_->worker.joinable())impl_->worker.join();}
} // namespace linkworld
