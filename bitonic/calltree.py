"""Print the Thicket calltree and metadata for one or more Caliper files.

Run on a Grace login node (verified 2026-10-14). The course python binary under
/scratch/group/csce-435-f26/python-3.10.8/bin lacks _ctypes, so use the Python
module and only borrow the course site-packages (Thicket, pandas, numpy):

    module load GCCcore/12.2.0 Python/3.10.8
    python3 calltree.py cali/bitonic-Random-1048576-8-*.cali

Do not pipe `module load` into another command; the shell function then runs in
a subshell and the load is lost. Paste the printed tree into Report.md section 3c.
"""
import sys
from glob import glob

# Course install of Thicket on Grace (must come before `import thicket`).
sys.path.append("/scratch/group/csce-435-f26/python-3.10.8/lib/python3.10/site-packages")

import thicket as th  # noqa: E402

files = []
for pattern in sys.argv[1:] or ["cali/*.cali"]:
    files.extend(sorted(glob(pattern)))
if not files:
    sys.exit("no .cali files matched; usage: python3 calltree.py <file.cali ...>")

tk = th.Thicket.from_caliperreader(files, disable_tqdm=True)

print("# MPI Bitonic Sort")
print(tk.tree(metric_column="Avg time/rank"))
print()
cols = [c for c in ["algorithm", "programming_model", "input_size", "input_type",
                    "num_procs", "scalability", "group_num", "implementation_source"]
        if c in tk.metadata.columns]
print(tk.metadata[cols])
