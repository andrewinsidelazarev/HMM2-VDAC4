"""Apply the HMM2 port edits to an externally supplied prepared engine tree."""
from hashlib import sha256
import json
from pathlib import Path


_changes = json.loads(Path(__file__).with_name("build159_engine_edits.json").read_text(encoding="utf-8"))


def patch(relative: str, source: str) -> str:
    change = _changes.get(relative)
    if change is None:
        return source
    if sha256(source.encode()).hexdigest() != change["base"]:
        raise ValueError(f"Cannot apply build 159 port edits to {relative}. Check the external engine revision.")

    lines = source.splitlines(keepends=True)
    for start, end, replacement in reversed(change["edits"]):
        lines[start:end] = [replacement]
    result = "".join(lines)
    if sha256(result.encode()).hexdigest() != change["result"]:
        raise ValueError(f"Build 159 engine edit verification failed for {relative}")
    return result
