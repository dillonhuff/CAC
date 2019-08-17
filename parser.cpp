#include "parser.h"

#include <fstream>

using namespace dbhc;

namespace CAC {

  bool isUnderscore(const char c) { return c == '_'; }

  bool isAlphaNum(const char c) { return isalnum(c); }
  
  maybe<Token> parseStr(const std::string target, TokenState& chars) {
    string str = "";
    for (int i = 0; i < (int) target.size(); i++) {
      if (chars.atEnd()) {
        return maybe<Token>();
      }

      char next = chars.parseChar();
      if (target[i] == next) {
        str += next;
      } else {
        return maybe<Token>();
      }
    }
  
    return maybe<Token>(Token(str));
  }
  
  std::function<maybe<Token>(TokenState& chars)> mkParseStr(const std::string str) {
    return [str](TokenState& state) { return parseStr(str, state); };
  }
  
  static inline
  maybe<Token> parseComma(ParseState<Token>& tokens) {
    Token t = tokens.parseChar();
    if (t.getStr() == ",") {
      return t;
    }

    return maybe<Token>();
  }

  maybe<Token> parseComment(TokenState& state) {
    // Parse any number of comment lines and whitespace
    auto result = tryParse<Token>(mkParseStr("//"), state);
    if (result.has_value()) {
      while (!state.atEnd() && !(state.peekChar() == '\n')) {
        state.parseChar();
      }

      if (!state.atEnd()) {
        //cout << "Parsing end char" << endl;
        state.parseChar();
      }

      return Token("//");

    } else {
      return maybe<Token>();
    }

  }
  
  static inline
  bool isWhitespace(const char c) {
    return isspace(c);
  }
  
  template<typename F>
  maybe<Token> consumeWhile(TokenState& state, F shouldContinue) {
    string tok = "";
    while (!state.atEnd() && shouldContinue(state.peekChar())) {
      tok += state.parseChar();
    }
    if (tok.size() > 0) {
      return Token(tok);
    } else {
      return maybe<Token>();
    }
  }
  
  static inline
  void consumeWhitespace(TokenState& state) {
    while (true) {
      auto commentM = tryParse<Token>(parseComment, state);
      if (!commentM.has_value()) {
        auto ws = consumeWhile(state, isWhitespace);
        if (!ws.has_value()) {
          return;
        }
      }
    }
  }

  Token parse_token(TokenState& state) {
    if (isalnum(state.peekChar())) {
      auto res = consumeWhile(state, [](const char c) { return isAlphaNum(c) || isUnderscore(c); });
      assert(res.has_value());
      return res.get_value();
    } else if (oneCharToken(state.peekChar())) {
      maybe<Token> result = tryParse<Token>(mkParseStr("=="), state);
      if (result.has_value()) {
        return result.get_value();
      }

      result = tryParse<Token>(mkParseStr("<="), state);
      if (result.has_value()) {
        return result.get_value();
      }

      result = tryParse<Token>(mkParseStr(">="), state);
      if (result.has_value()) {
        return result.get_value();
      }
      
      result = tryParse<Token>(mkParseStr("->"), state);
      if (result.has_value()) {
        return result.get_value();
      }
    
      char res = state.parseChar();
      string r;
      r += res;
      return Token(r, TOKEN_TYPE_SYMBOL);
    } else {
      cout << "Cannot tokenize " << state.remainder() << endl;
      assert(false);
    }

  }
  
  
  static inline
  std::vector<Token> tokenize(const std::string& classCode) {
    vector<char> chars;
    for (int i = 0; i < (int) classCode.size(); i++) {
      chars.push_back(classCode[i]);
    }
    TokenState state(chars);
    vector<Token> tokens;
  
    while (!state.atEnd()) {
      //cout << "Next char = " << state.peekChar() << endl;
      consumeWhitespace(state);

      if (state.atEnd()) {
        break;
      }

      Token t = parse_token(state);
      //cout << "Next char after token = " << state.peekChar() << endl;
      tokens.push_back(t);
    }

    return tokens;
  }

  class PortAST {
  public:
  };

  class BlockAST {
  };

  class EventAST {
  };
  
  class ModuleAST {
  public:
    
  };

#define try_consume(name, tokens) if (tokens.atEnd()) { return {}; } else if (tokens.peekChar() == Token(name)) { tokens.parseChar(); } else { return {}; }

  maybe<PortAST*> parsePortDecl(ParseState<Token>& tokens) {
    Token p = tokens.peekChar();
    if (p == Token("input") ||
        p == Token("output")) {
      bool isInput = p == Token("input");
      tokens.parseChar();

      try_consume("[", tokens);
      Token end = tokens.parseChar();
      try_consume(":", tokens);
      Token start = tokens.parseChar();
      try_consume("]", tokens);

      Token name = tokens.parseChar();
      return new PortAST();
    }

    return {};
  }

  maybe<EventAST*> parseReset(ParseState<Token>& tokens) {
    if (tokens.atEnd()) {
      return {};
    }

    Token n = tokens.peekChar();
    if (n == Token("posedge") ||
        n == Token("negedge") ||
        n == Token("synch")) {
      tokens.parseChar();
      Token sig = tokens.parseChar();
      return new EventAST();
    }

    return {};

  }
  
  maybe<EventAST*> parseEvent(ParseState<Token>& tokens) {
    if (tokens.atEnd()) {
      return {};
    }

    Token n = tokens.peekChar();
    if (n == Token("posedge") ||
        n == Token("negedge")) {
      tokens.parseChar();
      Token sig = tokens.parseChar();
      return new EventAST();
    }

    return {};
  }
  
  maybe<BlockAST*> parseBlock(ParseState<Token>& tokens) {
    // Sequence block or declaration
    try_consume("sequence", tokens);
    try_consume("@", tokens);
    try_consume("(", tokens);
    auto synchM = parseEvent(tokens);
    if (!synchM.has_value()) {
      return {};
    }

    try_consume(",", tokens);
    
    auto rstM = parseReset(tokens);
    if (!rstM.has_value()) {
      return {};
    }
    try_consume(")", tokens);

    // Turn in to parsing a statement
    try_consume("begin", tokens);
    try_consume("end", tokens);            
    return new BlockAST();
  }
  
  maybe<ModuleAST*> parseModule(ParseState<Token>& tokens) {
    try_consume("module", tokens);
    Token modName = tokens.parseChar();
    try_consume("(", tokens);
    auto ports = sepBtwn0<PortAST*, Token>(parsePortDecl, parseComma, tokens);
    try_consume(")", tokens);
    try_consume(";", tokens);

    auto body = many<BlockAST*, Token>(parseBlock, tokens);
    try_consume("endmodule", tokens);
    
    return new ModuleAST();
  }

  void parseTokens(TLU& t, vector<Token>& tokens) {
    ParseState<Token> ps(tokens);
    // parse many modules
    vector<ModuleAST*> mods =
      many<ModuleAST*>(parseModule, ps);

    cout << "After parsing..." << endl;
    cout << ps.remainder() << endl;
    assert(ps.atEnd());
  }
  
  TLU parseTLU(const std::string& filePath) {
    ifstream f(filePath);
    std::string str((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());    
    TLU t;
    vector<Token> tokens = tokenize(str);
    cout << "Tokens" << endl;
    for (auto t : tokens) {
      cout << t << endl;
    }

    parseTokens(t, tokens);
                          
    return t;
  }


}
