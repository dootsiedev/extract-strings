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

#include "string_tools.h"

static llvm::cl::OptionCategory MyToolCategory("tl-extract");

// for flexibility (no actual use case) I could try to add a argument number,
// or support more than 1 function (but how would I do both without parsing manually?)
static llvm::cl::opt<std::string> ExtractFuncOption(
	"func",
	llvm::cl::desc("the name of the function to extract strings from"),
	llvm::cl::Required,
	llvm::cl::cat(MyToolCategory));

static llvm::cl::opt<bool> Disable_SrcLoc(
	"disable-srcloc",
	llvm::cl::desc("disable source code references to the translations (may affect matching)"),
	llvm::cl::Optional,
	llvm::cl::cat(MyToolCategory),
	llvm::cl::init(false));

static llvm::cl::opt<bool> UseFullPath(
	"full-path",
	llvm::cl::desc("include the full path to the file in the SRC_LOC"),
	llvm::cl::Optional,
	llvm::cl::cat(MyToolCategory),
	llvm::cl::init(false));

using namespace clang;
using namespace clang::tooling;

void printPresumedLoc(clang::SourceManager& SM, clang::SourceLocation loc)
{
	// Check for an invalid location.
	if(loc.isInvalid())
	{
		llvm::errs() << "<invalid loc>\n";
		return;
	}

	// Get the presumed location, which accounts for #line directives.
	clang::PresumedLoc presumed_loc = SM.getPresumedLoc(loc);

	// Check for an invalid presumed location.
	if(presumed_loc.isInvalid())
	{
		llvm::errs() << "<invalid presumed loc>\n";
		return;
	}

	llvm::errs() << presumed_loc.getFilename() << ":" << presumed_loc.getLine() << ":"
				 << presumed_loc.getColumn() << "\n";
}

// https://stackoverflow.com/questions/72571073/how-to-get-the-parent-funtion-name-in-clang-tidy-astmatch-at-callexpr-position
#include "clang/AST/ParentMapContext.h" // clang::DynTypedNodeList
std::string get_caller_function(clang::ASTContext& Context, Expr* expr)
{
	// Get its parent nodes.  The docs do not realy explain why there can
	// be multiple parents, but I think it has to do with C++ templates.
	clang::DynTypedNodeList NodeList = Context.getParents(*expr);
	while(!NodeList.empty())
	{
		// Get the first parent.
		clang::DynTypedNode ParentNode = NodeList[0];

		// You can dump the parent like this to inspect it.
		// ParentNode.dump(llvm::outs(), *(Result.Context));

		// Is the parent a FunctionDecl?
		if(const FunctionDecl* Parent = ParentNode.get<FunctionDecl>())
		{
			// llvm::outs() << "Found ancestor FunctionDecl: " << (void const*)Parent << '\n';
			// llvm::outs() << "FunctionDecl name: " << Parent->getNameAsString() << '\n';
			return Parent->getNameAsString();
		}

		// It was not a FunctionDecl.  Keep going up.
		NodeList = Context.getParents(ParentNode);
	}
	return std::string();
}

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
			llvm::StringRef find_text = "TRANSLATORS:";
			size_t pos = commentText.find(find_text);

			if(pos != llvm::StringRef::npos)
			{
				// llvm::outs() << "Found Comment:" << commentText << "\n";
				// This works, but I don't like how the error messages look without TRANSLATORS:
				// commentText.substr(pos + find_text.size()).ltrim(' ')
				comments.emplace_back(commentText.substr(pos), Comment);
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
				// TODO: this could be a warning... since this is potentially in a macro...
				llvm::errs() << "error: failed to match comment: " << comment.text << "\n";
				printPresumedLoc(SM, comment.range.getBegin());
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
		// llvm::outs() << "Found string: \"" << string << "\"\n";

		//
		clang::SourceLocation loc = Arg->getExprLoc();

		// print the INFO() section.
		if(!Disable_SrcLoc)
		{
			if(loc.isInvalid())
			{
				llvm::errs() << "<invalid loc>\n";
				return false;
			}
			// Get the presumed location, which accounts for #line directives.
			clang::PresumedLoc presumed_loc = SM.getPresumedLoc(loc);

			// Check for an invalid presumed location.
			if(presumed_loc.isInvalid())
			{
				llvm::errs() << "<invalid presumed loc>\n";
				return false;
			}
			const char* trimmed_filename =
				(UseFullPath) ? presumed_loc.getFilename()
							  : remove_file_path(presumed_loc.getFilename());

			// TODO: how hard would it be to also include the function type signature?
			llvm::outs() << "INFO(\"" << get_caller_function(Context, Arg) << "\", \""
						 << trimmed_filename << "\", " << presumed_loc.getLine() << ", "
						 << presumed_loc.getColumn() << ")\n";
		}


		// Get the comment for the function.
		for(auto rit = comments.rbegin(); rit != comments.rend(); ++rit)
		{
			if(SM.isBeforeInTranslationUnit(rit->range.getBegin(), loc))
			{
				if(rit->used)
				{
					break;
				}
				if(rit + 1 != comments.rend())
				{
					if(!(rit + 1)->used)
					{
						// TODO: this could be a warning... since this is potentially in a macro...
						llvm::errs() << "error: comment not consumed!: " << (rit + 1)->text << "\n";
						printPresumedLoc(SM, (rit + 1)->range.getBegin());
						llvm::errs() << "before here: " << rit->text << "\n";
						printPresumedLoc(SM, rit->range.getBegin());
						return false;
					}
				}

				// count the number of newlines.
				// there has to be a better way...
				// I could use presumed loc but I already wrote this.
				llvm::StringRef gapText = clang::Lexer::getSourceText(
					clang::CharSourceRange::getCharRange(rit->range.getEnd(), loc),
					PP.getSourceManager(),
					PP.getLangOpts());
				int i = 0;
				for(char c : gapText)
				{
					if(c == '\n')
					{
						i++;
						// this is a hardcoded 2 line distance.
						if(i >= 3)
						{
							// TODO: this could be a warning... since this is potentially in a
							// macro...
							llvm::errs() << "error: gap between comment and translation too large: "
										 << rit->text << "\n";
							printPresumedLoc(SM, rit->range.getBegin());
							llvm::errs() << "to here: " << string << "\n";
							printPresumedLoc(SM, loc);
							return false;
						}
					}
				}
				rit->used = true;



				std::string escaped_comment_buf;
				llvm::StringRef escaped_comment = rit->text;
				if(escape_string_check_contains(rit->text))
				{
					escape_string(escaped_comment_buf, rit->text);
					escaped_comment = escaped_comment_buf;
				}
				llvm::outs() << "COMMENT(\"" << escaped_comment << "\")\n";
				break;
			}
		}
		llvm::outs() << "TL(\"" << string << "\", NULL)\n\n";
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
		// all the files will have their own TL_START if I did this...
		// and I don't know how to properly close it out
		// (but that's fine, I still need to process this to error check + merge duplicates).
		// llvm::outs() << "TL_START()\n\n";
		if(!Visitor.TraverseDecl(Context.getTranslationUnitDecl()))
		{
			// I want the test to fail.
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
	// wait... why do the comments work without this...?
	// I probably should also disable warnings, but I think it's harmless ATM (performance?).
	// args.push_back("--extra-arg=-fparse-all-comments");

	auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
	if(!ExpectedParser)
	{
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}

	if(!ExtractFuncOption.empty())
	{
		// I use llvm::outs for the output.
		// llvm::outs() << "--func value: " << ExtractFuncOption << "\n";
	}

	CommonOptionsParser& OptionsParser = ExpectedParser.get();

	ClangTool tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

	return tool.run(newFrontendActionFactory<PrintfStringFrontendAction>().get());
}