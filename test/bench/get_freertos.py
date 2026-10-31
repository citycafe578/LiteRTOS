#!/usr/bin/env python3
"""下載 FreeRTOS（Arduino_FreeRTOS_Library，MIT 授權）並整理成 testbench 要用的兩份來源：

    test/bench/freertos/kernel_min/     功能對等的最小設定（合作式、靜態配置），跟 LiteRTOS 公平比較
    test/bench/freertos/kernel_stock/   套件原本的設定（搶佔式、有 timer task 與 mutex），一般 Arduino 使用者實際會得到的

只需要執行一次（report.py 發現缺檔時會自動呼叫）。需要 git 與網路。
產生的資料夾已列在 .gitignore，不會進版控。
"""
import shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FR = ROOT / "test" / "bench" / "freertos"
REPO = "https://github.com/feilipu/Arduino_FreeRTOS_Library.git"
COMMIT = "5219bf9d63f8c3ed84956f67a6a731edabed6218"   # 2024-12-17，固定版本，數字才能重現
KEEP = {".c", ".h", ".cpp"}

def run(*cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd)
    if r.returncode != 0:
        sys.exit(f"指令失敗：{' '.join(cmd)}")

def ready():
    return all((FR / d / "tasks.c").exists() and (FR / d / "FreeRTOSConfig.h").exists()
               for d in ("kernel_min", "kernel_stock"))

def main():
    if ready():
        print("FreeRTOS 來源已就緒。")
        return
    if not shutil.which("git"):
        sys.exit("找不到 git。")
    cache = FR / "_src"
    if not (cache / ".git").exists():
        run("git", "clone", REPO, str(cache))
    run("git", "checkout", "-q", COMMIT, cwd=cache)
    for name in ("kernel_min", "kernel_stock"):
        dst = FR / name
        if dst.exists():
            shutil.rmtree(dst)
        dst.mkdir(parents=True)
        for f in (cache / "src").iterdir():
            if f.suffix in KEEP:
                shutil.copy2(f, dst / f.name)
    shutil.copy2(FR / "FreeRTOSConfig.min.h", FR / "kernel_min" / "FreeRTOSConfig.h")
    print("FreeRTOS 來源已整理好：", FR)

if __name__ == "__main__":
    main()
