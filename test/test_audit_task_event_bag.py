from pathlib import Path
from types import SimpleNamespace
import unittest

from scripts.audit_task_event_bag import format_event
from scripts.audit_task_event_bag import read_task_events


class TaskEventBagAuditTest(unittest.TestCase):
    def test_formats_one_stable_line(self):
        event = SimpleNamespace(
            task_id="bag-success",
            state=5,
            error_code=0,
            attempt=1,
            detail="navigation\n  succeeded",
        )

        self.assertEqual(
            format_event(event),
            "task_id=bag-success state=5 error_code=0 attempt=1 "
            "detail=navigation succeeded",
        )

    def test_rejects_missing_bag_directory(self):
        with self.assertRaisesRegex(RuntimeError, "bag directory does not exist"):
            read_task_events(Path("/definitely/missing/task-event-bag"))


if __name__ == "__main__":
    unittest.main()
