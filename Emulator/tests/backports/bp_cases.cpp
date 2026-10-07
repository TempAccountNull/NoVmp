// Backport cases, one group per area. Filled in per commit as each backport is analysed:
// a case is written first, run against the unpatched branch, and the backport is ported only
// where the case shows the bug (otherwise the commit is recorded as not applicable, with the
// passing case as proof).
#include "bp_harness.hpp"

namespace bp
{
	std::vector<test_case> isa_cases() { return {}; }
	std::vector<test_case> integer_cases() { return {}; }
	std::vector<test_case> avx_cases() { return {}; }
	std::vector<test_case> x87_cases() { return {}; }
	std::vector<test_case> exception_cases() { return {}; }
}
