#include "link/script.hpp"
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace linkworld;
void check(bool condition,const std::string& description) {
  if(!condition) throw std::runtime_error("Script particle test failed: "+description);
}
void rejects(const std::function<void()>& action,const std::string& diagnostic) {
  try { action(); }
  catch(const Error& error) {
    check(!error.location.file.empty() && error.location.line>0 && error.location.column>0,"errors retain source coordinates");
    check(std::string(error.what()).find(diagnostic)!=std::string::npos,"expected diagnostic '"+diagnostic+"', got "+error.what());
    return;
  }
  throw std::runtime_error("Script particle test expected rejection: "+diagnostic);
}
Value api(const std::string&,const std::vector<Value>&) { return {}; }
Value context() { return Value::Map{{"id","particles"},{"config",Value::Map{}}}; }
void rejectExpression(const std::string& expression,const std::string& diagnostic) {
  ScriptInstance instance(parseScript("on onStart(ctx) { let value = "+expression+"; }","invalid-particle.script"),api);
  rejects([&]{instance.invoke("onStart",{context()});},diagnostic);
}
bool near(double a,double b) { return std::abs(a-b)<1e-10; }
}

void testScriptParticles() {
  using namespace linkworld;
  auto loops=parseScript(R"(
    on onStart(ctx) {
      store total = 0;
      store nested = 0;
      store shadowed = 100;
      let id = 77;
      let scratch = 9;
      for (let id of [1,2,3]) {
        let scratch = id * 2;
        total = total + scratch;
        if (id == 2) { let conditional = 3; total = total + conditional; }
        for (let id of [4,5]) {
          let scratch = id;
          nested = nested + scratch;
        }
        total = total + id;
      }
      for (let unused of [0]) { let shadowed = 2; total = total + shadowed; }
      store restored = id + scratch;
      if (true) { let legacyBranch = 8; }
      store legacyResult = legacyBranch;
    }
  )","loops.script");
  ScriptInstance loopInstance(loops,api);
  loopInstance.invoke("onStart",{context()});
  check(loopInstance.storage().at("total").number()==23,"iteration locals, nested loops and outer assignments");
  check(loopInstance.storage().at("nested").number()==27,"nested iterators restore outer iterator");
  check(loopInstance.storage().at("restored").number()==86,"shadowed outer locals are restored");
  check(loopInstance.storage().at("shadowed").number()==100,"iteration let can shadow persistent storage without overwriting it");
  check(loopInstance.storage().at("legacyResult").number()==8,"legacy if-block let visibility is retained outside iteration scopes");

  auto snapshot=parseScript(R"(
    on onStart(ctx) {
      store sum = 0;
      let ids = [1,2,3];
      for (let id of ids) { ids = []; sum = sum + id; ctx.removeParticle(id); }
      store remaining = len(ids);
      for (let id of []) { sum = 999; }
    }
  )","snapshot.script");
  std::vector<double> removed;
  ScriptInstance snapshotInstance(snapshot,[&](const std::string& method,const std::vector<Value>& arguments)->Value {
    check(method=="removeParticle","particle API dispatch name"); removed.push_back(arguments.at(0).number()); return {};
  });
  snapshotInstance.invoke("onStart",{context()});
  check(snapshotInstance.storage().at("sum").number()==6 && removed==std::vector<double>{1,2,3},"for-of uses one stable collection snapshot");
  check(snapshotInstance.storage().at("remaining").number()==0,"assignment to an outer collection persists");

  auto math=parseScript(R"(
    on onStart(ctx) {
      store add = vadd([1,2,3],[4,-1,.5]);
      store subtract = vsub([1,2,3],[4,-1,.5]);
      store multiply = vmul([1,-2,3],.5);
      store product = dot([1,2,3],[4,5,6]);
      store unit = normalize([3,4,0]);
      store zero = normalize([0,0,0]);
      store tiny = normalize([1e-300,0,0]);
      store magnitude = length([3,4,12]);
      store root = sqrt(81);
      store trig = sin(0) + cos(0);
      store sequence = range(4);
      store count = len(sequence);
      store empty = len(range(0));
    }
  )","vectors.script");
  ScriptInstance mathInstance(math,api); mathInstance.invoke("onStart",{context()});
  const auto& values=mathInstance.storage();
  check(valueVec3(values.at("add"))==Vec3{5,1,3.5},"vector addition");
  check(valueVec3(values.at("subtract"))==Vec3{-3,3,2.5},"vector subtraction");
  check(valueVec3(values.at("multiply"))==Vec3{.5,-1,1.5},"vector scalar multiplication");
  check(values.at("product").number()==32 && values.at("magnitude").number()==13,"dot product and Euclidean norm");
  check(near(valueVec3(values.at("unit"))[0],.6) && near(valueVec3(values.at("unit"))[1],.8),"normalization");
  check(valueVec3(values.at("zero"))==Vec3{} && valueVec3(values.at("tiny"))==Vec3{1,0,0},"zero and very small vectors normalize safely");
  check(values.at("root").number()==9 && values.at("trig").number()==1,"sqrt sin cos");
  check(values.at("sequence").array()==Value::Array{0,1,2,3} && values.at("count").number()==4 && values.at("empty").number()==0,"range and array length");
  ScriptInstance utf8(parseScript(std::string("on onStart(ctx) { store bytes = len(\"")+"\xed\x9d\x91"+"\"); }","utf8-len.script"),api);
  utf8.invoke("onStart",{context()}); check(utf8.storage().at("bytes").number()==3,"string length counts UTF-8 bytes");

  rejectExpression("range(-1)","integer between 0 and 2048");
  rejectExpression("range(1.5)","integer between 0 and 2048");
  rejectExpression("range(2049)","integer between 0 and 2048");
  rejectExpression("len({})","array or string");
  rejectExpression("len(true)","array or string");
  rejectExpression("sqrt(-1)","nonnegative");
  rejectExpression("sin(true)","number");
  rejectExpression("normalize([1,2])","three finite numbers");
  rejectExpression("length([1,2,3,4])","three finite numbers");
  rejectExpression("dot([1,2,true],[1,2,3])","three finite numbers");
  rejectExpression("vadd([1e6,0,0],[1,0,0])","coordinate magnitude");
  rejectExpression("normalize([1e7,0,0])","coordinate magnitude");
  rejectExpression("vmul([1,0,0],1e308)","coordinate magnitude");
  rejectExpression("vmul([1e6,0,0],1e308)","finite numbers");
  rejectExpression("vadd([1,2,3])","argument count");
  rejectExpression("[1,2,3] + [4,5,6]","number");

  ScriptInstance nonArray(parseScript("on onStart(ctx) { for (let id of 3) {} }","not-array.script"),api);
  rejects([&]{nonArray.invoke("onStart",{context()});},"array");
  ScriptInstance tooMany(parseScript("on onStart(ctx) { for (let id of ctx.particleIds()) {} }","large-array.script"),
    [](const std::string&,const std::vector<Value>&)->Value { return Value::Array(2049,Value(0)); });
  rejects([&]{tooMany.invoke("onStart",{context()});},"2048 element budget");
  ScriptInstance duplicate(parseScript("on onStart(ctx) { for (let id of [1]) { let local = 1; let local = 2; } }","duplicate-loop.script"),api);
  rejects([&]{duplicate.invoke("onStart",{context()});},"duplicate variable");
  ScriptInstance escaped(parseScript("on onStart(ctx) { for (let id of [1]) { let local = 1; } ctx.log(local); }","escaped-loop.script"),api);
  rejects([&]{escaped.invoke("onStart",{context()});},"unknown variable");
  rejects([&]{parseScript("on onStart(ctx) { for (let ctx of [1]) {} }","reserved-loop.script");},"reserved variable");
  rejects([&]{parseScript("on onStart(ctx) { for (id of [1]) {} }","loop-declaration.script");},"let");
  rejects([&]{parseScript("on onStart(ctx) { for (let id = 0; id < 3; id = id + 1) {} }","c-loop.script");},"of");
  rejects([&]{parseScript("on onStart(ctx) { ctx.runNative(); }","arbitrary-api.script");},"unknown ctx API");
  rejects([&]{parseScript("on onStart(ctx) { while (true) {} }","while.script");},"unknown builtin");

  int calls=0;
  auto logged=parseScript("on onUpdate(ctx, deltaSeconds) { ctx.log(1); }","shared-budget.script");
  const auto log=[&](const std::string&,const std::vector<Value>&)->Value { ++calls; return {}; };
  ScriptInstance first(logged,log),second(logged,log);
  std::size_t shared=6;
  first.invoke("onUpdate",{context(),.01},&shared,65536);
  check(shared==2 && calls==1,"each evaluator/API step consumes the world budget");
  rejects([&]{second.invoke("onUpdate",{context(),.01},&shared,65536);},"world evaluation step budget");
  check(shared==0 && calls==1,"shared exhaustion prevents the next side effect");
  shared=100;
  rejects([&]{first.invoke("onUpdate",{context(),.01},&shared,2);},"evaluation step budget");
  check(shared==98 && calls==1,"local budget also stops side effects without underflowing shared budget");
  first.invoke("onUpdate",{context(),.01});
  check(calls==2,"failed invocation releases transient state and the shared-budget pointer");

  ScriptInstance maximum(parseScript("on onStart(ctx) { store count = 0; for (let id of range(2048)) { count = count + 1; } }","maximum-loop.script"),api);
  maximum.invoke("onStart",{context()},nullptr,65536);
  check(maximum.storage().at("count").number()==2048,"maximum legal range executes under an explicit larger bounded budget");
  ScriptInstance nestedBudget(parseScript("on onStart(ctx) { for (let a of range(2048)) { for (let b of range(2048)) {} } }","nested-budget.script"),api);
  rejects([&]{nestedBudget.invoke("onStart",{context()},nullptr,65536);},"evaluation step budget");

  const auto largeApi=[](const std::string&,const std::vector<Value>& arguments)->Value {
    if(arguments.at(0).string()=="text") return std::string(8192,'x');
    const auto count=arguments.at(0).string()=="six"?6:4;
    Value::Map padding; for(int i=0;i<count;++i) padding.emplace(std::to_string(i),std::string(8192,'x'));
    return padding;
  };
  ScriptInstance snapshotBudget(parseScript("on onStart(ctx) { store padding = ctx.getState(\"four\"); let ids = range(2048); for (let id of ids) {} }","snapshot-storage.script"),largeApi);
  rejects([&]{snapshotBudget.invoke("onStart",{context()},nullptr,65536);},"storage byte budget");
  check(Value(snapshotBudget.storage()).bytes()<=Limits::storageBytes,"snapshot failure retains only bounded persistent data");
  ScriptInstance shadowBudget(parseScript("on onStart(ctx) { let padding = ctx.getState(\"six\"); let original = ctx.getState(\"text\"); for (let id of [1]) { let original = ctx.getState(\"text\"); } }","shadow-storage.script"),largeApi);
  rejects([&]{shadowBudget.invoke("onStart",{context()});},"storage byte budget");

  bool oversizedReplacement=true;
  ScriptInstance replacement(parseScript(R"(
    on onStart(ctx) { store stable = [1,2,3]; store other = 7; }
    on onUpdate(ctx,deltaSeconds) { stable = ctx.getState("replacement"); }
  )","replacement-storage.script"),[&](const std::string&,const std::vector<Value>&)->Value {
    return oversizedReplacement?Value(Value::Array(8191,Value(0))):Value(Value::Array{4,5,6});
  });
  replacement.invoke("onStart",{context()});
  rejects([&]{replacement.invoke("onUpdate",{context(),.01});},"storage byte budget");
  check(replacement.storage().at("stable").array()==Value::Array{1,2,3} && replacement.storage().at("other").number()==7,
    "failed in-place assignment preflight preserves the old slot and unrelated storage");
  oversizedReplacement=false; replacement.invoke("onUpdate",{context(),.01});
  check(replacement.storage().at("stable").array()==Value::Array{4,5,6},"valid replacement succeeds after a rejected mutation");

  ScriptInstance depthMutation(parseScript(R"(
    on onStart(ctx) { store chain = null; }
    on onUpdate(ctx,deltaSeconds) { chain = {x:chain}; }
  )","replacement-depth.script"),api);
  depthMutation.invoke("onStart",{context()});
  for(int i=0;i<63;++i)depthMutation.invoke("onUpdate",{context(),.01});
  const auto previousChain=depthMutation.storage().at("chain");
  rejects([&]{depthMutation.invoke("onUpdate",{context(),.01});},"depth budget");
  check(depthMutation.storage().at("chain")==previousChain,"const-map preflight retains the original root-map depth accounting and rolls back excessive nesting");

  // Rejection must happen while appending the first oversized child, before
  // later arguments can run side effects or a large aggregate is allocated.
  for(const auto& [expression,diagnostic]:std::vector<std::pair<std::string,std::string>>{
      {"ctx.log([large,large,large,large,ctx.random()])","temporary array byte budget"},
      {"ctx.log({a:large,b:large,c:large,d:large,e:ctx.random()})","temporary map byte budget"},
      {"ctx.log(large,large,large,large,ctx.random())","call argument byte budget"}}) {
    int sideEffects=0;
    auto bounded=parseScript("on onStart(ctx) { let large = range(2048); "+expression+"; }","incremental-temporaries.script");
    ScriptInstance instance(bounded,[&](const std::string&,const std::vector<Value>&)->Value { ++sideEffects; return 0; });
    rejects([&]{instance.invoke("onStart",{context()});},diagnostic);
    check(sideEffects==0,"oversized containers and call arguments reject before later API side effects");
  }
  int acceptedCalls=0;
  ScriptInstance withinTemporaryLimit(parseScript("on onStart(ctx) { let large = range(2047); ctx.log([large,large,large,large]); }","bounded-temporary.script"),
    [&](const std::string&,const std::vector<Value>& arguments)->Value {
      ++acceptedCalls; check(arguments.at(0).bytes()==65509,"incremental accounting uses existing logical Value byte units"); return {};
    });
  withinTemporaryLimit.invoke("onStart",{context()});
  check(acceptedCalls==1,"temporary aggregate just below the byte limit is accepted");

  std::vector<std::string> methods;
  auto particleApis=parseScript(R"(on onStart(ctx) {
    ctx.spawnParticle({}); ctx.removeParticle(1); ctx.setParticle(1,{}); ctx.getParticle(1);
    ctx.particleIds(); ctx.neighbors(1,.5,4); ctx.particleCount(); ctx.random();
  })","particle-apis.script");
  ScriptInstance dispatch(particleApis,[&](const std::string& method,const std::vector<Value>&)->Value { methods.push_back(method); return {}; });
  dispatch.invoke("onStart",{context()});
  check(methods==std::vector<std::string>{"spawnParticle","removeParticle","setParticle","getParticle","particleIds","neighbors","particleCount","random"},"all bounded particle API names are admitted and dispatched");
}
