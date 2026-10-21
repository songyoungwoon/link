#include "link/remote.hpp"
#include "link/core.hpp"
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition,const std::string& message) {
  if(!condition)throw std::runtime_error("network test failed: "+message);
}
void rejects(const std::function<void()>& action,const std::string& label) {
  bool rejected=false;
  try{action();}catch(const linkworld::Error&){rejected=true;}
  check(rejected,label);
}
}
void testNetwork() {
  using namespace linkworld;
  const auto basic=parseRemoteLink("link://192.168.0.42/hello-world");
  check(basic.host=="192.168.0.42"&&basic.world=="hello-world"&&basic.port==8000,
        "numeric LAN addresses use the default server port");
  check(basic.authority()=="192.168.0.42:8000","authority explicitly includes the effective port");
  const auto escaped=parseRemoteLink("link://127.0.0.1:49152/%68ello%2DWorld9");
  check(escaped.world=="hello-World9"&&escaped.port==49152,"a world slug is decoded exactly once");
  check(parseRemoteLink("link://0.0.0.0:1/a").port==1&&
        parseRemoteLink("link://255.255.255.255:65535/Z9").port==65535,
        "IPv4 octet and TCP port boundaries are accepted");
  const std::vector<std::string> invalid={
    "", "http://192.168.0.42/a", "LINK://192.168.0.42/a",
    "link://local/a", "link://localhost/a", "link://example.com/a",
    "link://127.1/a", "link://127.0.1/a", "link://127.0.0.1.2/a",
    "link://127.00.0.1/a", "link://01.2.3.4/a", "link://256.1.2.3/a",
    "link://-1.2.3.4/a", "link://0x7f.0.0.1/a", "link://2130706433/a",
    "link://[::1]/a", "link://192.168.0.%31/a", "link://192.168.0.1./a",
    "link://192.168.0.1:/a", "link://192.168.0.1:0/a",
    "link://192.168.0.1:65536/a", "link://192.168.0.1:+80/a",
    "link://192.168.0.1:80:90/a", "link://user@192.168.0.1/a",
    "link://192.168.0.1", "link://192.168.0.1/", "link://192.168.0.1/a/",
    "link://192.168.0.1/a/b", "link://192.168.0.1/..", "link://192.168.0.1/a_b",
    "link://192.168.0.1/a?x=1", "link://192.168.0.1/a#b",
    "link://192.168.0.1/a%2Fb", "link://192.168.0.1/a%5Cb",
    "link://192.168.0.1/%252e%252e", "link://192.168.0.1/a%3Fb",
    "link://192.168.0.1/%00", "link://192.168.0.1/%7F",
    "link://192.168.0.1/a%", "link://192.168.0.1/a%2", "link://192.168.0.1/a%GG",
    "link://192.168.0.1/a b", "link://192.168.0.1/a\\b"
  };
  for(const auto& address:invalid)rejects([&]{(void)parseRemoteLink(address);},"reject unsafe or unsupported URI: "+address);
  rejects([]{(void)parseRemoteLink("link://127.0.0.1/"+std::string(2048,'x'));},"URI length is bounded");

  // Validation runs before opening any connection, so these tests need no server.
  const auto remote=parseRemoteLink("link://127.0.0.1:1/hello-world");
  const std::vector<std::string> invalidResources={
    "", "/", "/world.link", "//other/world.link", "../world.link",
    "assets/../world.link", "assets/./../world.link", "assets/..",
    "C:/world.link", "C:\\world.link", "assets\\photo.png",
    "world.link?x=1", "world.link#part", "user@host/file",
    "%2e%2e/world.link", "assets/%252e%252e/file", "assets/a%2fb",
    "assets/", ".", "./", std::string("a\0b",3), "a\nb", "a\x7f""b",
    "a/\xC0\xAF""b",
    "a/\xED\xA0\x80"
  };
  for(const auto& resource:invalidResources)
    rejects([&]{(void)fetchRemoteResource(remote,resource,1024);},"reject unsafe decoded resource");
  rejects([&]{(void)fetchRemoteResource(remote,std::string(2049,'x'),1024);},"resource length is bounded");
  rejects([]{(void)fetchRemoteResource({"127.0.0.1","%61",8000},"world.link",1024);},
          "manually constructed remote slugs cannot be encoded twice");
  rejects([]{(void)fetchRemoteResource({"localhost","a",8000},"world.link",1024);},
          "manually constructed remote targets still require literal IPv4");
  std::stop_source stopped;stopped.request_stop();
  for(const auto& resource:{"world.link","./scene.element","assets//./photo one.png","assets/\xED\x95\x9C\xEA\xB8\x80.png"}) {
    try {
      (void)fetchRemoteResource(remote,resource,1024,stopped.get_token());
      check(false,"pre-cancelled requests must fail before networking");
    }catch(const Error& error) {
      check(std::string(error.what()).find("cancelled")!=std::string::npos,
            "safe relative paths, spaces and UTF-8 survive validation and observe cancellation");
    }
  }
}