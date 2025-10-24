#include <stdio.h>

const char* _T(const char* c) { return c; }

int main()
{
	printf(_T("test!\n")); // T RANSLATORS: a broken comment!

	printf(/* TRANSLATORS: a comment!*/ _T("test %s!\n"), "foo");

	// TRANSLATORS: works (But not if you make the distance 1 more...)
	printf(
		_T("test\nnewline: %s!\n")
		,
		"bar");

	return 0;
}