"""pytest conftest: add this directory to sys.path so tests find the
sibling modules (`lkbt51_stub.py`, etc.) regardless of where pytest is run.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
