#!/usr/bin/env python3
"""Build each report with pdfLaTeX, keeping intermediate files temporary."""
from pathlib import Path
import shutil, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
if not shutil.which("pdflatex"):
    raise SystemExit("Install a TeX distribution with pdflatex to build reports.")
for source in sorted(root.rglob("report.tex")):
    with tempfile.TemporaryDirectory() as scratch:
        for _ in range(2):
            result = subprocess.run(["pdflatex", "-halt-on-error", "-interaction=nonstopmode",
                                     "-output-directory=" + scratch, source.name],
                                    cwd=source.parent, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            if result.returncode:
                raise SystemExit(result.stdout)
        log = (Path(scratch) / "report.log").read_text()
        if "Overfull" in log:
            raise SystemExit(str(source) + ": report contains overfull boxes")
        shutil.copy2(Path(scratch) / "report.pdf", source.with_suffix(".pdf"))
        print(source.with_suffix(".pdf").relative_to(root))
