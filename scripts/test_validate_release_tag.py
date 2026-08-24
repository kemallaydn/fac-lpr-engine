#!/usr/bin/env python3
import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name('validate-release-tag.py')
spec = importlib.util.spec_from_file_location('validate_release_tag', MODULE)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTagTests(unittest.TestCase):
    def write_changelog(self, body: str) -> Path:
        root = tempfile.TemporaryDirectory()
        self.addCleanup(root.cleanup)
        path = Path(root.name) / 'CHANGELOG.md'
        path.write_text(body, encoding='utf-8')
        return path

    def test_valid_semver_and_breaking_abi_section(self):
        path = self.write_changelog('## [1.2.3]\n\n### Breaking ABI\n- None.\n')
        version, body = release.validate('v1.2.3', path)
        self.assertEqual(version, '1.2.3')
        self.assertIn('Breaking ABI', body)

    def test_rejects_non_semver_tag(self):
        path = self.write_changelog('## [1.2.3]\n\n### Breaking ABI\n- None.\n')
        with self.assertRaises(ValueError):
            release.validate('release-1.2.3', path)

    def test_rejects_missing_release_section(self):
        path = self.write_changelog('## [1.2.2]\n\n### Breaking ABI\n- None.\n')
        with self.assertRaises(ValueError):
            release.validate('v1.2.3', path)

    def test_rejects_implicit_abi_status(self):
        path = self.write_changelog('## [1.2.3]\n\n### Added\n- Feature.\n')
        with self.assertRaises(ValueError):
            release.validate('v1.2.3', path)


if __name__ == '__main__':
    unittest.main()
