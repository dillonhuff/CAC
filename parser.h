#pragma once

#include <sstream>

#include "ir.h"

using namespace dbhc;
using namespace std;

namespace CAC {

  enum TokenType {
    TOKEN_TYPE_ID,
    TOKEN_TYPE_NUM,
    TOKEN_TYPE_SYMBOL,
    TOKEN_TYPE_KEYWORD,  
  };

  static inline
  bool isKeyword(const std::string& str) {
    vector<string> keywords{"void", "for", "return", "do", "while"};
    return dbhc::elem(str, keywords);
  }

  class Token {
    std::string str;
    TokenType tp;
  
  public:

    Token() {}
  
    Token(const std::string& str_) : str(str_), tp(TOKEN_TYPE_ID) {
      if (isKeyword(str)) {
        tp = TOKEN_TYPE_KEYWORD;
      }

      if (isdigit(str[0])) {
        tp = TOKEN_TYPE_NUM;
      }
    }
    Token(const std::string& str_,
          const TokenType tp_) : str(str_), tp(tp_) {}

    TokenType type() const { return tp; }
  
    bool isId() const { return type() == TOKEN_TYPE_ID; }
    bool isNum() const { return type() == TOKEN_TYPE_NUM; }
    std::string getStr() const { return str; }
  };

  static inline
  bool operator<(const Token l, const Token r) {
    return l.getStr() < r.getStr();
  }

  static inline
  bool isComparator(Token op) {
    vector<string> comps = {"==", ">", "<", "*", ">=", "<="};
    return dbhc::elem(op.getStr(), comps);
  }

  static inline
  std::ostream& operator<<(std::ostream& out, const Token& t) {
    out << t.getStr();
    return out;
  }

  static inline
  bool operator==(const Token& a, const Token& b) {
    return a.getStr() == b.getStr();
  }

  static inline
  bool operator!=(const Token& a, const Token& b) {
    return !(a == b);
  }

  class LabelAST {
  };

  enum ExprKind{
    EXPR_KIND_ID,
    EXPR_KIND_INT,
    EXPR_KIND_BINOP
  };

  class ExpressionAST {

  public:

    virtual ExprKind getKind() const = 0;
  };

  class IdentifierAST : public ExpressionAST {
    Token name;
  public:
    IdentifierAST(Token t) : name(t) {}

    string getName() const { return name.getStr(); }
    
    static
    bool classof(const ExpressionAST* const expr) {
      return expr->getKind() == EXPR_KIND_ID;
    }

    virtual ExprKind getKind() const override {
      return EXPR_KIND_ID;
    }
    
  };

  class IntegerAST : public ExpressionAST {
    Token name;
  public:
    IntegerAST(Token t) : name(t) {}

    string getName() const { return name.getStr(); }
    static
    bool classof(const ExpressionAST* const expr) {
      return expr->getKind() == EXPR_KIND_INT;
    }

    ExprKind getKind() const {
      return EXPR_KIND_INT;
    }
    
  };

  class BinopAST : public ExpressionAST {
  public:
    BinopAST(ExpressionAST* a, ExpressionAST* b) {}

    static
    bool classof(const ExpressionAST* const expr) {
      return expr->getKind() == EXPR_KIND_BINOP;
    }

    ExprKind getKind() const {
      return EXPR_KIND_BINOP;
    }
    
  };

  class ActivationAST {
  };
  
  enum StmtKind {
    STMT_KIND_GOTO,
    STMT_KIND_ICONNECT,    
    STMT_KIND_BEGIN    
  };

  class StmtAST {
  public:
    LabelAST* label;

    StmtAST() : label(nullptr) {}
    virtual StmtKind getKind() const = 0;
  };

  class InstrAST : public StmtAST {
  public:
  };

  class GotoAST : public InstrAST {
  public:

    vector<ActivationAST*> continuations;
    GotoAST(vector<ActivationAST*>& conts_) : continuations(conts_) {}
    
    virtual StmtKind getKind() const { return STMT_KIND_GOTO; }    
    static bool classof(const StmtAST* const stmt) { return stmt->getKind() == STMT_KIND_GOTO; }        
  };

  class ImpConnectAST : public InstrAST {
  public:

    ExpressionAST* lhs;
    ExpressionAST* rhs;
    ImpConnectAST(ExpressionAST* lhs_, ExpressionAST* rhs_) :
      lhs(lhs_), rhs(rhs_) {}
    virtual StmtKind getKind() const { return STMT_KIND_ICONNECT; }
    static bool classof(const StmtAST* const stmt) { return stmt->getKind() == STMT_KIND_ICONNECT; }    
  };
  
  class BeginAST : public StmtAST {
  public:
    vector<StmtAST*> stmts;

    BeginAST(vector<StmtAST*> stmts_) : stmts(stmts_) {}
    virtual StmtKind getKind() const { return STMT_KIND_BEGIN; }
    static bool classof(const StmtAST* const stmt) { return stmt->getKind() == STMT_KIND_BEGIN; }
  };
  
  class PortAST {
  public:
    bool isInput;
    int width;
    Token id;

    PortAST(bool isInput_, const int width_, const Token id_) :
      isInput(isInput_), width(width_), id(id_) {}

    string getName() const { return id.getStr(); }
  };

  class EventAST {
  };
  
  enum BlockKind {
    BLOCK_KIND_SEQUENTIAL,
  };
  
  class BlockAST {
  public:
    virtual BlockKind getKind() const = 0;
  };

  class SequenceBlockAST : public BlockAST{
  public:

    EventAST* clk;
    EventAST* rst;
    StmtAST* body;
    
    SequenceBlockAST(EventAST* syn_, EventAST* rst_, StmtAST* body_) :
      clk(syn_), rst(rst_), body(body_) {}
    
    virtual BlockKind getKind() const { return BLOCK_KIND_SEQUENTIAL; }


    static bool classof(const BlockAST* const blk) {
      return blk->getKind() == BLOCK_KIND_SEQUENTIAL;
    }
  };
  
  class ModuleAST {
    Token name;

  public:
    vector<PortAST*> ports;
    vector<BlockAST*> blocks;

    ModuleAST(Token n,
              vector<PortAST*>& pts,
              vector<BlockAST*>& blks) : name(n), ports(pts), blocks(blks) {}

    Token getName() const { return name; }
  };

  static inline
  bool oneCharToken(const char c) {
    vector<char> chars = {'{', '}', ';', ')', '(', ',', '[', ']', ':', '-', '&', '+', '=', '>', '<', '*', '.', '%', '@', '!'};
    return dbhc::elem(c, chars);
  }

  template<typename T>
  class ParseState {
    std::vector<T> ts;
    int pos;

  public:

    ParseState(const std::vector<T>& toks) : ts(toks), pos(0) {}

    int currentPos() const { return pos; }
    void setPos(const int position) { pos = position; }

    bool nextCharIs(const Token t) const {
    
      return !atEnd() && (peekChar() == t);
    }

    T peekChar(const int offset) const {
      assert(((int) ts.size()) > (pos + offset));
      return ts[pos + offset];
    }
  
    T peekChar() const { return peekChar(0); }

    T parseChar() {
      assert(((int) ts.size()) > pos);

      T next = ts[pos];
      pos++;
      return next;
    }

    bool atEnd() const {
      return pos == ((int) ts.size());
    }

    int remainderSize() const {
      return ((int) ts.size()) - pos;
    }

    std::string remainder() const {
      stringstream ss;
      for (int i = pos; i < ts.size(); i++) {
        ss << ts.at(i) << " ";
      }
      return ss.str();
      //return ts.substr(pos);
    }
  
  };

  typedef ParseState<char> TokenState;

  class TranslationUnit {
  public:
    vector<ModuleAST*> modules;
  };

  typedef TranslationUnit TLU;

  template<typename OutType, typename TokenType, typename Parser>
  maybe<OutType> tryParse(Parser p, ParseState<TokenType>& tokens) {
    int lastPos = tokens.currentPos();
    //cout << "lastPos = " << lastPos << endl;
  
    maybe<OutType> val = p(tokens);
    if (val.has_value()) {
      return val;
    }

    tokens.setPos(lastPos);

    //cout << "try failed, now pos = " << tokens.currentPos() << endl;
    return maybe<OutType>();
  }

  template<typename OutType, typename SepType, typename TokenType, typename StatementParser, typename SepParser>
  std::vector<OutType>
  sepBtwn0(StatementParser stmt, SepParser sep, ParseState<TokenType>& tokens) {
    std::vector<OutType> stmts;

    maybe<OutType> nextStmt = stmt(tokens);
    if (!nextStmt.has_value()) {
      return {};
    } else {
      stmts.push_back(nextStmt.get_value());
    }

    auto nextSep = tryParse<SepType>(sep, tokens);
    if (!nextSep.has_value()) {
      return stmts;
    }
  
    while (true) {
      maybe<OutType> nextStmt = stmt(tokens);
      assert(nextStmt.has_value());
      stmts.push_back(nextStmt.get_value());
      auto nextSep = tryParse<SepType>(sep, tokens);
      if (!nextSep.has_value()) {
        break;
      }
    }

    return stmts;
  }
  
  template<typename OutType, typename TokenType, typename Parser>
  std::vector<OutType> many(Parser p, ParseState<TokenType>& tokens) {
    std::vector<OutType> stmts;

    while (true) {
      auto res = p(tokens);
      if (!res.has_value()) {
        break;
      }

      stmts.push_back(res.get_value());
    }

    return stmts;
  }
  
  TLU parseTLU(const std::string& str);

  void lowerTLU(Context& c, TLU& t);
}
