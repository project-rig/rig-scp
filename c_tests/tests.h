/**
 * Shared header file for test suite.
 */
#ifndef TESTS_H
#define TESTS_H

#include <check.h>

Suite *make_queue_suite(void);
Suite *make_scp_suite(void);
Suite *make_rig_scp_suite(void);

#endif
