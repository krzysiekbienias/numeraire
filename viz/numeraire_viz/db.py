"""Database path resolution (aligned with NUMERAIRE_DB_PATH / repo layout)."""

from __future__ import annotations

import os
import sqlite3
from pathlib import Path
from typing import Any

import pandas as pd

_SCHEMA_MARKER = "sql/schema_v1.sql"
_DEFAULT_DB_NAME = "db.sqlite3"


def _path_has_schema(candidate: Path) -> bool:
    return (candidate / _SCHEMA_MARKER).is_file()


def repo_root() -> Path:
    """
    Numeraire++ repository root (directory containing sql/schema_v1.sql).

    Anchored on this package location (viz/numeraire_viz/…) so Jupyter cwd does not matter.
    Falls back to walking up from cwd when the package is run from an unusual layout.
    """
    anchor = Path(__file__).resolve().parent.parent.parent
    if _path_has_schema(anchor):
        return anchor

    here = Path.cwd().resolve()
    for candidate in (here, *here.parents):
        if _path_has_schema(candidate):
            return candidate
    return anchor


def _maybe_load_repo_dotenv() -> None:
    """Load NUMERAIRE_DB_PATH from repo .env when unset (same key as C++ / scripts)."""
    if os.environ.get("NUMERAIRE_DB_PATH", "").strip():
        return
    env_file = repo_root() / ".env"
    if not env_file.is_file():
        return
    for line in env_file.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        key, sep, value = stripped.partition("=")
        if sep and key.strip() == "NUMERAIRE_DB_PATH":
            os.environ["NUMERAIRE_DB_PATH"] = value.strip()
            return


def _resolve_against_repo(path: str | Path) -> Path:
    p = Path(path).expanduser()
    if p.is_absolute():
        return p.resolve()
    return (repo_root() / p).resolve()


def default_db_path() -> Path:
    _maybe_load_repo_dotenv()
    env = os.environ.get("NUMERAIRE_DB_PATH", "").strip()
    if env:
        return _resolve_against_repo(env)
    return (repo_root() / _DEFAULT_DB_NAME).resolve()


def resolve_db_path(db_path: str | Path | None = None) -> Path:
    if db_path is None:
        return default_db_path()
    return _resolve_against_repo(db_path)


def connect(db_path: str | Path | None = None) -> sqlite3.Connection:
    path = resolve_db_path(db_path)
    if not path.is_file():
        raise FileNotFoundError(
            f"SQLite database not found: {path} "
            f"(repo root: {repo_root()}; set NUMERAIRE_DB_PATH or pass db_path=)"
        )
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def read_sql(
    query: str,
    params: tuple[Any, ...] | dict[str, Any] | None = None,
    *,
    db_path: str | Path | None = None,
) -> pd.DataFrame:
    with connect(db_path) as conn:
        return pd.read_sql_query(query, conn, params=params)
