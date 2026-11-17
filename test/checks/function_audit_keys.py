"""
test/checks/function_audit_keys.py: no key of test/function_audit.py's GENERATORS
table is written twice.

A repeated key in a dict literal is accepted by Python and keeps only its last
value, so the table can say one thing twice and the second copy can drift. This
reads the table's literal from the file with ast (nothing of the audit is run)
and fails on every key that appears more than once. Run from the repository
root: python3 test/checks/function_audit_keys.py. Exits 0 when there are none.
"""

import ast
import os
import pathlib
import sys

path = pathlib.Path(os.environ.get("FUNCTION_AUDIT") or pathlib.Path(__file__).resolve().parent.parent / "function_audit.py")
tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))

tables = []
for node in tree.body:
    if isinstance(node, ast.Assign) and any(
        isinstance(target, ast.Name) and target.id == "GENERATORS"
        for target in node.targets
    ):
        if isinstance(node.value, ast.Dict):
            tables.append(node.value)

if len(tables) != 1:
    print(f"expected one GENERATORS literal in {path}, found {len(tables)}")
    sys.exit(1)

seen = {}
repeated = []
for key in tables[0].keys:
    text = ast.dump(key)
    if text in seen:
        repeated.append(ast.unparse(key))
    seen[text] = True

for key in repeated:
    print(f"GENERATORS repeats the key {key}")
sys.exit(1 if repeated else 0)
