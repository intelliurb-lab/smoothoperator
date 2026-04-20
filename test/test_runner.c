#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdlib.h>

/* Forward declarations for test suites */
CU_pSuite suite_message(void);
CU_pSuite suite_config(void);
CU_pSuite suite_liquidsoap_client(void);
CU_pSuite suite_ls_controller(void);

int main(void) {
  if (CU_initialize_registry() != CUE_SUCCESS)
    return CU_get_error();

  /* Add test suites */
  suite_message();
  suite_config();
  suite_liquidsoap_client();
  suite_ls_controller();

  /* Run all tests */
  CU_basic_set_mode(CU_BRM_VERBOSE);
  CU_basic_run_tests();

  /* Print summary */
  CU_cleanup_registry();
  return CU_get_error();
}
