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
    map<string, int> prec{{"+", 100}, {".", 110}, {"==", 99}, {"-", 100}, {"*", 100}, {"<", 99}, {">", 99}, {"<=", 99}, {">=", 99}, {"%", 100}};
    assert(contains_key(op.getStr(), prec));
    return map_find(op.getStr(), prec);
  }

  bool isBinop(const Token t) {
    vector<string> binopStrings{".", "==", "+", "&", "-", "/", "^", "%", "&&", "||", "<=", ">=", "<", ">", "*", "%"};
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

#define exit_failed(res) if (!res.has_value()) { return {}; }
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
      return new BinopAST(lhs, idM.get_value()->getName(), rhs);
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
    exit_failed(condM);    
    try_consume(",", tokens);

    Token dest = tokens.parseChar();
    try_consume(",", tokens);

    //Token delay = tokens.parseChar();
    auto delayM = parseExpression(tokens);
    exit_failed(delayM);
    try_consume(")", tokens);    

    return new ActivationAST(condM.get_value(), dest, delayM.get_value());
  }

  maybe<LabelAST*> parseLabel(ParseState<Token>& tokens) {
    exit_end(tokens);
    Token name = tokens.parseChar();
    try_consume(":", tokens);
    return new LabelAST(name);
  }

  maybe<GotoAST*> parseGoto(ParseState<Token>& tokens) {
    exit_end(tokens);
    Token lhs = tokens.parseChar();
    if (lhs == Token("goto")) {// Hll
      cout << "parsing goto at " << tokens.remainder() << endl;
      auto activations =
        sepBtwn0<ActivationAST*, Token>(parseActivation, parseComma, tokens);

      try_consume(";", tokens);
      return new GotoAST(activations);
    }

    return {};
  }

  maybe<InstrAST*> parseInstr(ParseState<Token>& tokens) {
    auto gt = tryParse<GotoAST*>(parseGoto, tokens);
    if (gt.has_value()) {
      return gt.get_value();
    }
    cout << "Parsing instr at " << tokens.remainder() << endl;

    exit_end(tokens);
    auto lhsM = tryParse<ExpressionAST*>(parseExpression, tokens);
    exit_failed(lhsM);

    try_consume("=", tokens);
    auto rhsM = tryParse<ExpressionAST*>(parseExpression, tokens);
    exit_failed(rhsM);

    try_consume(";", tokens);
    return new ImpConnectAST(lhsM.get_value(), rhsM.get_value());
  }

  maybe<StmtAST*> parseStmt(ParseState<Token>& tokens);

  maybe<ExternalAST*> parseExternal(ParseState<Token>& tokens) {
    exit_end(tokens);
    try_consume("external", tokens);
    try_consume(";", tokens);
    return new ExternalAST();

  }

  maybe<DefaultAST*> parseDefault(ParseState<Token>& tokens) {
    exit_end(tokens);
    try_consume("default", tokens);
    auto lhs = parseExpression(tokens);
    exit_failed(lhs);
    try_consume("=", tokens);
    auto rhs = parseExpression(tokens);
    exit_failed(rhs);

    try_consume(";", tokens);

    return new DefaultAST(lhs.get_value(), rhs.get_value());
  }

  maybe<BeginAST*> parseBegin(ParseState<Token>& tokens) {
    try_consume("begin", tokens);
    auto stmts = many<StmtAST*>(parseStmt, tokens);
    try_consume("end", tokens);

    return new BeginAST(stmts);
  }

  maybe<InvokeAST*> parseInvoke(ParseState<Token>& tokens) { 
    exit_end(tokens);
    Token rs = tokens.parseChar();
    try_consume(".", tokens);
    Token method = tokens.parseChar();
    try_consume("(", tokens);
    auto args =
      sepBtwn0<ExpressionAST*, Token>(parseExpression, parseComma, tokens);
    try_consume(")", tokens);
    try_consume(";", tokens);

    return new InvokeAST(rs, method, args);
  }

  maybe<StmtAST*> parseStmt(ParseState<Token>& tokens) {
    cout << "Parsing stmt at " << tokens.remainder() << endl;
    auto lblM = tryParse<LabelAST*>(parseLabel, tokens);

    auto iM = tryParse<InvokeAST*>(parseInvoke, tokens);
    if (iM.has_value()) {
      auto s = iM.get_value();
      if (lblM.has_value()) {
        s->label = lblM.get_value();
      }
      return s;
    }

    auto bM = tryParse<BeginAST*>(parseBegin, tokens);
    if (bM.has_value()) {
      auto s = bM.get_value();
      if (lblM.has_value()) {
        s->label = lblM.get_value();
      }
      return s;
    }

    auto instrM = tryParse<InstrAST*>(parseInstr, tokens);
    if (instrM.has_value()) {
      auto s = instrM.get_value();
      if (lblM.has_value()) {
        s->label = lblM.get_value();
      }
      return s;
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
  maybe<ModuleAST*> parseModule(ParseState<Token>& tokens);

  maybe<ResourceAST*> parseResource(ParseState<Token>& tokens) {

    exit_end(tokens);
    Token t = tokens.parseChar();
    exit_end(tokens);
    Token n = tokens.parseChar();
    try_consume(";", tokens);

    return new ResourceAST(t, n);	
  }

  maybe<AssignBlockAST*> parseAssign(ParseState<Token>& tokens) {
    exit_end(tokens);
    try_consume("assign", tokens);
    auto expr = tryParse<ExpressionAST*>(parseExpression, tokens);
    exit_failed(expr);
    try_consume("=", tokens);
    auto rhs = tryParse<ExpressionAST*>(parseExpression, tokens);
    exit_failed(rhs);
    try_consume(";", tokens);

    return new AssignBlockAST(expr.get_value(), rhs.get_value()); 
  } 

  maybe<BlockAST*> parseBlock(ParseState<Token>& tokens) {

    auto aM = tryParse<AssignBlockAST*>(parseAssign, tokens);
    if (aM.has_value()) {
      return aM.get_value();

    }

    auto rM = tryParse<ResourceAST*>(parseResource, tokens);
    if (rM.has_value()) {
      return rM.get_value();
    }

    auto mM = tryParse<ModuleAST*>(parseModule, tokens);
    if (mM.has_value()) {
      return new ModuleBlockAST(mM.get_value());
    }

    auto dE = tryParse<ExternalAST*>(parseExternal, tokens);
    if (dE.has_value()) {
      return dE.get_value();
    }

    auto dM = tryParse<DefaultAST*>(parseDefault, tokens);
    if (dM.has_value()) {
      return dM.get_value();
    }

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
      Module* activeMod;
      CC* lastInstr;
      StmtAST* lastStmt;
      Module* surroundingMod;

      map<string, StmtAST*> labelMap;
      map<StmtAST*, CC*> stmtStarts;
      map<StmtAST*, CC*> stmtEnds;

      CodeGenState() : activeMod(nullptr), lastInstr(nullptr), lastStmt(nullptr) {}

      CC* getStartForLabel(const std::string& name) {
        cout << "Finding label " << name << endl;
        assert(contains_key(name, labelMap));      
        StmtAST* stmt = map_find(name, labelMap);
        assert(contains_key(stmt, stmtStarts));
        return map_find(stmt, stmtStarts);
      }
  };

  int genConstExpression(ExpressionAST* l,
      CodeGenState& c,
      TLU& t) {
    assert(IntegerAST::classof(l));
    auto id = sc<IntegerAST>(l);
    int value = stoi(id->getName());
    return value;
  }

  string genName(ExpressionAST* l, CodeGenState& c, TLU& t) {
    assert(IdentifierAST::classof(l));
    auto id = sc<IdentifierAST>(l);
    return id->getName(); 
  }

  ModuleInstance* genResource(ExpressionAST* l, CodeGenState& c, TLU& t) {
    string rName = genName(l, c, t);
    return c.activeMod->getResource(rName);

  }

  Port genExpression(ExpressionAST* l,
      CodeGenState& c,
      TLU& t) {
    if (IdentifierAST::classof(l)) {
      auto id = sc<IdentifierAST>(l);
      string name = id->getName();
      return c.activeMod->ipt(name);
    } else if (IntegerAST::classof(l)) {
      auto id = sc<IntegerAST>(l);
      int value = stoi(id->getName());
      // TODO: Figure out widths via type inference?
      return c.activeMod->c(1, value);
    } else if (BinopAST::classof(l)) {
      auto bop = sc<BinopAST>(l);
      string op = bop->op;
      cout << "Operand is " << op << endl;

      if (op == ".") {
        ModuleInstance* rName = genResource(bop->a, c, t);
        string ptName = genName(bop->b, c, t);
        return rName->pt(ptName); 
      } else {
        Port lhs = genExpression(bop->a, c, t);
        Port rhs = genExpression(bop->b, c, t);

        if (op == "==") {
          // TODO: Compute width
          auto& context = *(c.activeMod->getContext());
          auto wMod = getWireMod(context, 1);
          auto tmpWire = c.activeMod->freshInstance(wMod, "cmp_tmp");

          auto compMod = addComparator(context, "eq", 1);
          auto cmp =
            c.activeMod->freshInstance(compMod, "cmp");
          auto inv = c.activeMod->addInvokeInstruction(cmp->action("apply"));
          bindByType(inv, cmp);
          inv->bind("in0", lhs);
          inv->bind("in1", rhs);
          inv->bind("out", tmpWire->pt("in"));

          return tmpWire->pt("out");
        }  else {
          cout << "Error: Unsupported binop " << op << endl;
          assert(false);
        }
      }
    } else {		
      cout << "Error: Unsupported expression kind" << endl;
      assert(false);
    }
  }

  void addGotos(StmtAST* body, CodeGenState& c, TLU& t) {
    if (BeginAST::classof(body)) {
      auto bst = sc<BeginAST>(body);
      for (auto stmt : bst->stmts) {
        addGotos(stmt, c, t);
      }
    } else if (GotoAST::classof(body)) {
      auto gts = sc<GotoAST>(body);
      auto gt = map_find(body, c.stmtStarts);
      for (auto act : gts->continuations) {
        Port cond = genExpression(act->cond, c, t);
        int delay = genConstExpression(act->delay, c, t);
        CC* dest = c.getStartForLabel(act->destLabel.getStr());
        gt->continueTo(cond, dest, delay);
      }

    } else {
    }

  }

  void genCode(StmtAST* body, CodeGenState& c, TLU& t) {
    cout << "Generating code" << endl;

    bool startOfSeq = c.lastInstr == nullptr;
    CC* lastStmtEnd = c.lastInstr;
    StmtAST* lastStmt = c.lastStmt;

    CC* fst;
    if (BeginAST::classof(body)) {
      // Get body and generate each statement in it
      auto bst = sc<BeginAST>(body);

      fst = c.activeMod->addEmpty();
      c.stmtStarts[body] = fst;

      c.lastInstr = fst;
      c.lastStmt = body;

      for (auto stmt : bst->stmts) {
        genCode(stmt, c, t);
      }

      auto endB = c.activeMod->addEmpty();
      c.lastInstr = endB;
    } else if (GotoAST::classof(body)) {
      //auto gts = sc<GotoAST>(body);
      auto gt = c.activeMod->addEmpty();
      c.stmtStarts[body] = gt;      
      fst = gt;
      c.lastInstr = gt;

      // Continuations are added after all code has been generated

    } else if (InvokeAST::classof(body)) {
      InvokeAST* inv = sc<InvokeAST>(body);
      map<ExpressionAST*, Port> exprsToPorts;
      string name = inv->name.getStr();
      cout << "Getting invocation for " << name << endl;
      ModuleInstance* m =
        c.activeMod->getResource(name);
      cout << "instance has name " << m->getName() << endl;
      string methodName = m->source->getName() + "_" + inv->method.getStr();
      cout << "Method name = " << methodName << endl;
      Module* methodAction = m->action(inv->method.getStr());
      cout << "Method action has name = " << methodAction->getName() << endl;
      auto invokeCall = c.activeMod->addInvokeInstruction(methodAction);
      //assert(false);
      bindByType(invokeCall, m);
      // Need to bind to arguments
      c.stmtStarts[body] = invokeCall;
      c.lastInstr = invokeCall;
      fst = invokeCall;
    } else {

      cout << "Stmt kind = " << body->getKind() << endl; 
      //assert(body->getKind() == STMT_KIND_GOTO);
      assert(ImpConnectAST::classof(body));
      cout << "Generating codde for imp connect" << endl;
      // Get expressions out and generate code for them?
      auto icSt = sc<ImpConnectAST>(body);
      auto l = icSt->lhs;
      Port lPt = genExpression(l, c, t);      

      auto r = icSt->rhs;
      Port rPt = genExpression(r, c, t);      

      auto ic = c.activeMod->addInstruction(lPt, rPt);
      c.stmtStarts[body] = ic;      
      fst = ic;
      c.lastInstr = ic;
      cout << "Done imp connect" << endl;      
    }

    if (startOfSeq) {
      cout << "Is start of sequence" << endl;
      fst->setIsStartAction(true);
    } else {
      assert(lastStmtEnd != nullptr);
      assert(lastStmt != nullptr);
      assert(fst != nullptr);
      assert(c.activeMod != nullptr);

      if (!GotoAST::classof(lastStmt)) {
        lastStmtEnd->continueTo(c.activeMod->c(1, 1), fst, 0);
      }
    }
    c.lastStmt = body;

    if (body->label != nullptr) {
      cout << "Adding label " << body->label->getName() << " to map" << endl;
      assert(!contains_key(body->label->getName(), c.labelMap));
      c.labelMap[body->label->getName()] = body;
    }

  }

  void lowerTLU(Context& c, TLU& t) {
    CodeGenState cgo;
    cgo.c = &c;
    for (auto mAST : t.modules) {
      auto m = c.addCombModule(mAST->getName().getStr());
      cgo.activeMod = m;
      for (auto pAST : mAST->ports) {
        if (pAST->isInput) {
          m->addInPort(pAST->width, pAST->getName());
        } else {
          m->addOutPort(pAST->width, pAST->getName());
        }
      }
      cgo.activeMod->setVerilogDeclString(mAST->getName().getStr());
      for (auto blk : mAST->blocks) {

        if (SequenceBlockAST::classof(blk)) {
          auto sblk = sc<SequenceBlockAST>(blk);
          StmtAST* body = sblk->body;
          genCode(body, cgo, t);
          addGotos(body, cgo, t);
        } else if (DefaultAST::classof(blk)) {
          auto db = sc<DefaultAST>(blk);
          auto id = db->pt;
          assert(IdentifierAST::classof(id));
          auto ids = sc<IdentifierAST>(id)->getName();
          Port pt = cgo.activeMod->ipt(ids);
          int val = genConstExpression(db->val, cgo, t);
          cout << "Setting default value of " << pt << " to " << val << endl;
          cgo.activeMod->setDefaultValue(pt.getName(), val);
        } else if (ExternalAST::classof(blk)) {
          cgo.activeMod->setPrimitive(true);	
        } else if (AssignBlockAST::classof(blk)) {
          auto asg = sc<AssignBlockAST>(blk);
          Port lhs = genExpression(asg->lhs, cgo, t);
          Port rhs = genExpression(asg->rhs, cgo, t);
          cgo.activeMod->addStructuralConnection(lhs, rhs);
        } else if (ModuleBlockAST::classof(blk)) {

          ModuleBlockAST* mBlock = sc<ModuleBlockAST>(blk);
          ModuleAST* mInternal = mBlock->m;

          // TODO: Create new module with name active_mod_<name>
          // then add all ports on active mod to this module
          // and then: do code generation looking up ports in the
          // containing module as needed?

          Module* ctrl = cgo.activeMod->getContext()->addModule(cgo.activeMod->getName() + "_" + mInternal->getName().getStr()); 
          for (auto pt : cgo.activeMod->getInterfacePorts()) {
            if (pt.isInput) {
              ctrl->addOutPort(pt.width, cgo.activeMod->getName() + "_" + pt.getName());
            } else {
              ctrl->addInPort(pt.width, cgo.activeMod->getName() + "_" + pt.getName());
            }
          }
          
          for (auto pt : mInternal->ports) {
            if (pt->isInput) {
              ctrl->addInPort(pt->width, pt->getName());
            } else {
              ctrl->addOutPort(pt->width, pt->getName());
            }
          }
          ctrl->setVerilogDeclString(ctrl->getName());
          // Add code generation for this module
          cgo.surroundingMod = cgo.activeMod;
          cgo.activeMod = ctrl;

          cgo.activeMod = cgo.surroundingMod;
          cgo.surroundingMod = nullptr;
          cgo.activeMod->addAction(ctrl);
        } else if (ResourceAST::classof(blk)) {
          ResourceAST* r = sc<ResourceAST>(blk);
          string rName = r->typeName.getStr();
          Module* rMod = cgo.activeMod->getContext()->getModule(rName);
          string instName = r->name.getStr();
          m->addInstance(rMod, instName);
        } else {
          assert(false);
        }
      }
      cgo.activeMod = nullptr;
    }
  }

}
