"""Check result acceptance, not a substitute for actually booting KUnit."""
import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    'dipper_kunit_results', ROOT / 'tools/dipper/kunit/check-results.py')
results = importlib.util.module_from_spec(spec)
spec.loader.exec_module(results)


class KUnitResultsTests(unittest.TestCase):
    def setUp(self):
        counts = dict(tests=14, passed=14, failed=0, crashed=0, skipped=0, errors=0)
        self.result = dict(arch='arm64', misc=counts, test_cases=[], sub_groups=[
            dict(name=results.SUITE, misc=copy.copy(counts), sub_groups=[],
                 test_cases=[dict(name=name, status='PASS') for name in sorted(results.CASES)])])

    def test_accept_complete_run(self):
        self.assertEqual(len(results.checked_cases(self.result)), 14)

    def test_zero_tests_and_error_do_not_pass(self):
        for change in [dict(tests=0, passed=0), dict(errors=1), dict(skipped=1),
                       dict(failed=1), dict(crashed=1)]:
            result = copy.deepcopy(self.result)
            result['misc'].update(change)
            with self.assertRaises(ValueError):
                results.checked_cases(result)

    def test_duplicate_or_failed_case_does_not_pass(self):
        cases = self.result['sub_groups'][0]['test_cases']
        for change in [dict(name=cases[1]['name']), dict(status='SKIP'), dict(status='FAIL')]:
            result = copy.deepcopy(self.result)
            result['sub_groups'][0]['test_cases'][0].update(change)
            with self.assertRaises(ValueError):
                results.checked_cases(result)

    def test_wrong_arch_or_suite_does_not_pass(self):
        result = copy.deepcopy(self.result)
        result['arch'] = 'um'
        with self.assertRaises(ValueError):
            results.checked_cases(result)
        self.result['sub_groups'][0]['name'] = 'unrelated'
        with self.assertRaises(ValueError):
            results.checked_cases(self.result)

    def test_no_vendor_callback_has_initialized_allocation_result(self):
        source = (ROOT / 'mm/page_alloc.c').read_text()
        allocator = source.split('struct page *__alloc_frozen_pages_noprof(')[1]
        init = allocator.index('struct page *page = NULL;')
        hook = allocator.index('trace_android_rvh_try_alloc_pages_gfp(&page,')
        self.assertLess(init, hook)
