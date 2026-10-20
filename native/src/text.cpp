#include "link/text.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>

namespace linkworld {
namespace {
bool letter(char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
bool digit(char c){return c>='0'&&c<='9';}
void validateUtf8(std::string_view source,const std::string& file) {
  std::size_t line=1,column=1;
  for(std::size_t i=0;i<source.size();) {
    const auto byte=static_cast<unsigned char>(source[i]);std::size_t length=1;unsigned int point=byte;
    if(byte>=0x80){if(byte>=0xc2&&byte<=0xdf){length=2;point=byte&0x1f;}else if(byte>=0xe0&&byte<=0xef){length=3;point=byte&0x0f;}else if(byte>=0xf0&&byte<=0xf4){length=4;point=byte&7;}else throw Error({file,line,column},"invalid UTF-8 source");
      if(length>source.size()-i)throw Error({file,line,column},"truncated UTF-8 source");
      for(std::size_t j=1;j<length;++j){const auto continuation=static_cast<unsigned char>(source[i+j]);if((continuation&0xc0)!=0x80)throw Error({file,line,column},"invalid UTF-8 continuation");point=(point<<6)|(continuation&0x3f);}
      if((length==3&&point<0x800)||(length==4&&point<0x10000)||(point>=0xd800&&point<=0xdfff)||point>0x10ffff)throw Error({file,line,column},"invalid UTF-8 code point");
    }
    if(byte=='\n'){++line;column=1;}else column+=length;i+=length;
  }
}
double numeric(const Token& token) {
  double result=0;auto text=std::string_view(token.text);
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,std::chars_format::general);
  if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||!std::isfinite(result))throw Error(token.location,"invalid or nonfinite number '"+token.text+"'");
  return result;
}
}
std::vector<Token> lex(std::string_view source,const std::string& file) {
  if(source.size()>Limits::documentBytes)throw Error({file,1,1},"source byte budget exceeded");
  validateUtf8(source,file);
  std::vector<Token> result;std::size_t index=0,line=1,column=1;
  const auto advance=[&]() {const auto c=source[index++];if(c=='\n'){++line;column=1;}else ++column;return c;};
  const auto at=[&](std::size_t ahead=0){return index+ahead<source.size()?source[index+ahead]:'\0';};
  const auto push=[&](TokenKind kind,std::string text,Location where){if(result.size()>=Limits::tokens)throw Error(where,"token budget exceeded");result.push_back({kind,std::move(text),std::move(where)});};
  while(index<source.size()) {
    const auto c=at();
    if(c==' '||c=='\t'||c=='\r'||c=='\n'){advance();continue;}
    if(c=='/'&&at(1)=='/'){while(index<source.size()&&at()!='\n')advance();continue;}
    Location where{file,line,column};
    if(letter(c)) {const auto start=index;while(letter(at())||digit(at()))advance();push(TokenKind::Identifier,std::string(source.substr(start,index-start)),where);continue;}
    if(digit(c)||(c=='.'&&digit(at(1)))) {
      const auto start=index;
      while(digit(at()))advance();
      if(at()=='.'){advance();while(digit(at()))advance();}
      if(at()=='e'||at()=='E'){advance();if(at()=='+'||at()=='-')advance();if(!digit(at()))throw Error(where,"number exponent requires digits");while(digit(at()))advance();}
      if(letter(at())||at()=='.')throw Error(where,"invalid numeric token");
      Token token{TokenKind::Number,std::string(source.substr(start,index-start)),where};numeric(token);push(token.kind,token.text,where);continue;
    }
    if(c=='"') {
      advance();std::string text;bool closed=false;
      while(index<source.size()) {
        auto character=advance();if(character=='"'){closed=true;break;}
        if(static_cast<unsigned char>(character)<32)throw Error(where,"raw control character in string");
        if(character=='\\'){
          if(index>=source.size())throw Error(where,"unterminated string escape");
          switch(advance()){case 'n':character='\n';break;case 'r':character='\r';break;case 't':character='\t';break;case '"':character='"';break;case '\\':character='\\';break;default:throw Error(where,"unsupported string escape");}
        }
        text+=character;if(text.size()>Limits::stringBytes)throw Error(where,"string byte budget exceeded");
      }
      if(!closed)throw Error(where,"unterminated string");push(TokenKind::String,std::move(text),where);continue;
    }
    const std::string pair=std::string(1,c)+at(1);
    if(pair=="=="||pair=="!="||pair=="<="||pair==">="||pair=="&&"||pair=="||"){advance();advance();push(TokenKind::Symbol,pair,where);continue;}
    if(std::string_view("{}[]():;,.+-*/%=!<>&|").find(c)!=std::string_view::npos){advance();push(TokenKind::Symbol,std::string(1,c),where);continue;}
    throw Error(where,"unsupported character in source");
  }
  result.push_back({TokenKind::End,"",{file,line,column}});return result;
}
const Token& Cursor::peek(std::size_t ahead) const {
  if(tokens_.empty())throw Error({},"empty token stream");
  const auto last=tokens_.size()-1;return tokens_[ahead>last-index_?last:index_+ahead];
}
Token Cursor::take(){auto result=peek();if(result.kind!=TokenKind::End)++index_;return result;}
bool Cursor::match(const std::string& text){const auto expected=!text.empty()&&letter(text[0])?TokenKind::Identifier:TokenKind::Symbol;if(peek().text==text&&peek().kind==expected){take();return true;}return false;}
Token Cursor::expect(const std::string& text){const auto expected=!text.empty()&&letter(text[0])?TokenKind::Identifier:TokenKind::Symbol;if(peek().text!=text||peek().kind!=expected)throw Error(peek().location,"expected '"+text+"', found '"+peek().text+"'");return take();}
Token Cursor::expectKind(TokenKind kind,const std::string& description){if(peek().kind!=kind)throw Error(peek().location,"expected "+description);return take();}
bool Cursor::done() const{return peek().kind==TokenKind::End;}
Value Cursor::value(std::size_t depth) {
  std::size_t count=0;
  std::function<Value(std::size_t)> parse=[&](std::size_t current)->Value {
    const auto where=peek().location;if(current>Limits::parseDepth)throw Error(where,"data parse depth budget exceeded");
    if(++count>Limits::astNodes)throw Error(where,"data AST budget exceeded");
    if(match("[")){Value::Array result;if(match("]"))return result;do{result.push_back(parse(current+1));}while(match(","));expect("]");return result;}
    if(match("{")){Value::Map result;if(match("}"))return result;do{const auto key=take();if(key.kind!=TokenKind::Identifier&&key.kind!=TokenKind::String)throw Error(key.location,"expected map key");expect(":");auto item=parse(current+1);if(!result.emplace(key.text,std::move(item)).second)throw Error(key.location,"duplicate map key '"+key.text+"'");}while(match(","));expect("}");return result;}
    const bool negative=match("-");const bool positive=!negative&&match("+");
    if(negative||positive){const auto token=expectKind(TokenKind::Number,"number after sign");return negative?-numeric(token):numeric(token);}
    const auto token=take();
    if(token.kind==TokenKind::Number)return numeric(token);
    if(token.kind==TokenKind::String)return token.text;
    if(token.kind==TokenKind::Identifier){if(token.text=="true")return true;if(token.text=="false")return false;if(token.text=="null")return nullptr;}
    throw Error(token.location,"expected data value");
  };
  return parse(depth);
}
} // namespace linkworld
