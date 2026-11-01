#!/usr/bin/env python3
"""Measure only this app under private Xvfb and D-Bus, with no CLI started."""
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build/liquid-cli"


def sample(pid):
    base = Path("/proc") / str(pid)
    fields = (base / "stat").read_text().split(")", 1)[1].split()
    memory = {}
    for line in (base / "smaps_rollup").read_text().splitlines():
        if line.startswith(("Pss:", "Rss:")):
            memory[line.split(":")[0]] = int(line.split()[1]) / 1024
    return {"time": time.monotonic(), "ticks": int(fields[11]) + int(fields[12]), **memory}


def child():
    from PIL import Image, ImageDraw
    with tempfile.TemporaryDirectory(prefix="liquid-cli-resource-") as directory:
        config = Path(directory) / "config"
        (config / "kyfences").mkdir(parents=True)
        wallpaper = Path(directory) / "wallpaper.png"
        image = Image.new("RGB", (1280, 800), "#446b52")
        draw = ImageDraw.Draw(image)
        for x in range(0, 1280, 100):
            draw.rectangle((x, 0, x + 25, 800), fill="#264d3b")
        image.save(wallpaper)
        (config / "kyfences/layout.json").write_text(json.dumps({"wallpaperPath": str(wallpaper)}))
        env = dict(os.environ, XDG_CONFIG_HOME=str(config), QT_QPA_PLATFORMTHEME="")
        log = open(ROOT / "build/resource-app.log", "w")
        app = subprocess.Popen([str(EXE)], env=env, stdout=log, stderr=log)
        rows = []
        try:
            time.sleep(2)
            for label, action in (("collapsed-cold", None), ("expanded", "--show"), ("collapsed-warm", "--hide")):
                if action:
                    subprocess.run([str(EXE), action], env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(2)
                first = sample(app.pid)
                memory = [first["Pss"]]
                for _ in range(16):
                    time.sleep(.5)
                    memory.append(sample(app.pid)["Pss"])
                last = sample(app.pid)
                state = json.loads(subprocess.check_output([str(EXE), "--state"], env=env, stderr=subprocess.DEVNULL, text=True))
                row = {"scene": label, "seconds": round(last["time"] - first["time"], 2),
                       "cpuOneCorePercent": round((last["ticks"] - first["ticks"]) / os.sysconf("SC_CLK_TCK") / (last["time"] - first["time"]) * 100, 3),
                       "pssMiB": round(statistics.median(memory), 2), "rssMiB": round(last["Rss"], 2),
                       "sessionRunning": state["sessionRunning"], "glassBuilds": state["glassBuilds"]}
                rows.append(row)
                print(json.dumps(row), flush=True)
            (ROOT / "build/resource-results.json").write_text(json.dumps({"scope": "UI only, private Xvfb, no CLI", "rows": rows}, indent=2))
        finally:
            app.terminate()
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()
            log.close()


if __name__ == "__main__":
    if "--child" in sys.argv:
        child()
    else:
        subprocess.run(["xvfb-run", "-a", "-s", "-screen 0 1920x1080x24", "dbus-run-session", "--", sys.executable, __file__, "--child"], check=True)
