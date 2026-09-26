#!/usr/bin/env python3
"""Inspect the kungfu profile db (config.db): tables, config columns, rows."""
import sqlite3
import pathlib

db = pathlib.Path.home() / ".config/kungfu/home/runtime/system/etc/kungfu/db/live/config.db"
print("db file:", db, "exists:", db.exists())
conn = sqlite3.connect(str(db))
tables = [r[0] for r in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")]
print("tables:", tables)
for t in tables:
    cols = [r[1] for r in conn.execute(f"PRAGMA table_info({t})")]
    print(f"  {t}: {cols}")
if "config" in tables:
    for row in conn.execute("SELECT * FROM config"):
        print("  row:", row)
conn.close()
