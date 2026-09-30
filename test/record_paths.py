"""Regression checks for the local evidence-directory migration."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "artifact"))
from record_paths import record_path


class RecordPaths(unittest.TestCase):
    def test_retained_paths(self):
        with tempfile.TemporaryDirectory(prefix="joggle-record-paths-") as directory:
            root = Path(directory)
            for old, new in (
                (".cache/artifact/result.csv", "local/cache/artifact/result.csv"),
                ("build-study/result.json", "local/legacy-builds/build-study/result.json"),
            ):
                target = root / new
                target.parent.mkdir(parents=True, exist_ok=True)
                target.touch()
                self.assertEqual(record_path(old, root), target)
                self.assertEqual(record_path(root / old, root), target)
                original = root / old
                original.parent.mkdir(parents=True, exist_ok=True)
                original.touch()
                self.assertEqual(record_path(old, root), original)
            self.assertEqual(record_path("paper/missing.csv", root), root / "paper/missing.csv")
            self.assertEqual(record_path("builder/missing.csv", root), root / "builder/missing.csv")
            outside = root.parent / "unrelated.csv"
            self.assertEqual(record_path(outside, root), outside)


if __name__ == "__main__":
    unittest.main()
