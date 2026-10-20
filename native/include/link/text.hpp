#pragma once
#include "core.hpp"
#include <string_view>
namespace linkworld {
enum class TokenKind { Identifier,Number,String,Symbol,End };
struct Token { TokenKind kind; std::string text; Location location; };
std::vector<Token> lex(std::string_view source,const std::string& file);
class Cursor {
public:
  explicit Cursor(std::vector<Token> tokens):tokens_(std::move(tokens)){}
  const Token& peek(std::size_t ahead=0) const;
  Token take(); bool match(const std::string&); Token expect(const std::string&);
  Token expectKind(TokenKind,const std::string& description);
  Value value(std::size_t depth=0);
  bool done() const;
private: std::vector<Token> tokens_; std::size_t index_=0;
};
} // namespace linkworld
