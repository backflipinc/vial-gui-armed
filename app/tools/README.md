# Golden data from Vial Armed 1.x

The tests compare the C++ app with what the Python app (1.x) produced: keycodes, KLE layouts, macros, HUD
frames, lighting frames, screen streams, HUD presets. These scripts regenerate that data into `app/tests/data`
and `app/src/keycodes/tables_gen.cpp`. The Python app is gone from this branch, so point them at a 1.x checkout:

```sh
git worktree add ../vial-armed-1.8 v1.8.0
python3 -m venv ../vial-armed-1.8/venv && ../vial-armed-1.8/venv/bin/pip install -r ../vial-armed-1.8/requirements.txt
VIAL_ARMED_1X=../vial-armed-1.8 ../vial-armed-1.8/venv/bin/python app/tools/gen_hud_golden.py
```
