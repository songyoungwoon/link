#include "link/script.hpp"
#include "link/text.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <utility>

namespace linkworld {
namespace {
struct Expr {
  enum class Kind { Literal,Name,Unary,Binary,Member,Index,Call,Array,Map } kind;
  Location location;
  Value literal;
  std::string text;
  std::size_t height=1;
  std::vector<std::unique_ptr<Expr>> operands;
  std::vector<std::string> keys;
};
struct Statement {
  enum class Kind { Store,Let,Assign,Expression,If } kind;
  Location location;
  std::string name;
  std::unique_ptr<Expr> expression;
  std::vector<Statement> thenBlock,elseBlock;
};
struct Hook {
  Location location;
  std::vector<std::string> parameters;
  std::vector<Statement> body;
};
const std::map<std::string,std::vector<std::string>> hookParameters={
  {"onStart",{"ctx"}}, {"onUpdate",{"ctx","deltaSeconds"}},
  {"onInteract",{"ctx","event"}}, {"onDestroy",{"ctx"}}
};
const std::set<std::string> builtinNames={"min","max","abs","clamp","mix"};
const std::set<std::string> apiNames={"getState","setState","getTransform","setTransform","find","setStyle","clearStyle","log"};
bool reserved(const std::string& name) {
  return name=="ctx" || name=="deltaSeconds" || name=="event" || name=="true" || name=="false" || name=="null" ||
         name=="let" || name=="store" || name=="if" || name=="else" || name=="on";
}
}

struct ScriptProgram { std::string file; std::map<std::string,Hook> hooks; };

namespace {
class Parser {
public:
  explicit Parser(std::vector<Token> tokens):cursor_(std::move(tokens)){}
  std::shared_ptr<const ScriptProgram> program(const std::string& file) {
    auto result=std::make_shared<ScriptProgram>(); result->file=file;
    while(!cursor_.done()) {
      auto location=cursor_.expect("on").location;
      auto name=cursor_.expectKind(TokenKind::Identifier,"lifecycle hook name");
      const auto expected=hookParameters.find(name.text);
      if(expected==hookParameters.end()) throw Error(name.location,"unsupported lifecycle hook '"+name.text+"'");
      if(result->hooks.contains(name.text)) throw Error(name.location,"duplicate lifecycle hook '"+name.text+"'");
      Hook hook; hook.location=location;
      cursor_.expect("(");
      if(!cursor_.match(")")) {
        do { hook.parameters.push_back(cursor_.expectKind(TokenKind::Identifier,"hook parameter").text); } while(cursor_.match(","));
        cursor_.expect(")");
      }
      if(hook.parameters!=expected->second) throw Error(name.location,"invalid parameters for "+name.text);
      hook.body=block(0); validateBlock(hook.body,0);
      result->hooks.emplace(name.text,std::move(hook));
    }
    if(result->hooks.empty()) throw Error({file,1,1},"Script must declare at least one lifecycle hook");
    return result;
  }
private:
  Cursor cursor_;
  std::size_t astNodes_=0;
  std::unique_ptr<Expr> finish(std::unique_ptr<Expr> expression) {
    for(const auto& operand:expression->operands) expression->height=std::max(expression->height,operand->height+1);
    if(expression->height>Limits::parseDepth) throw Error(expression->location,"Script expression depth budget exceeded");
    return expression;
  }
  void validateExpression(const Expr& expression,std::size_t depth) {
    if(depth>Limits::parseDepth) throw Error(expression.location,"Script expression depth budget exceeded");
    for(const auto& operand:expression.operands) validateExpression(*operand,depth+1);
  }
  void validateBlock(const std::vector<Statement>& statements,std::size_t depth) {
    if(depth>Limits::parseDepth) throw Error(cursor_.peek().location,"Script block depth budget exceeded");
    for(const auto& statement:statements) {
      validateExpression(*statement.expression,1); validateBlock(statement.thenBlock,depth+1); validateBlock(statement.elseBlock,depth+1);
    }
  }
  void budget(Location location,std::size_t depth) {
    if(depth>Limits::parseDepth) throw Error(location,"Script parser depth budget exceeded");
    if(++astNodes_>Limits::astNodes) throw Error(location,"Script AST node budget exceeded");
  }
  std::unique_ptr<Expr> expressionNode(Expr::Kind kind,Location location,std::size_t depth) {
    budget(location,depth);
    auto result=std::make_unique<Expr>(); result->kind=kind; result->location=std::move(location); return result;
  }
  std::vector<Statement> block(std::size_t depth) {
    if(depth>Limits::parseDepth) throw Error(cursor_.peek().location,"Script parser depth budget exceeded");
    cursor_.expect("{"); std::vector<Statement> result;
    while(!cursor_.match("}")) {
      if(cursor_.done()) throw Error(cursor_.peek().location,"expected closing '}'");
      result.push_back(statement(depth+1));
    }
    return result;
  }
  Statement statement(std::size_t depth) {
    const auto token=cursor_.peek(); budget(token.location,depth);
    Statement result; result.location=token.location;
    if(cursor_.match("if")) {
      result.kind=Statement::Kind::If; cursor_.expect("("); result.expression=expression(0,depth+1); cursor_.expect(")");
      result.thenBlock=block(depth+1);
      if(cursor_.match("else")) result.elseBlock=block(depth+1);
      return result;
    }
    if(token.kind==TokenKind::Identifier && (token.text=="store" || token.text=="let")) {
      cursor_.take(); result.kind=token.text=="store"?Statement::Kind::Store:Statement::Kind::Let;
      auto name=cursor_.expectKind(TokenKind::Identifier,"variable name");
      if(reserved(name.text)) throw Error(name.location,"reserved variable name '"+name.text+"'");
      result.name=name.text; cursor_.expect("="); result.expression=expression(0,depth+1); cursor_.expect(";"); return result;
    }
    if(token.kind==TokenKind::Identifier && cursor_.peek(1).kind==TokenKind::Symbol && cursor_.peek(1).text=="=") {
      cursor_.take(); cursor_.take();
      if(reserved(token.text)) throw Error(token.location,"cannot assign to reserved name '"+token.text+"'");
      result.kind=Statement::Kind::Assign; result.name=token.text; result.expression=expression(0,depth+1); cursor_.expect(";"); return result;
    }
    result.kind=Statement::Kind::Expression; result.expression=expression(0,depth+1); cursor_.expect(";"); return result;
  }
  static int precedence(const std::string& symbol) {
    if(symbol=="||") return 1; if(symbol=="&&") return 2;
    if(symbol=="==" || symbol=="!=") return 3;
    if(symbol=="<" || symbol==">" || symbol=="<=" || symbol==">=") return 4;
    if(symbol=="+" || symbol=="-") return 5;
    if(symbol=="*" || symbol=="/" || symbol=="%") return 6;
    return -1;
  }
  std::unique_ptr<Expr> expression(int minimum,std::size_t depth) {
    if(depth>Limits::parseDepth) throw Error(cursor_.peek().location,"Script parser depth budget exceeded");
    auto left=unary(depth);
    while(true) {
      const auto token=cursor_.peek(); const int current=token.kind==TokenKind::Symbol?precedence(token.text):-1;
      if(current<minimum) break;
      cursor_.take(); auto operation=expressionNode(Expr::Kind::Binary,token.location,depth);
      operation->text=token.text; operation->operands.push_back(std::move(left));
      operation->operands.push_back(expression(current+1,depth+1)); left=finish(std::move(operation));
    }
    return left;
  }
  std::unique_ptr<Expr> unary(std::size_t depth) {
    const auto token=cursor_.peek();
    if(token.kind==TokenKind::Symbol && (token.text=="!" || token.text=="-" || token.text=="+")) {
      cursor_.take(); auto result=expressionNode(Expr::Kind::Unary,token.location,depth); result->text=token.text;
      result->operands.push_back(unary(depth+1)); return finish(std::move(result));
    }
    auto result=primary(depth);
    while(true) {
      if(cursor_.match(".")) {
        auto name=cursor_.expectKind(TokenKind::Identifier,"member name");
        if(cursor_.match("(")) {
          if(result->kind!=Expr::Kind::Name || result->text!="ctx") throw Error(name.location,"only ctx.method(...) API calls are supported");
          if(!apiNames.contains(name.text)) throw Error(name.location,"unknown ctx API '"+name.text+"'");
          auto call=expressionNode(Expr::Kind::Call,name.location,depth); call->text="ctx."+name.text;
          call->operands=arguments(depth); result=finish(std::move(call));
        } else {
          auto member=expressionNode(Expr::Kind::Member,name.location,depth); member->text=name.text;
          member->operands.push_back(std::move(result)); result=finish(std::move(member));
        }
      } else if(cursor_.match("[")) {
        auto index=expressionNode(Expr::Kind::Index,cursor_.peek().location,depth);
        index->operands.push_back(std::move(result)); index->operands.push_back(expression(0,depth+1)); cursor_.expect("]"); result=finish(std::move(index));
      } else break;
    }
    return result;
  }
  std::vector<std::unique_ptr<Expr>> arguments(std::size_t depth) {
    std::vector<std::unique_ptr<Expr>> result;
    if(!cursor_.match(")")) {
      do { result.push_back(expression(0,depth+1)); } while(cursor_.match(","));
      cursor_.expect(")");
    }
    return result;
  }
  std::unique_ptr<Expr> primary(std::size_t depth) {
    const auto token=cursor_.take();
    if(token.kind==TokenKind::Symbol && token.text=="(") { auto result=expression(0,depth+1); cursor_.expect(")"); return result; }
    if(token.kind==TokenKind::Symbol && token.text=="[") {
      auto result=expressionNode(Expr::Kind::Array,token.location,depth);
      if(!cursor_.match("]")) {
        do { result->operands.push_back(expression(0,depth+1)); } while(cursor_.match(","));
        cursor_.expect("]");
      }
      return finish(std::move(result));
    }
    if(token.kind==TokenKind::Symbol && token.text=="{") {
      auto result=expressionNode(Expr::Kind::Map,token.location,depth); std::set<std::string> keys;
      if(!cursor_.match("}")) {
        do {
          auto key=cursor_.take();
          if(key.kind!=TokenKind::Identifier && key.kind!=TokenKind::String) throw Error(key.location,"expected a map key");
          if(!keys.insert(key.text).second) throw Error(key.location,"duplicate map key '"+key.text+"'");
          cursor_.expect(":"); result->keys.push_back(key.text); result->operands.push_back(expression(0,depth+1));
        } while(cursor_.match(","));
        cursor_.expect("}");
      }
      return finish(std::move(result));
    }
    if(token.kind==TokenKind::Number) {
      auto result=expressionNode(Expr::Kind::Literal,token.location,depth);
      double numeric=0;
      const auto parsed=std::from_chars(token.text.data(),token.text.data()+token.text.size(),numeric,std::chars_format::general);
      if(parsed.ec!=std::errc{} || parsed.ptr!=token.text.data()+token.text.size() || !std::isfinite(numeric)) throw Error(token.location,"invalid or nonfinite numeric literal");
      result->literal=numeric; return result;
    }
    if(token.kind==TokenKind::String || (token.kind==TokenKind::Identifier && (token.text=="true" || token.text=="false" || token.text=="null"))) {
      auto result=expressionNode(Expr::Kind::Literal,token.location,depth);
      if(token.kind==TokenKind::String) result->literal=token.text;
      else if(token.text=="true" || token.text=="false") result->literal=token.text=="true";
      return result;
    }
    if(token.kind==TokenKind::Identifier) {
      if(cursor_.match("(")) {
        if(!builtinNames.contains(token.text)) throw Error(token.location,"unknown builtin '"+token.text+"'; arbitrary calls are not supported");
        auto result=expressionNode(Expr::Kind::Call,token.location,depth); result->text=token.text; result->operands=arguments(depth); return finish(std::move(result));
      }
      if(token.text=="if" || token.text=="else" || token.text=="let" || token.text=="store" || token.text=="on") throw Error(token.location,"expected an expression");
      auto result=expressionNode(Expr::Kind::Name,token.location,depth); result->text=token.text; return result;
    }
    throw Error(token.location,"expected a Script expression");
  }
};
}

std::shared_ptr<const ScriptProgram> parseScript(std::string_view source,const std::string& file) {
  if(source.size()>Limits::scriptBytes) throw Error({file,1,1},"Script file byte budget exceeded");
  return Parser(lex(source,file)).program(file);
}

struct ScriptInstance::Impl {
  std::shared_ptr<const ScriptProgram> program;
  Api api;
  Value::Map persistent,locals;
  std::set<std::string> parameters;
  std::size_t steps=0,calls=0;
  bool invoking=false;
  void step(Location where) { if(++steps>Limits::evalSteps) throw Error(where,"Script evaluation step budget exceeded"); }
  void storageBudget(const Value::Map& nextPersistent,const Value::Map& nextLocals,Location where) {
    if(Value(nextPersistent).bytes()+Value(nextLocals).bytes()>Limits::storageBytes) throw Error(where,"Script storage byte budget exceeded");
  }
  void assign(const std::string& name,Value value,Statement::Kind kind,Location where) {
    if(parameters.contains(name)) throw Error(where,"cannot overwrite hook parameter '"+name+"'");
    auto nextPersistent=persistent, nextLocals=locals;
    if(kind==Statement::Kind::Store) {
      if(nextPersistent.contains(name) || nextLocals.contains(name)) throw Error(where,"duplicate variable '"+name+"'");
      nextPersistent.emplace(name,std::move(value));
    } else if(kind==Statement::Kind::Let) {
      if(nextPersistent.contains(name) || nextLocals.contains(name)) throw Error(where,"duplicate variable '"+name+"'");
      nextLocals.emplace(name,std::move(value));
    } else if(nextLocals.contains(name)) nextLocals[name]=std::move(value);
    else if(nextPersistent.contains(name)) nextPersistent[name]=std::move(value);
    else throw Error(where,"assignment to unknown variable '"+name+"'");
    storageBudget(nextPersistent,nextLocals,where);
    persistent=std::move(nextPersistent); locals=std::move(nextLocals);
  }
  Value evaluate(const Expr& expression) {
    step(expression.location);
    try { return evaluateImpl(expression); }
    catch(const Error& error) { if(error.location.file.empty()) throw Error(expression.location,error.what()); throw; }
    catch(const std::exception& exception) { throw Error(expression.location,exception.what()); }
  }
  Value finite(double number,Location location) {
    if(!std::isfinite(number)) throw Error(location,"arithmetic result must be finite"); return number;
  }
  Value evaluateImpl(const Expr& expression) {
    const auto& values=expression.operands;
    switch(expression.kind) {
      case Expr::Kind::Literal: return expression.literal;
      case Expr::Kind::Name: {
        if(auto found=locals.find(expression.text);found!=locals.end()) return found->second;
        if(auto found=persistent.find(expression.text);found!=persistent.end()) return found->second;
        throw Error(expression.location,"unknown variable '"+expression.text+"'");
      }
      case Expr::Kind::Array: {
        Value::Array result; for(const auto& entry:values) result.push_back(evaluate(*entry));
        Value value(std::move(result)); if(value.bytes()>Limits::storageBytes) throw Error(expression.location,"temporary array byte budget exceeded"); return value;
      }
      case Expr::Kind::Map: {
        Value::Map result; for(std::size_t i=0;i<values.size();++i) result.emplace(expression.keys[i],evaluate(*values[i]));
        Value value(std::move(result)); if(value.bytes()>Limits::storageBytes) throw Error(expression.location,"temporary map byte budget exceeded"); return value;
      }
      case Expr::Kind::Member: {
        const auto base=evaluate(*values[0]); const auto& map=base.map(); const auto found=map.find(expression.text);
        if(found==map.end()) throw Error(expression.location,"map has no field '"+expression.text+"'"); return found->second;
      }
      case Expr::Kind::Index: {
        const auto base=evaluate(*values[0]), index=evaluate(*values[1]);
        if(const auto* array=std::get_if<Value::Array>(&base.data)) {
          const double numeric=index.number();
          if(numeric<0 || std::floor(numeric)!=numeric || numeric>=static_cast<double>(array->size())) throw Error(expression.location,"array index out of bounds or not an integer");
          return array->at(static_cast<std::size_t>(numeric));
        }
        const auto& map=base.map(); const auto found=map.find(index.string());
        if(found==map.end()) throw Error(expression.location,"map key does not exist"); return found->second;
      }
      case Expr::Kind::Unary: {
        const auto operand=evaluate(*values[0]);
        if(expression.text=="!") return !operand.boolean();
        return finite(expression.text=="-"?-operand.number():operand.number(),expression.location);
      }
      case Expr::Kind::Binary: {
        const auto left=evaluate(*values[0]);
        if(expression.text=="&&") { if(!left.boolean()) return false; return evaluate(*values[1]).boolean(); }
        if(expression.text=="||") { if(left.boolean()) return true; return evaluate(*values[1]).boolean(); }
        const auto right=evaluate(*values[1]);
        if(expression.text=="==") return left==right;
        if(expression.text=="!=") return !(left==right);
        if(expression.text=="+" && std::holds_alternative<std::string>(left.data) && std::holds_alternative<std::string>(right.data)) {
          const auto text=left.string()+right.string(); if(text.size()>Limits::stringBytes) throw Error(expression.location,"string byte budget exceeded"); return text;
        }
        const double a=left.number(), b=right.number();
        if(expression.text=="<") return a<b; if(expression.text==">") return a>b;
        if(expression.text=="<=") return a<=b; if(expression.text==">=") return a>=b;
        if(expression.text=="+") return finite(a+b,expression.location);
        if(expression.text=="-") return finite(a-b,expression.location);
        if(expression.text=="*") return finite(a*b,expression.location);
        if(b==0) throw Error(expression.location,"division or remainder by zero");
        if(expression.text=="/") return finite(a/b,expression.location);
        return finite(std::fmod(a,b),expression.location);
      }
      case Expr::Kind::Call: {
        if(++calls>Limits::callDepth) { --calls; throw Error(expression.location,"Script call depth budget exceeded"); }
        struct CallGuard { std::size_t& calls; ~CallGuard(){--calls;} } guard{calls};
        std::vector<Value> arguments; for(const auto& argument:values) arguments.push_back(evaluate(*argument));
        if(expression.text.starts_with("ctx.")) return api(expression.text.substr(4),arguments);
        const auto arity=expression.text=="abs"?1u:(expression.text=="min" || expression.text=="max")?2u:3u;
        if(arguments.size()!=arity) throw Error(expression.location,"wrong argument count for builtin '"+expression.text+"'");
        const double a=arguments[0].number();
        if(expression.text=="abs") return finite(std::abs(a),expression.location);
        const double b=arguments[1].number();
        if(expression.text=="min") return std::min(a,b);
        if(expression.text=="max") return std::max(a,b);
        const double c=arguments[2].number();
        if(expression.text=="clamp") {
          if(b>c) throw Error(expression.location,"clamp minimum exceeds maximum");
          return std::clamp(a,b,c);
        }
        return finite(a+(b-a)*c,expression.location);
      }
    }
    throw Error(expression.location,"unknown AST expression");
  }
  void execute(const std::vector<Statement>& statements) {
    for(const auto& statement:statements) {
      step(statement.location);
      try {
        if(statement.kind==Statement::Kind::If) execute(evaluate(*statement.expression).boolean()?statement.thenBlock:statement.elseBlock);
        else {
          auto value=evaluate(*statement.expression);
          if(statement.kind!=Statement::Kind::Expression) assign(statement.name,std::move(value),statement.kind,statement.location);
        }
      } catch(const Error& error) { if(error.location.file.empty()) throw Error(statement.location,error.what()); throw; }
      catch(const std::exception& exception) { throw Error(statement.location,exception.what()); }
    }
  }
};

ScriptInstance::ScriptInstance(std::shared_ptr<const ScriptProgram> program,Api api):impl_(std::make_unique<Impl>()) {
  if(!program || !api) throw std::invalid_argument("ScriptInstance requires a program and API dispatcher");
  impl_->program=std::move(program); impl_->api=std::move(api);
}
ScriptInstance::~ScriptInstance()=default;
const Value::Map& ScriptInstance::storage() const { return impl_->persistent; }
void ScriptInstance::invoke(const std::string& hook,const std::vector<Value>& arguments) {
  const auto found=impl_->program->hooks.find(hook); if(found==impl_->program->hooks.end()) return;
  if(impl_->invoking) throw Error(found->second.location,"recursive Script invocation is not supported");
  if(found->second.parameters.size()!=arguments.size()) throw Error(found->second.location,"wrong lifecycle argument count");
  impl_->invoking=true; impl_->steps=0; impl_->calls=0; impl_->locals.clear(); impl_->parameters.clear();
  struct InvocationGuard { Impl& impl; ~InvocationGuard(){impl.locals.clear();impl.parameters.clear();impl.invoking=false;} } guard{*impl_};
  for(std::size_t i=0;i<arguments.size();++i) {
    impl_->locals.emplace(found->second.parameters[i],arguments[i]); impl_->parameters.insert(found->second.parameters[i]);
  }
  try { impl_->storageBudget(impl_->persistent,impl_->locals,found->second.location); }
  catch(const Error& error) { if(error.location.file.empty()) throw Error(found->second.location,error.what()); throw; }
  impl_->execute(found->second.body);
}
} // namespace linkworld
