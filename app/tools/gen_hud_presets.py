# Golden data for tst_hud: the Python app's HUD presets in the export format
import json
import os
import sys

root = os.path.join(os.path.dirname(__file__), "..", "..", "src", "main", "python")
sys.path.insert(0, root)
from hud_presets import PRESETS  # noqa: E402

out = {name: json.loads(fn().to_json()) for name, fn in PRESETS if fn is not None}
json.dump(out, open(sys.argv[1], "w"), indent=1, sort_keys=True)
