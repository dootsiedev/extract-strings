// based off some code written by AI, GLM 4.5 air on openrouter
// I had to edit one thing to get it working.
// The prompt was: LLVM dump C string from all printf functions using ast
// this took hours to get linker flags working (AI was partly to blame), 
// and it required a weird hack for diaguids.lib on windows (https://github.com/llvm/llvm-project/issues/86250)

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/AST/ASTContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

static llvm::cl::OptionCategory MyToolCategory("tl-extract");

// for flexibility (no actual use case) I could try to add a argument number, 
// or support more than 1 function (but how would I do both without parsing manually?)
static llvm::cl::opt<std::string> ExtractFuncOption(
	"func",
	llvm::cl::desc("the name of the function to extract strings from"),
	llvm::cl::Required,
	llvm::cl::cat(MyToolCategory)
);

// no point in parsing it again inside this tool,
// just call the tool again with --extra-arg and use the merging tool.
#if 0
static llvm::cl::list<std::string> IgnoreDefine(
	"second-pass-flags",
	// the 2nd pass lets you get a chance to add macros to expose translations.
	// if you need more than 1 pass, consider rewriting your code with --extra-arg=-DTL_STRING_EXTRACT
	// leaving this empty is faster
	llvm::cl::desc("a second pass might help with translations hidden under a macro define, it might give compiler errors but it should work?"),
	llvm::cl::Optional,
	llvm::cl::cat(MyToolCategory)
);
#endif

static llvm::cl::opt<std::string> OutputOption(
	"o",
	llvm::cl::desc("the output file, prints to stdout if not specified"),
	llvm::cl::Optional,
	llvm::cl::cat(MyToolCategory)
);

using namespace clang;
using namespace clang::tooling;

// TODO: what if I renamed it to extract-strings? no need for the TL suffix because why not?
// TODO: get comments
// TODO: make a function that prints the output with source location and comments
//		should it only be the macro format, 
//		or should I also support a easier to parse format like newline separated or json?

class PrintfStringASTVisitor : public RecursiveASTVisitor<PrintfStringASTVisitor> {
public:
    explicit PrintfStringASTVisitor(ASTContext *Context) : Context(Context) {}

    bool VisitCallExpr(CallExpr *callExpr) {
        if (FunctionDecl *funcDecl = callExpr->getDirectCallee()) {
            if (funcDecl->getNameInfo().getAsString() == ExtractFuncOption) {
                if (callExpr->getNumArgs() > 0) {
                    Expr *firstArg = callExpr->getArg(0);
                    if (StringLiteral *strLit = dyn_cast<StringLiteral>(firstArg->IgnoreImpCasts())) {
                        llvm::outs() << "Found string: \"" << strLit->getString() << "\"\n";
                    }
                }
            }
        }
        return true;
    }

private:
    ASTContext *Context;
};

class PrintfStringASTConsumer : public ASTConsumer {
public:
    PrintfStringASTConsumer(CompilerInstance &CI) : Visitor(&CI.getASTContext()) {}

    virtual void HandleTranslationUnit(ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    }

private:
    PrintfStringASTVisitor Visitor;
};

class PrintfStringFrontendAction : public ASTFrontendAction {
public:
    virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) {
        return std::make_unique<PrintfStringASTConsumer>(CI);
    }
};

int main(int argc, const char **argv)
{
    // add "extra-arg" to change the flags, or try to modify the CompilationDatabase?
	// -fparse-all-comments
	auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!ExpectedParser) {
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}
	
	if (!ExtractFuncOption.empty()) {
		llvm::outs() << "--func value: " << ExtractFuncOption << "\n";
	}
	
	std::string err;
	std::unique_ptr<CompilationDatabase> cd = CompilationDatabase::autoDetectFromSource("compile_commands.json", err);
	if(!cd)
	{
		llvm::errs() << err << '\n';
		return 1;
	}
	
	CommonOptionsParser& OptionsParser = ExpectedParser.get();
	
    ClangTool tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());
    
    return tool.run(newFrontendActionFactory<PrintfStringFrontendAction>().get());
}