#!/usr/bin/env python3
"""LiteRTOS 對 FreeRTOS 的資源比較。

用同一個兩 task 的應用（test/bench/size_main.c 與 test/bench/freertos/fr_size_main.c），分別用：
    - 不含 RTOS（對照組）
    - LiteRTOS
    - FreeRTOS 最小設定（合作式、靜態配置、功能對等）
    - FreeRTOS 預設設定（Arduino_FreeRTOS 套件原本的設定：搶佔式、timer task、mutex）
編譯後比較 Flash / RAM。FreeRTOS 的來源會自動下載（需要 git），見 test/bench/get_freertos.py。

用法（在專案根目錄）：
    python test/bench/report.py                 編譯四個環境並報告
    python test/bench/report.py --no-build      只讀已經編好的 .pio/build/*/firmware.elf
    python test/bench/report.py --md out.md     另外把報告存成 Markdown

速度要在板子上量（兩個都燒進去，開序列埠 115200）：
    pio run -e bench    -t upload      LiteRTOS
    pio run -e bench_fr -t upload      FreeRTOS（最小設定）
FreeRTOS 預設設定的 task stack 在 heap 裡，avr-size 看不到：
    pio run -e bench_fr_stock_size -t upload，序列埠會印出一行 heap used=…，用 --stock-heap 帶進來。
"""
import argparse, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UNO_FLASH = 32256      # 扣掉 bootloader 的可用 Flash
UNO_RAM = 2048
ENVS = ["bench_base", "bench_size", "bench_fr_size", "bench_fr_stock_size"]
LITE_GLOBALS = ["taskList", "taskCount", "lastRunIndex", "currentTask", "schedulerSP",
                "criticalNesting", "criticalSavedSREG"]

def find_tool(name, override=None):
    if override:
        return override
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    for ext in ("", ".exe"):
        p = core / "packages" / "toolchain-atmelavr" / "bin" / (name + ext)
        if p.exists():
            return str(p)
    found = shutil.which(name)
    if found:
        return found
    sys.exit(f"找不到 {name}。請先用 pio 編譯一次（會自動安裝工具鏈），或用 --{name} 指定路徑。")

def build():
    subprocess.run([sys.executable, str(ROOT / "test" / "bench" / "get_freertos.py")], check=True)
    pio = shutil.which("pio") or shutil.which("platformio")
    cmd = [pio, "run"] if pio else [sys.executable, "-m", "platformio", "run"]
    for e in ENVS:
        cmd += ["-e", e]
    print("$", " ".join(cmd), flush=True)
    if subprocess.run(cmd, cwd=ROOT).returncode != 0:
        sys.exit("編譯失敗，請先修好編譯錯誤。")

def elf_of(env):
    p = ROOT / ".pio" / "build" / env / "firmware.elf"
    if not p.exists():
        sys.exit(f"找不到 {p}，請先編譯（拿掉 --no-build）。")
    return str(p)

def sizes(avr_size, elf):
    out = subprocess.check_output([avr_size, elf], text=True).strip().splitlines()[-1].split()
    text, data, bss = int(out[0]), int(out[1]), int(out[2])
    return {"flash": text + data, "ram": data + bss}

def symbols(avr_nm, elf):
    out = subprocess.check_output([avr_nm, "-S", "--size-sort", elf], text=True).splitlines()
    syms = {}
    for line in out:
        parts = line.split()
        if len(parts) == 4:
            syms[parts[3]] = int(parts[1], 16)
    return syms

def pct(new, old):
    return f"{100 * (new - old) / old:+.0f}%"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--md", help="另存 Markdown 報告")
    ap.add_argument("--stock-heap", type=int, default=799,
                    help="FreeRTOS 預設設定動態配置的 heap 用量（預設 799 B：模擬器實測，請用板子上印出的 heap used 取代）")
    ap.add_argument("--avr-size"); ap.add_argument("--avr-nm")
    ap.add_argument("--elf-base"); ap.add_argument("--elf-size")
    ap.add_argument("--elf-fr-min"); ap.add_argument("--elf-fr-stock")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    offline = a.elf_base or a.no_build
    if not offline:
        build()
    elfs = {
        "base": a.elf_base or elf_of("bench_base"),
        "lite": a.elf_size or elf_of("bench_size"),
        "frmin": a.elf_fr_min or elf_of("bench_fr_size"),
        "frstock": a.elf_fr_stock or elf_of("bench_fr_stock_size"),
    }
    avr_size, avr_nm = find_tool("avr-size", a.avr_size), find_tool("avr-nm", a.avr_nm)
    s = {k: sizes(avr_size, v) for k, v in elfs.items()}
    s["frstock"]["ram"] += a.stock_heap          # 動態配置的 stack/TCB 在 heap
    b = s["base"]
    d = {k: {"flash": s[k]["flash"] - b["flash"], "ram": s[k]["ram"] - b["ram"]} for k in s if k != "base"}

    L = []
    L.append("# LiteRTOS vs FreeRTOS（同一個 2-task 應用：xTaskDelayUntil 閃燈 + vTaskDelay/taskYIELD）\n")
    L.append("| | Flash (B) | RAM (B) | RTOS 本身 Flash | RTOS 本身 RAM |")
    L.append("|---|---:|---:|---:|---:|")
    L.append(f"| 不含 RTOS | {b['flash']} | {b['ram']} | | |")
    names = {"lite": "**LiteRTOS**", "frmin": "FreeRTOS 最小設定（功能對等）", "frstock": "FreeRTOS 預設設定"}
    for k in ("lite", "frmin", "frstock"):
        L.append(f"| {names[k]} | {s[k]['flash']} | {s[k]['ram']} | {d[k]['flash']} | {d[k]['ram']} |")
    L.append("")
    for k, nm in (("frmin", "FreeRTOS 最小設定"), ("frstock", "FreeRTOS 預設設定")):
        L.append(f"LiteRTOS 相對 {nm}：整個映像 Flash {pct(s['lite']['flash'], s[k]['flash'])}、"
                 f"RAM {pct(s['lite']['ram'], s[k]['ram'])}；RTOS 本身 Flash {pct(d['lite']['flash'], d[k]['flash'])}、"
                 f"RAM {pct(d['lite']['ram'], d[k]['ram'])}。")
    L.append("")
    L.append(f"（Uno 可用 Flash {UNO_FLASH} B、RAM {UNO_RAM} B。FreeRTOS 預設設定的 RAM 含 heap {a.stock_heap} B。）\n")

    # RAM 明細
    sl, sf = symbols(avr_nm, elfs["lite"]), symbols(avr_nm, elfs["frmin"])
    stacks = sum(sl.get(k, 0) for k in ("szStackA", "szStackB"))
    lite_tcb = sum(sl.get(k, 0) for k in ("szTcbA", "szTcbB"))
    lite_glob = sum(sl.get(k, 0) for k in LITE_GLOBALS)
    fr_tcb = sum(sf.get(k, 0) for k in ("szTcbA", "szTcbB"))
    fr_idle = sf.get("idleTcb", 0) + sf.get("idleStack", 0)
    fr_stacks = sum(sf.get(k, 0) for k in ("szStackA", "szStackB"))
    fr_glob = d["frmin"]["ram"] - fr_tcb - fr_idle - fr_stacks
    L.append("## RAM 明細（RTOS 本身，bytes）\n")
    L.append("| 項目 | LiteRTOS | FreeRTOS 最小 |")
    L.append("|---|---:|---:|")
    L.append(f"| 你的 2 個 task 的 stack（大小自己決定） | {stacks} | {fr_stacks} |")
    L.append(f"| 2 個 task 的 TCB | {lite_tcb} | {fr_tcb} |")
    L.append(f"| idle task（TCB + stack，FreeRTOS 一定要有） | 0 | {fr_idle} |")
    L.append(f"| 核心全域變數（清單、計數等） | {lite_glob} | {fr_glob} |")
    L.append(f"| 合計 | {d['lite']['ram']} | {d['frmin']['ram']} |")
    L.append("")
    text = "\n".join(L)
    print(text)
    if a.md:
        Path(a.md).write_text(text + "\n", encoding="utf-8")
        print(f"已存到 {a.md}")

if __name__ == "__main__":
    main()
