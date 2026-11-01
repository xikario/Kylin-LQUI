#!/usr/bin/env python3
"""Read-only bridge to the installed AI Space knowledge worker."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

# Keep historical Agent command lines working while using one query core.
argv = sys.argv[1:]
legacy = "--status" in argv or "--fetch" in argv or ("--json" in argv and "--query" not in argv)
if legacy:
    old = argparse.ArgumentParser(description="Legacy AI Space read-only query")
    old.add_argument("words", nargs="*")
    old.add_argument("--db")
    old.add_argument("--limit", type=int, default=15)
    old.add_argument("--budget-ms", type=int, default=2000)
    old.add_argument("--status", action="store_true")
    old.add_argument("--fetch")
    old.add_argument("--mode")
    old.add_argument("--json", action="store_true")
    old.add_argument("--body-only", action="store_true")
    known = old.parse_args(argv)
    prefix = ["--db", known.db] if known.db else []
    if known.status:
        argv = prefix + ["stats"]
    elif known.fetch:
        argv = prefix + [
            "fetch",
            "--reference" if known.fetch.startswith("aispace://") else "--path",
            known.fetch,
            "--limit",
            str(known.limit),
        ]
    else:
        argv = (
            prefix
            + [
                "search",
                "--all",
                "--query",
                " ".join(known.words),
                "--limit",
                str(known.limit),
                "--budget-ms",
                str(known.budget_ms),
            ]
            + (["--body-only"] if known.body_only else [])
        )

else:
    argv = [value for value in argv if value != "--json"]

parser = argparse.ArgumentParser()
parser.add_argument("--db", help="Explicit library; otherwise discover AI Space storage settings")
parser.add_argument("command", choices=["stats", "search", "issues", "fetch", "libraries"])
args, rest = parser.parse_known_args(argv)
if "--db" in rest or any(v.startswith("--db=") for v in rest):
    parser.error("place --db before the command")
source = Path(__file__).resolve().parents[3] / "scripts/knowledge_library.py"
candidates = [Path(__file__).with_name("knowledge_library.py"), source]
binary = shutil.which("ai-space")
if binary:
    candidates.append(
        Path(binary).resolve().parent.parent / "libexec/ai-space/knowledge_library.py"
    )
candidates += [
    Path.home() / ".local/libexec/ai-space/knowledge_library.py",
    Path("/usr/local/libexec/ai-space/knowledge_library.py"),
    Path("/usr/libexec/ai-space/knowledge_library.py"),
]
worker = next((p for p in candidates if p.is_file()), None)
if worker is None:
    parser.error("install AI Space or run from its source checkout")


def ai_space_data(config, data):
    """Return True when these XDG roots hold any AI Space settings or library."""
    return any(
        p.exists()
        for p in (
            config / "xikario/ai-space.ini",
            config / "ai-space/libraries.json",
            config / "kylin/ukui-fences.ini",
            data / "ai-space/knowledge/smart-space-knowledge.sqlite",
            data / "ukui-fences/knowledge/smart-space-knowledge.sqlite",
        )
    )


def discovery_environment():
    """Agent sandboxes may point HOME/XDG at a private profile directory.

    Only when nothing selects a library and that profile has no AI Space data
    at all, read the same user's account home instead of reporting no library.
    """
    environment = dict(os.environ)
    if (
        args.db
        or environment.get("AI_SPACE_KNOWLEDGE_DB")
        or environment.get("UKUI_FENCES_KNOWLEDGE_DB")
        or environment.get("AI_SPACE_ACCOUNT_HOME_FALLBACK") == "0"
    ):
        return environment
    home = Path.home()
    config = Path(environment.get("XDG_CONFIG_HOME") or home / ".config")
    data = Path(environment.get("XDG_DATA_HOME") or home / ".local/share")
    try:
        import pwd

        account = Path(pwd.getpwuid(os.getuid()).pw_dir)
    except (ImportError, KeyError, OSError):
        return environment
    account_config, account_data = account / ".config", account / ".local/share"
    if (config, data) == (account_config, account_data) or ai_space_data(config, data):
        return environment
    if not ai_space_data(account_config, account_data):
        return environment
    environment.update(XDG_CONFIG_HOME=str(account_config), XDG_DATA_HOME=str(account_data))
    print(
        "AI空间: %s 下没有AI空间设置，改用账户主目录 %s 的设置；"
        "可用 --db 或 AI_SPACE_KNOWLEDGE_DB 指定库，AI_SPACE_ACCOUNT_HOME_FALLBACK=0 关闭此回退"
        % (config, account),
        file=sys.stderr,
    )
    return environment


sys.exit(
    subprocess.call(
        [sys.executable, str(worker), *(["--db", args.db] if args.db else []), args.command, *rest],
        env=discovery_environment(),
    )
)
