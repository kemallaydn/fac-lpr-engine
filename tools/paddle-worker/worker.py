#!/usr/bin/env python3
"""Loopback-only PaddleOCR text recognition worker for cropped plate images.

Protocol: GET /health -> {"status":"ok"}; POST /recognize (JPEG/PNG bytes)
-> {"candidates":[{"text":"34ABC123","confidence":0.95}]}.
No camera/video handling, no disk persistence, and no plate logging.
"""
from __future__ import annotations

import argparse
import json
import math
from http.server import BaseHTTPRequestHandler, HTTPServer

MAX_BODY_BYTES = 4 * 1024 * 1024
MAX_IMAGE_PIXELS = 4096 * 4096


def normalize_plate(value: str) -> str:
    # Normalization is deliberately NOT correction. Invalid text is not guessed.
    value = value.upper().replace("İ", "I").replace("ı", "I")
    return "".join(char for char in value if ("A" <= char <= "Z") or ("0" <= char <= "9"))


def recognize_crop(model: object, data: bytes) -> list[dict[str, object]]:
    import cv2
    import numpy as np

    decoded = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_COLOR)
    if decoded is None or decoded.size == 0:
        raise ValueError("image decoding failed")
    height, width = decoded.shape[:2]
    if height <= 0 or width <= 0 or height * width > MAX_IMAGE_PIXELS:
        raise ValueError("image dimensions exceed limit")
    predictions = model.predict(input=decoded, batch_size=1)
    if len(predictions) != 1:
        raise ValueError("unexpected OCR output count")
    result = predictions[0]
    text = result["rec_text"]
    score = float(result["rec_score"])
    if not isinstance(text, str) or not math.isfinite(score) or not 0.0 <= score <= 1.0:
        raise ValueError("invalid OCR output")
    normalized = normalize_plate(text)
    if not normalized or len(normalized) > 16:
        return []
    return [{"text": normalized, "confidence": score}]


def make_handler(model: object):
    class Handler(BaseHTTPRequestHandler):
        server_version = "FacPaddleWorker/1"

        def log_message(self, format_string: str, *args: object) -> None:
            # Do not log request payloads, recognized plate text, or URLs.
            return

        def send_json(self, status: int, payload: dict[str, object]) -> None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path != "/health":
                self.send_json(404, {"error": "not_found"})
                return
            self.send_json(200, {"status": "ok"})

        def do_POST(self) -> None:
            if self.path != "/recognize":
                self.send_json(404, {"error": "not_found"})
                return
            if self.headers.get("Content-Type", "").split(";")[0] not in (
                "image/jpeg", "image/png"
            ):
                self.send_json(415, {"error": "unsupported_media_type"})
                return
            try:
                length = int(self.headers.get("Content-Length", ""))
            except ValueError:
                self.send_json(400, {"error": "invalid_content_length"})
                return
            if not 0 < length <= MAX_BODY_BYTES:
                self.send_json(413, {"error": "invalid_body_size"})
                return
            body = self.rfile.read(length)
            if len(body) != length:
                self.send_json(400, {"error": "short_body"})
                return
            try:
                candidates = recognize_crop(model, body)
            except ValueError:
                self.send_json(422, {"error": "invalid_image_or_ocr_response"})
                return
            except Exception:
                self.send_json(503, {"error": "ocr_inference_failed"})
                return
            self.send_json(200, {"candidates": candidates})

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--model", default="PP-OCRv5_mobile_rec")
    parser.add_argument("--model-dir", default=None)
    parser.add_argument("--device", default="cpu")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("port must be in [1024, 65535]")

    from paddleocr import TextRecognition

    model = TextRecognition(model_name=args.model, model_dir=args.model_dir, device=args.device)
    # Instantiate once; load model before declaring worker ready. Inference is serial,
    # which avoids concurrent Paddle calls and bounds simultaneous inference.
    server = HTTPServer(("127.0.0.1", args.port), make_handler(model))
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
