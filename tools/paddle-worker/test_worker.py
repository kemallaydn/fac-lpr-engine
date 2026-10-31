"""Worker protocol tests using a fake PaddleOCR model; no model download required."""
from __future__ import annotations

import importlib.util
import json
import threading
import unittest
from http.client import HTTPConnection
from pathlib import Path
from http.server import HTTPServer


path = Path(__file__).with_name("worker.py")
spec = importlib.util.spec_from_file_location("fac_paddle_worker", path)
assert spec is not None and spec.loader is not None
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class FakeModel:
    def __init__(self, text: str = "34 ABL 489", score: float = 0.93):
        self.text = text
        self.score = score
        self.calls = 0

    def predict(self, *, input, batch_size):
        self.calls += 1
        assert input.shape[0] > 0 and batch_size == 1
        return [{"rec_text": self.text, "rec_score": self.score}]


class WorkerTests(unittest.TestCase):
    def test_normalizes_without_guessing_characters(self):
        self.assertEqual(worker.normalize_plate("34 ABL-489"), "34ABL489")
        self.assertEqual(worker.normalize_plate("34 A8L 489"), "34A8L489")

    def test_paddle_prediction_on_image_bytes(self):
        import cv2
        import numpy as np

        model = FakeModel()
        encoded = cv2.imencode(".png", np.zeros((32, 128, 3), dtype=np.uint8))[1]
        result = worker.recognize_crop(model, encoded.tobytes())
        self.assertEqual(result, [{"text": "34ABL489", "confidence": 0.93}])
        self.assertEqual(model.calls, 1)

    def test_http_protocol_and_failure_bounds(self):
        import cv2
        import numpy as np

        model = FakeModel()
        server = HTTPServer(("127.0.0.1", 0), worker.make_handler(model))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        conn = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
        try:
            conn.request("GET", "/health")
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(json.loads(response.read())["status"], "ok")

            payload = cv2.imencode(
                ".png", np.zeros((32, 128, 3), dtype=np.uint8)
            )[1].tobytes()
            conn.request("POST", "/recognize", payload, {"Content-Type": "image/png"})
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(json.loads(response.read())["candidates"][0]["text"], "34ABL489")

            conn.request("POST", "/recognize", b"x", {"Content-Type": "text/plain"})
            response = conn.getresponse()
            self.assertEqual(response.status, 415)
            response.read()
        finally:
            conn.close()
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)


if __name__ == "__main__":
    unittest.main()
