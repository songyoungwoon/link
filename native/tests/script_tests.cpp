#include "link/script.hpp"
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition,const std::string& description) {
  if(!condition) throw std::runtime_error("Script test failed: "+description);
}
void rejects(const std::function<void()>& action,const std::string& description,const std::string& text="") {
  try { action(); }
  catch(const linkworld::Error& error) {
    check(!error.location.file.empty(),description+" has source file");
    check(error.location.line>0 && error.location.column>0,description+" has line and column");
    if(!text.empty()) check(std::string(error.what()).find(text)!=std::string::npos,description+" expected diagnostic '"+text+"', got "+error.what());
    return;
  }
  throw std::runtime_error("Script test expected rejection: "+description);
}
linkworld::Value context(double speed=2) {
  return linkworld::Value::Map{{"id","test_component"},{"config",linkworld::Value::Map{{"speed",speed},{"owner","test_object"}}}};
}
linkworld::Value nullApi(const std::string&,const std::vector<linkworld::Value>&) { return {}; }
void runtimeReject(const std::string& expression,const std::string& description,const std::string& text) {
  auto program=linkworld::parseScript("on onStart(ctx) { let result = "+expression+"; }","failure.script");
  linkworld::ScriptInstance instance(program,nullApi);
  rejects([&]{instance.invoke("onStart",{context()});},description,text);
}
}

void testScript() {
  using namespace linkworld;
  auto expressionProgram=parseScript(R"(
    // Operators are typed, and logical evaluation short-circuits.
    on onStart(ctx) {
      store precedence = 1 + 2 * 3;
      store relation = precedence == 7 && !(2 > 3);
      store skipped = false && unknownVariable;
      store skippedOr = true || unknownVariable;
      store fraction = -.15 + 1e-3;
      store point = {items: [1, 2, {height: 3}]}.items[2].height;
      store keyLookup = {"dash-key": 9}["dash-key"];
      store interpolation = mix(10, 20, .25);
      store clamped = clamp(18, 0, 10);
      store magnitude = abs(-3);
      store maximum = max(min(8, 4), 2);
      store same = [1, 2] == [1, 2];
      store text = "mint" + " studio";
      store negativeRemainder = -7 % 4;
      store quotedSymbol = "-";
      store quotedDelimiter = "[";
      store quotedKeyword = "true";
      if (relation) { store branch = 10; } else { store branch = 20; }
    }
  )","operators.script");
  ScriptInstance expressions(expressionProgram,nullApi);
  expressions.invoke("onStart",{context()});
  const auto& values=expressions.storage();
  check(values.at("precedence").number()==7,"multiplication precedence");
  check(values.at("relation").boolean() && !values.at("skipped").boolean() && values.at("skippedOr").boolean(),"booleans and short circuit");
  check(std::abs(values.at("fraction").number()+.149)<1e-10,"signed fractions and exponent");
  check(values.at("point").number()==3 && values.at("keyLookup").number()==9,"member, array and map indexing");
  check(values.at("interpolation").number()==12.5 && values.at("clamped").number()==10,"mix and clamp");
  check(values.at("magnitude").number()==3 && values.at("maximum").number()==4,"abs min max");
  check(values.at("same").boolean() && values.at("text").string()=="mint studio","data equality and string concatenation");
  check(values.at("negativeRemainder").number()==-3 && values.at("branch").number()==10,"remainder and if branch");
  check(values.at("quotedSymbol").string()=="-" && values.at("quotedDelimiter").string()=="[" && values.at("quotedKeyword").string()=="true","quoted symbols and keywords remain strings");

  auto shared=parseScript(R"(
    on onStart(ctx) { store hits = 0; }
    on onUpdate(ctx, deltaSeconds) {
      let next = ctx.getState(ctx.find(ctx.config.owner)).value + ctx.config.speed * deltaSeconds;
      ctx.setState(ctx.config.owner, {value: next});
    }
    on onInteract(ctx, event) {
      let increment = 1;
      hits = hits + increment;
      ctx.log(event.targetId);
    }
    on onDestroy(ctx) { ctx.log("destroyed"); }
  )","shared.script");
  Value::Map state{{"value",10}}; int logs=0;
  const auto api=[&](const std::string& method,const std::vector<Value>& arguments)->Value {
    if(method=="find")return arguments.at(0);
    if(method=="getState")return state;
    if(method=="setState") {for(const auto& [key,value]:arguments.at(1).map())state.insert_or_assign(key,value);return {};}
    if(method=="log") {++logs;return {};}
    throw std::runtime_error("unsupported test API");
  };
  ScriptInstance first(shared,api), second(shared,api);
  first.invoke("onStart",{context(2)}); second.invoke("onStart",{context(100)});
  Value event=Value::Map{{"targetId","shape"},{"bindingId","test_component"},{"type","click"}};
  first.invoke("onInteract",{context(2),event}); first.invoke("onInteract",{context(2),event});
  second.invoke("onInteract",{context(100),event});
  check(first.storage().at("hits").number()==2 && second.storage().at("hits").number()==1,"shared immutable AST has separate persistent storage");
  check(!first.storage().contains("increment"),"callback let variables do not persist");
  first.invoke("onUpdate",{context(2),.5});
  check(state.at("value").number()==11,"ctx config, explicit find, state API and delta argument");
  first.invoke("onDestroy",{context()}); check(logs==4,"event argument and onDestroy invoked");

  runtimeReject("1 / 0","division by zero","zero");
  runtimeReject("1 % 0","remainder by zero","zero");
  runtimeReject("1 + true","typed arithmetic","number");
  runtimeReject("[1][2]","array bounds","bounds");
  runtimeReject("[1][.5]","integer array index","integer");
  runtimeReject("null.field","null map member","map");
  runtimeReject("{}.missing","unknown map member","field");
  runtimeReject("min(1)","builtin arity","argument");
  runtimeReject("clamp(1, 10, 0)","clamp bounds","minimum");
  runtimeReject("notDeclared","unknown variable","unknown variable");
  runtimeReject("1e308 * 1e308","finite arithmetic","finite");
  runtimeReject("true && 1","strict boolean operator","bool");
  rejects([&]{parseScript("on onUpdate(ctx, elapsed) {}","params.script");},"exact lifecycle parameters","parameters");
  rejects([&]{parseScript("on other(ctx) {}","hook.script");},"unknown lifecycle hook","unsupported");
  rejects([&]{parseScript("on onStart(ctx) {} on onStart(ctx) {}","duplicate.script");},"duplicate lifecycle hook","duplicate");
  rejects([&]{parseScript("on onStart(ctx) { while (true) {} }","loops.script");},"loops unsupported","builtin");
  rejects([&]{parseScript("on onStart(ctx) { ctx.readFile(\"secret\"); }","api.script");},"unknown APIs unsupported","unknown ctx API");
  rejects([&]{parseScript("on onStart(ctx) { ctx = 3; }","immutable.script");},"context assignment rejected","reserved");
  rejects([&]{parseScript("\"on\" onStart(ctx) {}","quoted-hook.script");},"quoted keyword is not grammar");
  auto missing=parseScript("on onStart(ctx) { missing = 1; }","assign.script");
  ScriptInstance unknown(missing,nullApi);
  rejects([&]{unknown.invoke("onStart",{context()});},"assignment requires declaration","unknown variable");
  auto apiError=parseScript("on onStart(ctx) { ctx.getState(); }","api-error.script");
  ScriptInstance failedApi(apiError,[](const std::string&,const std::vector<Value>&)->Value{throw std::runtime_error("test API failure");});
  rejects([&]{failedApi.invoke("onStart",{context()});},"API errors get source location","test API failure");

  std::string evaluation="on onStart(ctx) { store n = 0; ";
  for(int i=0;i<1500;++i)evaluation+="n = n + 1; ";evaluation+="}";
  ScriptInstance evaluated(parseScript(evaluation,"steps.script"),nullApi);
  rejects([&]{evaluated.invoke("onStart",{context()});},"per callback evaluation limit","step budget");

  std::string nested="1"; for(int i=0;i<18;++i)nested="abs("+nested+")";
  runtimeReject(nested,"nested builtin call depth","call depth");
  std::string deep="on onStart(ctx) { let n = ";deep+=std::string(70,'(');deep+="1";deep+=std::string(70,')');deep+="; }";
  rejects([&]{parseScript(deep,"depth.script");},"parser recursion limit","depth");
  std::string chain="on onStart(ctx) { let n = 0";for(int i=0;i<5000;++i)chain+=" + 1";chain+="; }";
  rejects([&]{parseScript(chain,"chain.script");},"long operator chain rejects before deep AST destruction","depth");
  std::string members="on onStart(ctx) { let n = ctx";for(int i=0;i<5000;++i)members+=".field";members+="; }";
  rejects([&]{parseScript(members,"members.script");},"long member chain rejects before deep AST destruction","depth");
  std::string ast="on onStart(ctx) { ";for(int i=0;i<11000;++i)ast+="0; ";ast+="}";
  rejects([&]{parseScript(ast,"ast.script");},"AST node limit","AST");

  std::string storage="on onStart(ctx) { ";
  for(int i=0;i<9;++i)storage+="store value"+std::to_string(i)+" = \""+std::string(8100,'x')+"\"; ";storage+="}";
  ScriptInstance stored(parseScript(storage,"storage.script"),nullApi);
  rejects([&]{stored.invoke("onStart",{context()});},"combined callback and persistent storage budget","storage");
  check(Value(stored.storage()).bytes()<=Limits::storageBytes,"failed storage mutation does not commit oversized data");
}
