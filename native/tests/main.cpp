#include <exception>
#include <iostream>
#include "link/loader.hpp"
#include <filesystem>
void testParser(); void testScript(); void testRuntime(); void testExhibition(); void testAfterlight(); void testWalk(); void testGuides(); void testNetwork();
int junctionTest(const std::filesystem::path& fixture) {
  int rejected=0;
  try{linkworld::resolveLink("link://local/escaped",fixture/"worlds");}catch(const linkworld::Error& e){if(std::string(e.what()).find("escapes")!=std::string::npos)++rejected;}
  try{linkworld::resolveResource(fixture/"worlds"/"room-a","escape/payload.element");}catch(const linkworld::Error& e){if(std::string(e.what()).find("escapes")!=std::string::npos)++rejected;}
  if(rejected==2){std::cout<<"PASS canonical directory junction confinement (LINK and resource)\n";return 0;}
  std::cerr<<"FAIL junction confinement: expected two rejected escapes, got "<<rejected<<'\n';return 1;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
  if(argc==3&&std::wstring(argv[1])==L"--junction-fixture")return junctionTest(argv[2]);
#else
int main(int argc,char** argv){
  if(argc==3&&std::string(argv[1])=="--junction-fixture")return junctionTest(argv[2]);
#endif
  int failures=0;
  for(auto [name,test]:{std::pair<const char*,void(*)()>{"parser/link/style",testParser},{"script interpreter",testScript},{"runtime/navigation",testRuntime},{"exhibition/architecture",testExhibition},{"afterlight/room",testAfterlight},{"walk/portal",testWalk},{"documentation example",testGuides},{"remote LINK URI",testNetwork}}){
    try{test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  }
  return failures?1:0;
}
