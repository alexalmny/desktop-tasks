import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("measure", Path(__file__).parents[1] / "scripts/measure.py")
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class MeasurementTests(unittest.TestCase):
    def test_cpu_fields_with_parentheses_in_process_name(self):
        stat = "123 (desktop (tasks)) S 1 2 3 4 5 6 7 8 9 10 120 30 0 0"
        self.assertEqual(measure.cpu_seconds(stat, 100), 1.5)

    def test_memory_units(self):
        self.assertEqual(measure.kilobytes("Name: tasks\nVmRSS: 123 kB\nPss: 7 kB\nThreads: 3"), {"VmRSS": 125952, "Pss": 7168})


if __name__ == "__main__":
    unittest.main()
