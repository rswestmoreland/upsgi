#include <check.h>
#include "../upsgi.h"


START_TEST(test_upsgi_strncmp)
{
	int result;
        result = upsgi_strncmp("test", 4, "test", 4);
	ck_assert(result == 0);

	result = upsgi_strncmp("test", 4, "tes", 3);
	ck_assert(result == 1);

	result = upsgi_strncmp("tes", 3, "test", 4);
	ck_assert(result == 1);

	result = upsgi_strncmp("aaa", 3, "bbb", 3);
	ck_assert_msg(result < 0, "result: %d", result);

	result = upsgi_strncmp("bbb", 3, "aaa", 3);
	ck_assert_msg(result > 0, "result: %d", result);
}
END_TEST

Suite *check_core_strings(void)
{
	Suite *s = suite_create("upsgi strings");
	TCase *tc = tcase_create("strings");

	suite_add_tcase(s, tc);
	tcase_add_test(tc, test_upsgi_strncmp);
	return s;
}

START_TEST(test_upsgi_opt_set_int)
{
	int result;
	upsgi_opt_set_int("", "true", &result);
	ck_assert(result == 0);

	upsgi_opt_set_int("", "false", &result);
	ck_assert(result == 0);

	upsgi_opt_set_int("", "0", &result);
	ck_assert(result == 0);

	upsgi_opt_set_int("", "60", &result);
	ck_assert(result == 60);

	// When used with "optional_argument", value will be passed as NULL
	upsgi_opt_set_int("", NULL, &result);
	ck_assert(result == 1);
}
END_TEST

Suite *check_core_opt_parsing(void)
{
	Suite *s = suite_create("upsgi opt parsing");
	TCase *tc = tcase_create("opt_parsing");

	suite_add_tcase(s, tc);
	tcase_add_test(tc, test_upsgi_opt_set_int);
	return s;
}

START_TEST(test_upsgi_cron_task_needs_execution_handles_weekday_7_as_sunday)
{
	int result;
	struct tm *t;
	time_t now;

	now = time(NULL);
	t = localtime(&now);
	t->tm_wday= 0;

	result = upsgi_cron_task_needs_execution(t, -1, -1, -1, -1, 0);
	ck_assert(result == 1);

	result = upsgi_cron_task_needs_execution(t, -1, -1, -1, -1, 7);
	ck_assert(result == 1);

	result = upsgi_cron_task_needs_execution(t, -1, -1, -1, -1, 1);
	ck_assert(result == 0);
}
END_TEST

Suite *check_core_cron(void)
{
	Suite *s = suite_create("upsgi cron");
	TCase *tc = tcase_create("cron");

	suite_add_tcase(s, tc);
	tcase_add_test(tc, test_upsgi_cron_task_needs_execution_handles_weekday_7_as_sunday);
	return s;
}

int main(void)
{
	int nf;
	SRunner *r = srunner_create(check_core_strings());
	srunner_add_suite(r, check_core_opt_parsing());
	srunner_add_suite(r, check_core_cron());
	srunner_run_all(r, CK_NORMAL);
	nf = srunner_ntests_failed(r);
	srunner_free(r);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

