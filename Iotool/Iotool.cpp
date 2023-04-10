#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Refactoring/RecursiveSymbolVisitor.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/AST/Stmt.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseMap.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

using namespace clang::tooling;
using namespace clang;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static llvm::cl::extrahelp MoreHelp("\nMore help text...\n");

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
    ASTContext&ACT;
    MyASTVisitor(ASTContext &A) : ACT(A) {

    }
    std::unordered_map<std::string, int> pointerstore;
    
    bool VisitVarDecl(VarDecl *vardecl) {
        if (vardecl->isFileVarDecl() && vardecl->getType()->isPointerType()) {
            llvm::outs() << "本文件的指针变量，名为: " << vardecl->getNameAsString() << "\n";
            const Expr* varinit = vardecl->getAnyInitializer();//获取指针初始化表达式, *p = exp,获取exp这个表达式，如果没有初始化，就是获得一个空指针
            if (varinit && varinit->isConstantInitializer(ACT, false)) {
                const Type* inittype = varinit->getType().getTypePtr();
                const QualType pointeertype = inittype->getPointeeType();
                if (!pointeertype.isNull()) {
//                    llvm::outs() << vardecl->getNameAsString() << "\n";
                    if (pointeertype->isConstantSizeType()) { //指针指向一个固定大小的值/地址 叫法不知道怎么叫？
                        Expr *ptrexpr = vardecl->getInit();
//                        llvm::outs() << ptrexpr->getStmtClassName() << "\n";
//                        if (ImplicitCastExpr *ice = dyn_cast<ImplicitCastExpr>(ptrexpr)) {
//
//                        }
                        
                        ptrexpr = ptrexpr->IgnoreImplicit();
                        llvm::outs() << ptrexpr->getStmtClassName() << "\n";
                        if (CStyleCastExpr *csce = dyn_cast<CStyleCastExpr>(ptrexpr)) {
                            llvm::outs() << "47行\n";
                            const Expr* subExpr = csce->getSubExpr();
//                            llvm::outs() << subExpr->getStmtClassName() << "\n";
                            if (const ParenExpr *pe = dyn_cast<ParenExpr>(subExpr)) { //有可能被括号包围，取出括号里的变量
                                const Expr* innersubexr = pe->getSubExpr();
                                subExpr = innersubexr;
                            }
                            if (const IntegerLiteral *ill = dyn_cast<IntegerLiteral>(subExpr)) {
                                llvm::APInt intValue = ill->getValue();
//                                llvm::outs() << intValue << "\n";
                                llvm::outs() << "56行\n";
                                pointerstore.insert(std::pair<std::string, int>(vardecl->getNameAsString(), intValue.getLimitedValue()));
                            }
                        }
                    }
                    if (pointeertype->isConstantArrayType()) {//指针指向数组
                        
                    }
                    if (pointeertype->isConstantMatrixType()) { //指针指向多维数组
                        
                    }
                }
            }
        }
        return true;
    }
};
class MyASTConsumer : public ASTConsumer {
public:
    void HandleTranslationUnit(ASTContext &Ctx) override {
        MyASTVisitor MV(Ctx);
        MV.TraverseDecl(Ctx.getTranslationUnitDecl());
        llvm::outs() << "遍历结束！\n";
        if (MV.pointerstore.size() == 0) {
            llvm::outs() << "指针没有初始化过!或没有指针!\n";
            return;
        }
        std::vector<std::pair<std::string, int>> tempmap (MV.pointerstore.begin(), MV.pointerstore.end());
        std::sort(tempmap.begin(), tempmap.end(), [] (const std::pair<std::string, int> &a, const std::pair<std::string, int> &b){
            return a.second < b.second;
        });
        MV.pointerstore.clear();
        for (auto &p : tempmap) {
            llvm::outs() << p.first << " " << p.second << "\n";
            MV.pointerstore[p.first] = p.second;
        }
        int base = tempmap[0].second;
        std::vector<std::vector<std::string>> finalstore;
        finalstore.push_back({tempmap[0].first});
        for (unsigned int i = 1, j = 0; i < tempmap.size(); i++) {
            if (tempmap[i].second <= base + 4) {
                base = tempmap[i].second;
                finalstore[j].push_back(tempmap[i].first);
            }
            else {
                base = tempmap[i].second;
                std::vector<std::string> tempstore;
                tempstore.push_back(tempmap[i].first);
                finalstore.push_back(tempstore);
                j++;
            }
        }
        for (auto &a : finalstore) {
            for (auto &b : a) {
                llvm::outs() << b << " ";
            }
            llvm::outs() << "\n";
        }
        
    }
};
class MyFrontedAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef InFile) override {
        return std::make_unique<MyASTConsumer>();
    }
    
};
int main(int argc, const char **argv) {
  CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<MyFrontedAction>().get());
}
