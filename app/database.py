"""Convenience re-export module.

This project already keeps DB plumbing under `database/`.
Some services/routes expect `app.database` style imports, so this module
re-exports the same engine/session/base/dependency.
"""

from database.connection import engine, SessionLocal, Base, get_db  # noqa: F401

__all__ = ["engine", "SessionLocal", "Base", "get_db"]
