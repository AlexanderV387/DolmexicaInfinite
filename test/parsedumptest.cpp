// Temporary: prints the tree an expression is parsed into (DOLMEXICA_PARSE).
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <prism/memorystack.h>
#include <prism/wrapper.h>
#include "mugenassignment.h"
#include "mugenassignmentevaluator.h"

static void dumpAssignment(DreamMugenAssignment* a, int depth) {
	if (!a) { printf("%*sNULL\n", 2 * depth, ""); return; }
	printf("%*stype %d", 2 * depth, "", (int)a->mType);
	switch (a->mType) {
	case MUGEN_ASSIGNMENT_TYPE_NUMBER: printf(" number %d\n", ((DreamMugenNumberAssignment*)a)->mValue); return;
	case MUGEN_ASSIGNMENT_TYPE_STRING: printf(" string '%s'\n", ((DreamMugenStringAssignment*)a)->mValue); return;
	case MUGEN_ASSIGNMENT_TYPE_RAW_VARIABLE: printf(" raw variable '%s'\n", ((DreamMugenRawVariableAssignment*)a)->mName); return;
	case MUGEN_ASSIGNMENT_TYPE_VARIABLE: printf(" variable\n"); return;
	case MUGEN_ASSIGNMENT_TYPE_ARRAY: printf(" array\n"); dumpAssignment(((DreamMugenArrayAssignment*)a)->mIndex, depth + 1); return;
	case MUGEN_ASSIGNMENT_TYPE_NEGATION: case MUGEN_ASSIGNMENT_TYPE_UNARY_MINUS: case MUGEN_ASSIGNMENT_TYPE_BITWISE_INVERSION:
		printf("\n"); dumpAssignment(((DreamMugenDependOnOneAssignment*)a)->a, depth + 1); return;
	case MUGEN_ASSIGNMENT_TYPE_RANGE: printf(" range\n"); dumpAssignment(((DreamMugenRangeAssignment*)a)->a, depth + 1); return;
	case MUGEN_ASSIGNMENT_TYPE_NULL: case MUGEN_ASSIGNMENT_TYPE_FIXED_BOOLEAN: case MUGEN_ASSIGNMENT_TYPE_FLOAT: case MUGEN_ASSIGNMENT_TYPE_STATIC: printf("\n"); return;
	default:
		printf("\n");
		dumpAssignment(((DreamMugenDependOnTwoAssignment*)a)->a, depth + 1);
		dumpAssignment(((DreamMugenDependOnTwoAssignment*)a)->b, depth + 1);
	}
}

TEST(ParseDumpTest, Dump) {
	const char* text = getenv("DOLMEXICA_PARSE");
	if (!text) return;
	initMemoryHandler();
	static MemoryStack stack = createMemoryStack(1024 * 1024);
	setupDreamAssignmentReader(&stack);
	setupDreamAssignmentEvaluator();
	std::string copy(text);
	DreamMugenAssignment* a = parseDreamMugenAssignmentFromString(&copy[0]);
	dumpAssignment(a, 0);
	fflush(stdout);
}
