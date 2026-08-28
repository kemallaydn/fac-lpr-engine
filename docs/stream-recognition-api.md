# Stateful stream recognition API

FAC LPR Engine keeps ordinary recognition deliberately stateless. `LprPipeline::recognize()` remains the canonical per-frame implementation and its behavior is not changed by temporal recognition.

Stateful recognition is an optional C++ application-layer capability composed above the existing pipeline:

```text
frame -> LprPipeline -> TemporalPlateConsensus -> StablePlateEventFilter
                         \___________________________________________/
                                   RecognitionStreamSession
```

## C++ integration

Consumers that own an already configured `std::shared_ptr<LprPipeline>` may create an independent `RecognitionStreamSession` for each logical stream/camera.

```cpp
fac_lpr::application::RecognitionStreamSession session{pipeline};
auto result = session.recognize(frame);
```

`RecognitionStreamSessionResult` exposes three distinct views of the operation:

- `frame_result`: unchanged ordinary per-frame pipeline output;
- `temporal`: bounded cross-frame consensus state/output;
- `emission`: recognition-level stable event emission / duplicate suppression.

This separation is intentional. Temporal consensus must not conceal a per-frame regression and recognition-level duplicate suppression must not become an access-control rule.

## Ownership and lifecycle

- One session owns one temporal history and one stable-emission cooldown state.
- Different sessions do not share recognition history or suppression state.
- Session methods serialize access to session-owned mutable state.
- `reset()` clears temporal and emission state and rearms timestamp tracking.
- `close()` is idempotent, clears retained state and rejects subsequent recognition/reset work.
- Input images are borrowed only for the duration of a recognition call.
- No raw images are retained by temporal history.
- History size and time lifetime are bounded by `TemporalPlateConsensusConfig`.
- The session does not own RTSP, camera discovery, reconnect logic, persistence, barrier control or authorization.

Frames producing more than one recognition are returned normally in `frame_result` but are marked ambiguous and are not fed into a single-stream temporal consensus state. This prevents unrelated vehicles from being merged without an explicit tracking identity.

## C ABI decision

The existing C ABI v1 remains unchanged.

FAC LPR intentionally does **not** add stream/session fields to `fac_lpr_engine_config_v1`, `fac_lpr_result_v1` or any existing v1 record. Existing symbol names, layouts, enum values, ownership rules and thread-safety semantics therefore remain binary/source compatible.

A C ABI stream extension is deferred until temporal behavior has sufficient sequence-level regression evidence and downstream consumers require the capability. Adding an ABI before the lifecycle/result contract is proven would permanently freeze avoidable design choices.

If a C ABI stream surface is introduced later, it must be additive and independently versioned, using a separate opaque stream handle and new symbols. It must not reinterpret or resize existing v1 structures.

## FAC Access boundary

The stable emission filter suppresses redundant recognition emissions from adjacent frames. It does not decide whether the same vehicle may create another access event, enter again, open a barrier or satisfy authorization rules. Those policies remain the responsibility of FAC Access.
