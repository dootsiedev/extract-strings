// based off some code written by AI, GLM 4.5 air on openrouter
// I had to edit one thing to get it working, and spent a few hours trying to fix linker errors.
// But it is now mostly modified for extracting comments.

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Comment.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

#include "escape_string.h"

static llvm::cl::OptionCategory MyToolCategory("tl-extract");

// for flexibility (no actual use case) I could try to add a argument number,
// or support more than 1 function (but how would I do both without parsing manually?)
static llvm::cl::opt<std::string> ExtractFuncOption(
	"func",
	llvm::cl::desc("the name of the function to extract strings from"),
	llvm::cl::Required,
	llvm::cl::cat(MyToolCategory));

static llvm::cl::opt<std::string> OutputOption(
	"o",
	llvm::cl::desc("the output file, prints to stdout if not specified"),
	llvm::cl::Optional,
	llvm::cl::cat(MyToolCategory));

using namespace clang;
using namespace clang::tooling;

// TODO: what if I renamed it to extract-strings? no need for the TL suffix because why not?
// TODO: get comments
// TODO: make a function that prints the output with source location and comments
//		should it only be the macro format,
//		or should I also support a easier to parse format like newline separated or json?

class PrintfStringASTVisitor : public RecursiveASTVisitor<PrintfStringASTVisitor>, CommentHandler
{
public:

	explicit PrintfStringASTVisitor(CompilerInstance& CI)
	: Context(CI.getASTContext())
	, PP(CI.getPreprocessor())
	, SM(CI.getSourceManager())
	{
		PP.addCommentHandler(this);
	}

	// this does not need to be here, it does not interleave with VisitCallExpr
	bool HandleComment(Preprocessor&, SourceRange Comment) override
	{
		if(SM.isInMainFile(Comment.getBegin()))
		{
			llvm::StringRef commentText = clang::Lexer::getSourceText(
				clang::CharSourceRange::getCharRange(Comment),
				PP.getSourceManager(),
				PP.getLangOpts());
			size_t pos = commentText.find("TRANSLATORS:");
			if(pos != llvm::StringRef::npos)
			{
				llvm::outs() << "Found Comment:" << commentText << "\n";
				comments.emplace_back(commentText, Comment);
			}
		}
		return false;
	}

	bool VisitCallExpr(CallExpr* callExpr)
	{
		if(FunctionDecl* funcDecl = callExpr->getDirectCallee())
		{
			if(funcDecl->getNameInfo().getAsString() == ExtractFuncOption)
			{
				if(callExpr->getNumArgs() > 0)
				{
					Expr* firstArg = callExpr->getArg(0);
					if(StringLiteral* strLit = dyn_cast<StringLiteral>(firstArg->IgnoreImpCasts()))
					{
						return handle_string(firstArg, strLit->getString());
					}
				}
			}
		}
		return true;
	}

	bool check_for_unmatched_comments()
	{
		bool success = true;
		for(auto& comment : comments)
		{
			if(!comment.used)
			{
				// TODO: ask AI if the library has a pretty formatter for errors with source location
				llvm::errs() << "error: failed to match comment: "<< comment.text << "\n";
				success = false;
			}
		}
		return success;
	}

private:
	struct comment_entry
	{
		// emplace back...
		comment_entry(llvm::StringRef text_, clang::SourceRange range_)
		: text(text_)
		, range(range_)
		{
		}
		llvm::StringRef text;
		clang::SourceRange range;
		bool used = false;
	};
	std::vector<comment_entry> comments;

	ASTContext& Context;
	clang::Preprocessor& PP;
	clang::SourceManager& SM;

	bool handle_string(Expr* Arg, llvm::StringRef string)
	{
		std::string str;
		if(escape_string_check_contains(string))
		{
			escape_string(str, string);
			string = str;
		}
		llvm::outs() << "Found string: \"" << string << "\"\n";

		// Get the comment for the function.
		clang::SourceLocation loc = Arg->getExprLoc();
		// Get the comment for the function.
		for(auto rit = comments.rbegin(); rit != comments.rend(); ++rit)
		{
			if(SM.isBeforeInTranslationUnit(rit->range.getBegin(), loc))
			{
				if(rit->used)
				{
					break;
				}
				// count the number of newlines.
				// there has to be a better way...
				llvm::StringRef gapText = clang::Lexer::getSourceText(
					clang::CharSourceRange::getCharRange(rit->range.getBegin(), loc),
					PP.getSourceManager(),
					PP.getLangOpts());
				int i = 0;
				for(char c: gapText)
				{
					if(c == '\n')
					{
						i++;
						// this is a hardcoded 2 line distance.
						if(i >= 3)
						{
							// TODO: ask AI if the library has a pretty formatter for errors with source location
							llvm::errs() << "error: gap between comment and translation too large: "<< rit->text << "\n";
							return false;
						}
					}
				}
				rit->used = true;
				llvm::outs() << "Matched Comment: " << rit->text << "\n";
				break;
			}
		}
		return true;
	}
};

class PrintfStringASTConsumer : public ASTConsumer
{
public:
	explicit PrintfStringASTConsumer(CompilerInstance& CI)
	: Visitor(CI)
	{
	}

	void HandleTranslationUnit(ASTContext& Context) override
	{
		if(!Visitor.TraverseDecl(Context.getTranslationUnitDecl()))
		{
			exit(1);
		}
		if(!Visitor.check_for_unmatched_comments())
		{
			exit(1);
		}
	}

private:
	PrintfStringASTVisitor Visitor;
};

class PrintfStringFrontendAction : public ASTFrontendAction
{
public:
	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override
	{
		return std::make_unique<PrintfStringASTConsumer>(CI);
	}
};

int main(int argc, const char** argv)
{
	std::vector<const char*> args(argv, argv + argc);
	int args_size = args.size();
	args.push_back("-fparse-all-comments");
	// add "extra-arg" to change the flags, or try to modify the CompilationDatabase?
	//
	auto ExpectedParser = CommonOptionsParser::create(args_size, args.data(), MyToolCategory);
	if(!ExpectedParser)
	{
		llvm::outs().flush();
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}

	if(!ExtractFuncOption.empty())
	{
		llvm::outs() << "--func value: " << ExtractFuncOption << "\n";
	}

	CommonOptionsParser& OptionsParser = ExpectedParser.get();

	ClangTool tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

	return tool.run(newFrontendActionFactory<PrintfStringFrontendAction>().get());
}