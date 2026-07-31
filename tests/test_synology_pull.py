from __future__ import annotations

import importlib.machinery
import importlib.util
import io
import os
import tempfile
import unittest
from email.message import Message
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).parents[1] / "scripts" / "synology-pull"
LOADER = importlib.machinery.SourceFileLoader("synology_pull", str(SCRIPT_PATH))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
assert SPEC is not None
synology_pull = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(synology_pull)


class FakeResponse:
    def __init__(self, payload: bytes, status: int = 200) -> None:
        self._payload = payload
        self.status = status
        self.headers = Message()
        self.headers["Content-Type"] = "application/octet-stream"

    def __enter__(self) -> FakeResponse:
        return self

    def __exit__(self, *args: object) -> None:
        return None

    def getcode(self) -> int:
        return self.status

    def read(self, _size: int = -1) -> bytes:
        payload, self._payload = self._payload, b""
        return payload


class DownloadFileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.client = synology_pull.SynologyClient("https://nas.example.test")

    def test_publishes_already_complete_partial_without_network_request(self) -> None:
        payload = b"complete"
        with tempfile.TemporaryDirectory() as temp_dir:
            target = Path(temp_dir) / "market.dump"
            partial = target.with_name(target.name + ".part")
            partial.write_bytes(payload)

            with mock.patch.object(
                synology_pull.urllib.request,
                "urlopen",
                side_effect=AssertionError("network request was not expected"),
            ):
                result = self.client.download_file(
                    "/share/market.dump",
                    target,
                    size=len(payload),
                    mtime=123,
                )

            self.assertEqual(result, "downloaded")
            self.assertEqual(target.read_bytes(), payload)
            self.assertFalse(partial.exists())
            self.assertEqual(int(target.stat().st_mtime), 123)

    def test_refuses_partial_symlink(self) -> None:
        payload = b"remote"
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            target = root / "market.dump"
            partial = target.with_name(target.name + ".part")
            external = root / "external"
            external.write_bytes(b"")
            partial.symlink_to(external)

            with mock.patch.object(
                synology_pull.urllib.request,
                "urlopen",
                return_value=FakeResponse(payload),
            ):
                with self.assertRaisesRegex(
                    synology_pull.SynologyError, "symlink"
                ):
                    self.client.download_file(
                        "/share/market.dump",
                        target,
                        size=len(payload),
                        mtime=123,
                    )

            self.assertEqual(external.read_bytes(), b"")


class ParserTest(unittest.TestCase):
    def test_url_is_required_without_environment_default(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            parser = synology_pull.build_parser()
            with mock.patch("sys.stderr", new=io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    parser.parse_args(["probe"])

        self.assertEqual(raised.exception.code, 2)

    def test_url_uses_environment_default(self) -> None:
        url = "https://nas.example.test"
        with mock.patch.dict(
            os.environ, {"SYNOLOGY_URL": url}, clear=True
        ):
            args = synology_pull.build_parser().parse_args(["probe"])

        self.assertEqual(args.url, url)


if __name__ == "__main__":
    unittest.main()
