#!/usr/bin/env python3
"""Insert SimNow (CTP) broker configs into the kungfu profile db.

The Config rows are read by kf_cached (profile_bank_) and pushed to kf_md /
kf_td on startup, where broker::TraderService::get_config() picks them up.

Location uname convention (kungfu::yijinjing::location):
    "<category>/<group>/<name>/<mode>"  e.g. "td/ctp/simnow/live"
and its uid = MurmurHash3_x86_32(uname, seed=42).

Usage:
    python3 tools/setup_simnow_config.py --user-id 123456 --password secret
        [--group ctp] [--name simnow]
        [--md-front tcp://180.168.146.187:10212]
        [--td-front tcp://180.168.146.187:10202]

SimNow reference (broker_id 9999, app_id simnow_client_test,
auth_code 0000000000000000):
    第一套 (交易时段, 与实盘一致):
        TD tcp://180.168.146.187:10101   MD tcp://180.168.146.187:10111
    第二套 (7x24, 全天可连, 品种/数据有限):
        TD tcp://180.168.146.187:10202   MD tcp://180.168.146.187:10212
"""
import argparse
import json
import pathlib
import sqlite3
import sys

KUNGFU_HASH_SEED = 42
CATEGORY = {"md": 0, "td": 1, "strategy": 2, "system": 3}  # longfist enums::category
MODE = {"live": 0, "data": 1, "replay": 2, "backtest": 3}  # longfist enums::mode


def murmur3_x86_32(data: bytes, seed: int = KUNGFU_HASH_SEED) -> int:
    """Pure-python MurmurHash3 x86_32, mirrors src/yijinjing/util/MurmurHash3.cpp."""
    c1, c2 = 0xCC9E2D51, 0x1B873593
    h1 = seed & 0xFFFFFFFF
    length = len(data)
    rounded = length & ~3

    for i in range(0, rounded, 4):
        k1 = int.from_bytes(data[i:i + 4], "little")
        k1 = (k1 * c1) & 0xFFFFFFFF
        k1 = ((k1 << 15) | (k1 >> 17)) & 0xFFFFFFFF
        k1 = (k1 * c2) & 0xFFFFFFFF
        h1 ^= k1
        h1 = ((h1 << 13) | (h1 >> 19)) & 0xFFFFFFFF
        h1 = (h1 * 5 + 0xE6546B64) & 0xFFFFFFFF

    k1 = 0
    tail = data[rounded:]
    if len(tail) >= 3:
        k1 ^= tail[2] << 16
    if len(tail) >= 2:
        k1 ^= tail[1] << 8
    if len(tail) >= 1:
        k1 ^= tail[0]
        k1 = (k1 * c1) & 0xFFFFFFFF
        k1 = ((k1 << 15) | (k1 >> 17)) & 0xFFFFFFFF
        k1 = (k1 * c2) & 0xFFFFFFFF
        h1 ^= k1

    h1 ^= length
    h1 ^= h1 >> 16
    h1 = (h1 * 0x85EBCA6B) & 0xFFFFFFFF
    h1 ^= h1 >> 13
    h1 = (h1 * 0xC2B2AE35) & 0xFFFFFFFF
    h1 ^= h1 >> 16
    return h1 & 0xFFFFFFFF


def location_uid(category: str, group: str, name: str, mode: str) -> int:
    uname = f"{category}/{group}/{name}/{mode}"
    return murmur3_x86_32(uname.encode("utf-8")), uname


def profile_db() -> pathlib.Path:
    db = (pathlib.Path.home() / ".config/kungfu/home/runtime/system/etc/kungfu/db/live/config.db")
    if not db.exists():
        sys.exit(f"profile db not found: {db} (start kf_master once to create it)")
    return db


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--group", default="ctp")
    parser.add_argument("--name", default="simnow")
    parser.add_argument("--user-id", required=True, help="SimNow investor id")
    parser.add_argument("--password", required=True, help="SimNow password")
    parser.add_argument("--broker-id", default="9999")
    parser.add_argument("--app-id", default="simnow_client_test")
    parser.add_argument("--auth-code", default="0000000000000000")
    parser.add_argument("--md-front", default="tcp://180.168.146.187:10212",
                        help="7x24 default; trading hours: tcp://180.168.146.187:10111")
    parser.add_argument("--td-front", default="tcp://180.168.146.187:10202",
                        help="7x24 default; trading hours: tcp://180.168.146.187:10101")
    args = parser.parse_args()

    rows = []
    for cat, front in (("md", args.md_front), ("td", args.td_front)):
        uid, uname = location_uid(cat, args.group, args.name, "live")
        value = {
            "front_uri": front,
            "broker_id": args.broker_id,
            "user_id": args.user_id,
            "password": args.password,
            "app_id": args.app_id,
            "auth_code": args.auth_code,
        }
        rows.append((uid, CATEGORY[cat], args.group, args.name, MODE["live"], json.dumps(value)))
        print(f"{uname}  uid={uid:08x}")

    db = profile_db()
    conn = sqlite3.connect(str(db))
    for row in rows:
        conn.execute('INSERT OR REPLACE INTO config (location_uid, category, "group", name, mode, value) '
                     "VALUES (?, ?, ?, ?, ?, ?)", row)
    conn.commit()
    for row in conn.execute('SELECT location_uid, category, "group", name, mode, value FROM config'):
        print("row:", row)
    conn.close()
    print(f"written to {db}")
    print(f"start with: kf_md --group {args.group} --name {args.name} --source ctp")
    print(f"            kf_td --group {args.group} --name {args.name} --source ctp")


if __name__ == "__main__":
    main()
