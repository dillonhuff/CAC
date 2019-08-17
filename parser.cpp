#include "parser.h"

#include <fstream>

using namespace dbhc;

namespace CAC {

  template<typename ResultType, typename InputType>
  ResultType* sc(InputType* tp) {
    return static_cast<ResultType*>(tp);
  }
  
  template<typename ResultType, typename InputType>
  dbhc::maybe<ResultType*> extractM(InputType* tp) {
    if (ResultType::classof(tp)) {
      return sc<ResultType>(tp);
    }

    return dbhc::maybe<ResultType*>();
  }
  
  int precedence(Token op) {
    map<string, int> prec{{"+", 100}, {"==", 99}, {"-", 100}, {"*", 100}, {"<", 99}, {">", 99}, {"<=", 99}, {">=", 99}, {"%", 100}};
    assert(contains_key(op.getStr(), prec));
    return map_find(op.getStr(), prec);
  }
  
  bool isBinop(const Token t) {
    vector<string> binopStrings{"==", "+", "&", "-", "/", "^", "%", "&&", "||", "<=", ">=", "<", ">", "*", "%"};
    return elem(t.getStr(), binopStrings);
  }
  
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

#define exit_end(tokens) if (tokens.atEnd()) { return {}; }
#define try_consume(name, tokens) if (tokens.atEnd()) { return {}; } else if (tokens.peekChar() == Token(name)) { tokens.parseChar(); } else { return {}; }

  maybe<IdentifierAST*> parseId(ParseState<Token>& tokens) {
    Token t = tokens.parseChar();
    if (t.isId()) {
      return new IdentifierAST(t);
    }

    return maybe<IdentifierAST*>();
  }
  
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
      return new PortAST(isInput, 1, name);
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

  maybe<ExpressionAST*>
  parsePrimitiveExpressionMaybe(ParseState<Token>& tokens) {
    exit_end(tokens);
    // //cout << "-- Parsing primitive expression " << tokens.remainder() << endl;

    // if (tokens.nextCharIs(Token("("))) {
    //   tokens.parseChar();

    //   //cout << "Inside parens " << tokens.remainder() << endl;
    //   auto inner = parseExpressionMaybe(tokens);
    //   if (inner.has_value()) {
    //     if (tokens.nextCharIs(Token(")"))) {
    //       tokens.parseChar();
    //       return inner;
    //     }
    //   }
    //   return maybe<Expression*>();
    // }

  
    // auto fCall = tryParse<FunctionCall*>(parseFunctionCall, tokens);
    // if (fCall.has_value()) {
    //   return fCall.get_value();
    // }

    // //cout << "---- Trying to parse method call " << tokens.remainder() << endl;
    // auto mCall = tryParse<Expression*>(parseMethodCall, tokens);
    // if (mCall.has_value()) {
    //   return mCall;
    // }

    // auto faccess = tryParse<Expression*>(parseFieldAccess, tokens);
    // if (faccess.has_value()) {
    //   return faccess;
    // }
    
    // Try parsing a function call
    // If that does not work try to parse an identifier
    // If that does not work try parsing a parenthesis
    auto id = tryParse<IdentifierAST*>(parseId, tokens);
    if (id.has_value()) {
      cout << "Getting id" << endl;
      return id.get_value();
    }

    // //cout << "Expressions = " << tokens.remainder() << endl;
    if (!tokens.atEnd() && tokens.peekChar().isNum()) {
      cout << "Getting integer" << endl;
      return new IntegerAST(tokens.parseChar().getStr());
    }

    return maybe<ExpressionAST*>();
  }

  ExpressionAST* popOperand(vector<ExpressionAST*>& postfixString) {
    assert(postfixString.size() > 0);

    ExpressionAST* top = postfixString.back();
    postfixString.pop_back();
  
    auto idM = extractM<IdentifierAST>(top);
    if (idM.has_value() && isBinop(idM.get_value()->getName())) {
      auto rhs = popOperand(postfixString);
      auto lhs = popOperand(postfixString);
      return new BinopAST(lhs, rhs);
      //return new BinopAST(lhs, idM.get_value()->getName(), rhs);
    }

    return top;
  }
  
  maybe<ExpressionAST*> parseExpression(ParseState<Token>& tokens) {
    cout << "-- Parsing expression " << tokens.remainder() << endl;

    vector<Token> operatorStack;
    vector<ExpressionAST*> postfixString;
  
    while (true) {
      auto pExpr = parsePrimitiveExpressionMaybe(tokens);
      cout << "After primitive expr = " << tokens.remainder() << endl;
      if (!pExpr.has_value()) {
        break;
      }

      cout << "Found expr: "  << pExpr.get_value() << endl;

      postfixString.push_back(pExpr.get_value());
    
      if (tokens.atEnd() || !isBinop(tokens.peekChar())) {
        break;
      }

      Token binop = tokens.parseChar();
      cout << "Binop = " << binop << endl;
      if (!isBinop(binop)) {
        break;
      }

      //cout << "Adding binop " << binop << endl;
      if (operatorStack.size() == 0) {
        //cout << tab(1) << "Op stack empty " << binop << endl;      
        operatorStack.push_back(binop);
      } else if (precedence(binop) > precedence(operatorStack.back())) {
        //cout << tab(1) << "Op has higher precedence " << binop << endl;      
        operatorStack.push_back(binop);
      } else {
        while (true) {
          Token topOp = operatorStack.back();
          operatorStack.pop_back();

          //cout << "Popping " << topOp << " from op stack" << endl;

          postfixString.push_back(new IdentifierAST(topOp));
        
          if ((operatorStack.size() == 0) ||
              (precedence(binop) > precedence(operatorStack.back()))) {
            break;
          }
        }

        operatorStack.push_back(binop);
      }
    }

    if (operatorStack.size() == 0) {
      assert(postfixString.size() == 1);
      return postfixString[0];
    }
    // Pop and print all operators on the stack
    //cout << "Adding ops" << endl;
    // Reverse order of this?
    for (auto op : operatorStack) {
      //cout << tab(1) << "Popping operator " << op << endl;
      postfixString.push_back(new IdentifierAST(op));
    }

    if (postfixString.size() == 0) {
      return maybe<ExpressionAST*>();
    }

    //cout << "Building final value" << endl;
    //cout << "Postfix string" << endl;
    // for (auto s : postfixString) {
    //   cout << tab(1) << *s << endl;
    // }

    ExpressionAST* final = popOperand(postfixString);
    assert(postfixString.size() == 0);
    assert(final != nullptr);

    //cout << "Returning expression " << *final << endl;
    return final;
  }
  
  maybe<ActivationAST*> parseActivation(ParseState<Token>& tokens) {
    try_consume("(", tokens);
    // TODO: Change to expression parsing
    //Token cond = tokens.parseChar();
    auto condM = parseExpression(tokens);
    if (!condM.has_value()) {
      return {};
    }
    try_consume(",", tokens);

    Token dest = tokens.parseChar();
    try_consume(",", tokens);

    Token delay = tokens.parseChar();
    try_consume(")", tokens);    

    return new ActivationAST();
  }

  maybe<LabelAST*> parseLabel(ParseState<Token>& tokens) {
    exit_end(tokens);
    Token name = tokens.parseChar();
    try_consume(":", tokens);
    return new LabelAST();
  }
  
  maybe<InstrAST*> parseInstr(ParseState<Token>& tokens) {
    auto lbl = tryParse<LabelAST*>(parseLabel, tokens);

    cout << "Parsing instr at " << tokens.remainder() << endl;
    exit_end(tokens);
    Token lhs = tokens.parseChar();
    if (lhs == Token("goto")) {
      cout << "parsing goto at " << tokens.remainder() << endl;
      auto activations =
        sepBtwn0<ActivationAST*, Token>(parseActivation, parseComma, tokens);

      try_consume(";", tokens);
      return new GotoAST();
    }

    try_consume("=", tokens);
    // TODO: Change to parse expression
    Token rhs = tokens.parseChar();
    try_consume(";", tokens);    
    return new ImpConnectAST();
  }
  
  maybe<StmtAST*> parseStmt(ParseState<Token>& tokens);
  
  maybe<BeginAST*> parseBegin(ParseState<Token>& tokens) {
    try_consume("begin", tokens);
    auto stmts = many<StmtAST*>(parseStmt, tokens);
    try_consume("end", tokens);

    return new BeginAST(stmts);
  }
  
  maybe<StmtAST*> parseStmt(ParseState<Token>& tokens) {
    cout << "Parsing stmt at " << tokens.remainder() << endl;
    
    auto bM = tryParse<BeginAST*>(parseBegin, tokens);
    if (bM.has_value()) {
      return bM.get_value();
    }


    auto instrM = tryParse<InstrAST*>(parseInstr, tokens);
    if (instrM.has_value()) {
      return instrM.get_value();
    }

    return {};
  }

  maybe<SequenceBlockAST*> parseSequence(ParseState<Token>& tokens) {
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
    auto stmtM = tryParse<StmtAST*>(parseStmt, tokens);
    if (!stmtM.has_value()) {
      return {};
    }

    return new SequenceBlockAST(synchM.get_value(), rstM.get_value(), stmtM.get_value());
  }
  
  maybe<BlockAST*> parseBlock(ParseState<Token>& tokens) {
    auto sBlockM = tryParse<SequenceBlockAST*>(parseSequence, tokens);
    if (sBlockM.has_value()) {
      return sBlockM.get_value();
    }

    return {};
  }
  
  maybe<ModuleAST*> parseModule(ParseState<Token>& tokens) {
    try_consume("module", tokens);
    Token modName = tokens.parseChar();
    try_consume("(", tokens);
    auto ports = sepBtwn0<PortAST*, Token>(parsePortDecl, parseComma, tokens);
    cout << "# of ports = " << ports.size() << endl;
    try_consume(")", tokens);
    try_consume(";", tokens);

    auto body = many<BlockAST*, Token>(parseBlock, tokens);
    try_consume("endmodule", tokens);
    
    return new ModuleAST(modName, ports, body);
  }

  void parseTokens(TLU& t, vector<Token>& tokens) {
    ParseState<Token> ps(tokens);
    // parse many modules
    vector<ModuleAST*> mods =
      many<ModuleAST*>(parseModule, ps);

    for (auto m : mods) {
      t.modules.push_back(m);
    }
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

  class CodeGenState {
  public:
    Context* c;
  };
  
  void genCode(StmtAST* body, Context& c, TLU& t) {
    if (BeginAST::classof(body)) {
      // Get body and generate each statement in it
      auto bst = sc<BeginAST>(body);
      for (auto stmt : bst->stmts) {
        genCode(stmt, c, t);
      }
    } else if (GotoAST::classof(body)) {
      //c->addEmpty();
    } else {
      assert(ImpConnectAST::classof(body));
      //c->addEmpty();      
    }
  }

  void lowerTLU(Context& c, TLU& t) {
    CodeGenState cgo;
    cgo.c = &c;
    for (auto mAST : t.modules) {
      auto m = c.addCombModule(mAST->getName().getStr());
      for (auto pAST : mAST->ports) {
        if (pAST->isInput) {
          m->addInPort(pAST->width, pAST->getName());
        } else {
          m->addOutPort(pAST->width, pAST->getName());
        }
      }

      for (auto blk : mAST->blocks) {
        
        if (SequenceBlockAST::classof(blk)) {
          auto sblk = sc<SequenceBlockAST>(blk);
          StmtAST* body = sblk->body;
          genCode(body, c, t);
        }
      }
    }
  }

}
