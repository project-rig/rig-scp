/**
 * The main executable for the unit test suite.
 */

#include <check.h>

#include "tests.h"

int
main(int argc, char *argv[])
{
	int number_failed;
	
	// We need a single master suite from which the first suite runner is created.
	Suite *master_suite = suite_create("master");
	SRunner *sr = srunner_create(master_suite);
	//srunner_set_fork_status(sr, CK_NOFORK);
	
	// Add all suites
	srunner_add_suite(sr, make_queue_suite());
	srunner_add_suite(sr, make_scp_suite());
	
	// Run the tests
	srunner_run_all(sr, CK_NORMAL);
	
	// Collect results
	number_failed = srunner_ntests_failed(sr);
	
	srunner_free(sr);
	
	return (number_failed == 0) ? 0 : -1;
}
