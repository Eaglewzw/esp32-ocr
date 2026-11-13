#!/usr/bin/env python3
"""Validate a complete hardware capture and produce CSV, figures and a report."""

import argparse
import collections
import csv
import json
import math
from pathlib import Path
import statistics

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = ("label", "line15", "line2", "line3")
NAMES = {"label": "完整面单", "line15": "单行15字", "line2": "单行2字", "line3": "单行3字"}
SIZES = (736, 320)
FIELDS = ("pre_us", "model_us", "post_us", "total_us")


def validate(events):
    assert sum(e["type"] == "start" for e in events) == 1, "Missing or duplicate start"
    assert events[-1]["type"] == "done", "Incomplete capture"
    assert not any(e["type"] == "failed" for e in events)
    rows = [e for e in events if e["type"] == "run"]
    assert len(rows) == 208, f"Expected 208 runs including warmup, got {len(rows)}"
    for sample in SAMPLES:
        for size in SIZES:
            group = [r for r in rows if r["sample"] == sample and r["size"] == size]
            measured = [r for r in group if not r["warmup"]]
            assert len(measured) == 20 and sorted(r["run"] for r in measured) == list(range(1, 21))
            for batch in (1, 2):
                batch_rows = [r for r in group if r["batch"] == batch]
                assert len(batch_rows) == 13 and sum(r["warmup"] for r in batch_rows) == 3
            quality = [e for e in events if e["type"] == "quality_done" and e["sample"] == sample and e["size"] == size]
            assert len(quality) == 1
            boxes = [e for e in events if e["type"] == "box" and e["sample"] == sample and e["size"] == size]
            reference = next(r for r in measured if r["run"] == 10)
            assert len(boxes) == reference["boxes"]
    models = [e for e in events if e["type"] == "model"]
    assert len(models) == 24
    for model in models:
        size = model["size"]
        assert model["valid"] and model["input"] == [1, size, size, 3] and model["output"] == [1, size, size, 1]
    for row in rows:
        assert row["core"] == 1 and all(row[key] > 0 for key in FIELDS)
        assert sum(row[key] for key in FIELDS[:3]) <= row["total_us"]
    return rows


def summarize(rows):
    summary = []
    for sample in SAMPLES:
        for size in SIZES:
            group = [r for r in rows if not r["warmup"] and r["sample"] == sample and r["size"] == size]
            record = {"sample": sample, "size": size, "n": len(group)}
            for key in FIELDS:
                values = sorted(r[key] / 1000 for r in group)
                name = key.removesuffix("_us")
                record.update({
                    name + "_mean_ms": statistics.mean(values), name + "_median_ms": statistics.median(values),
                    name + "_p95_ms": values[math.ceil(0.95 * len(values)) - 1],
                    name + "_min_ms": min(values), name + "_max_ms": max(values),
                    name + "_stdev_ms": statistics.stdev(values),
                })
            record["boxes_min"] = min(r["boxes"] for r in group)
            record["boxes_max"] = max(r["boxes"] for r in group)
            for batch in (1, 2):
                record[f"batch{batch}_total_mean_ms"] = statistics.mean(r["total_us"] / 1000 for r in group if r["batch"] == batch)
            summary.append(record)
    return summary


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def draw_boxes(events, output):
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20)
    for sample in SAMPLES:
        path = ROOT / ("main/test_images/pp_ocr_v6.jpg" if sample == "label" else f"main/test_images/benchmark/{sample}.jpg")
        source = Image.open(path).convert("RGB")
        w, h = source.size
        comparison = Image.new("RGB", (w * 2, h + 48), "white")
        for column, size in enumerate(SIZES):
            panel = source.copy()
            draw = ImageDraw.Draw(panel)
            boxes = [e for e in events if e["type"] == "box" and e["sample"] == sample and e["size"] == size]
            for box in boxes:
                points = list(zip(box["points"][::2], box["points"][1::2]))
                draw.line(points + [points[0]], fill="#d92929", width=3)
                x, y = points[0]
                label = str(box["index"] + 1)
                draw.rectangle((x, max(0, y - 24), x + 32, max(0, y - 24) + 24), fill="white")
                draw.text((x + 2, max(0, y - 24)), label, font=font, fill="#d92929")
            comparison.paste(panel, (column * w, 48))
            ImageDraw.Draw(comparison).text((column * w + 12, 12), f"{sample}: DET {size} x {size} | {len(boxes)} boxes", font=font, fill="black")
        comparison.save(output / f"{sample}_boxes.png")
        if sample != "label":
            focused = Image.new("RGB", (w * 2, 208), "white")
            focused.paste(comparison.crop((0, 0, w * 2, 48)), (0, 0))
            focused.paste(comparison.crop((0, 48 + 280, w * 2, 48 + 440)), (0, 48))
            focused.save(output / f"{sample}_boxes_detail.png")


def plot_summary(summary, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(11, 5), constrained_layout=True)
    colors = ("#75b9e7", "#2b6eab", "#e4a151")
    labels = ("Preprocess", "Model", "Postprocess")
    for sample_index, sample in enumerate(SAMPLES):
        for size_index, size in enumerate(SIZES):
            record = next(s for s in summary if s["sample"] == sample and s["size"] == size)
            position = sample_index * 3 + size_index
            bottom = 0
            for key, color, label in zip(FIELDS[:3], colors, labels):
                value = record[key.removesuffix("_us") + "_mean_ms"]
                ax.bar(position, value, bottom=bottom, color=color, width=0.75,
                       label=label if position == 0 else None)
                bottom += value
            ax.text(position, bottom + 15, f"{record['total_mean_ms']:.0f}", ha="center", fontsize=9)
    positions = [i * 3 + j for i in range(4) for j in range(2)]
    ax.set_xticks(positions, [f"{sample}\n{size}" for sample in SAMPLES for size in SIZES])
    ax.set_ylabel("Detection time (ms)")
    ax.set_title("ESP32-P4: 736 vs 320 detection | 20 measured runs per condition")
    ax.legend()
    ax.grid(axis="y", alpha=0.2)
    ax.set_axisbelow(True)
    ax.margins(y=0.12)
    fig.savefig(output / "latency_comparison.png", dpi=170)
    plt.close(fig)


def report(events, summary, output):
    start = next(e for e in events if e["type"] == "start")
    provenance = json.loads((output / "provenance.json").read_text())
    lookup = {(r["sample"], r["size"]): r for r in summary}
    lines = [
        "# ESP32-P4 OCR 检测尺寸实验", "", "以下数值均来自本次板上实测，不使用官方基准或面积比例推算。", "",
        f"采集时间：{provenance['captured_at']}。基线提交：`{provenance['git_head']}`。", "",
        f"CPU {start['cpu_mhz']} MHz；PSRAM {start['psram_mhz']} MHz；优化配置 {start['optimization']}；"
        f"Core {start['core']} 单核、任务优先级 {start['priority']}；芯片修订 {start['chip_revision']}；{start['idf']}。", "",
        "## 方法", "",
        "同一固件、相同 INT8 检测模型；736 使用模型默认形状，320 在构造时覆盖输入。"
        "所有输入/输出形状均通过设备端检查。检测阈值为 0.2，框阈值 0.4，unclip 1.4，最小尺寸 3。", "",
        "完整面单使用原图 750×1170。15/2/3 字样本均为 736×736 白底黑字、Droid Sans Fallback Full 40 px、"
        "起点 (48,320)。测试样本、字体与固件 SHA-256 记录在 provenance.json。", "",
        "每种条件两批各10次，批前预热3次。第一批736→320，第二批320→736；正式160次，预热48次。"
        "JPEG每张仅解码一次；模型加载、Rec、日志输出和屏幕绘制均在检测计时区间之外。"
        "LVGL保持静态测试页面，原有后台任务仍运行。", "",
        "检测总时间在 Det::run() 外测量；三个阶段在函数内计时。P95 使用 nearest-rank，20次中的第19个排序值。"
        "两批各自均值、标准差、最小/最大值见 summary.csv。", "",
        "## 耗时（毫秒）", "",
        "| 样本 | 尺寸 | 前处理均值 | 推理均值 | 后处理均值 | 总耗时均值 | 中位数 | P95 | 框数范围 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for r in summary:
        lines.append(f"| {NAMES[r['sample']]} | {r['size']} | {r['pre_mean_ms']:.3f} | {r['model_mean_ms']:.3f} | "
                     f"{r['post_mean_ms']:.3f} | {r['total_mean_ms']:.3f} | {r['total_median_ms']:.3f} | "
                     f"{r['total_p95_ms']:.3f} | {r['boxes_min']}–{r['boxes_max']} |")
    lines += ["", "## 尺寸对比", "", "| 样本 | 总耗时加速 | 总耗时下降 | 纯模型加速 |", "|---|---:|---:|---:|"]
    for sample in SAMPLES:
        a, b = lookup[(sample, 736)], lookup[(sample, 320)]
        lines.append(f"| {NAMES[sample]} | {a['total_mean_ms']/b['total_mean_ms']:.3f}× | "
                     f"{100*(1-b['total_mean_ms']/a['total_mean_ms']):.2f}% | {a['model_mean_ms']/b['model_mean_ms']:.3f}× |")
    lines += ["", "![阶段耗时对比](latency_comparison.png)", "", "## 文字长度对比", "",
              "| 尺寸 | 15字总耗时 | 2字总耗时 | 3字总耗时 | 15字/2字 | 15字/3字 |", "|---|---:|---:|---:|---:|---:|"]
    for size in SIZES:
        a, b, c = [lookup[(sample, size)]["total_mean_ms"] for sample in ("line15", "line2", "line3")]
        lines.append(f"| {size} | {a:.3f} | {b:.3f} | {c:.3f} | {a/b:.3f}× | {a/c:.3f}× |")
    lines += ["", "## 检测框与辅助识别", "",
              "红框来自第一批第10次正式检测，编号按检测器输出顺序。不同尺寸的编号不保证对应同一块文字。"
              "辅助识别在计时实验之外执行，沿用48×320 INT16 Rec，score≥0.5才为accepted。"
              "原图始终保留，Rec从原图裁剪。框数不能直接当作准确率，需结合图像检查漏检、拆框和合框。", ""]
    for sample in SAMPLES:
        lines += [f"### {NAMES[sample]}", "", f"![{sample}]({sample}_boxes{'_detail' if sample != 'label' else ''}.png)", "",
                  "| Detect尺寸 | 框编号 | 识别文本 | 置信度 | 保留 |", "|---|---:|---|---:|---|"]
        for e in events:
            if e["type"] == "ocr" and e["sample"] == sample:
                text = e["text"].replace("|", "\\|").replace("\n", " ")
                lines.append(f"| {e['size']} | {e['index']+1} | {text} | {e['score']:.4f} | {'是' if e['accepted'] else '否'} |")
        lines.append("")
    lines += ["## 数据文件", "", "- raw_serial.log：完整串口原始输出。", "- events.jsonl：原始结构化事件，包含预热、形状和框坐标。",
              "- runs.csv：208次逐次数据，warmup列区分预热。", "- summary.csv：8个条件的统计值。",
              "- provenance.json、sdkconfig.test、tracked_changes.patch：可复现实验信息。", "",
              "## 适用范围", "", "结果反映当前板卡、固件与这四张样本的检测性能。"
              "总耗时不包含模型首次加载、JPEG解码、Rec和UI；不能直接作为完整OCR吞吐量。"
              "受控文字只有一种字体、字号和背景，不能代表不同距离、光照、倾斜或小字场景。", ""]
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    events = [json.loads(line) for line in (args.capture / "events.jsonl").read_text().splitlines()]
    rows = validate(events)
    summary = summarize(rows)
    write_csv(args.capture / "runs.csv", rows)
    write_csv(args.capture / "summary.csv", summary)
    draw_boxes(events, args.capture)
    plot_summary(summary, args.capture)
    report(events, summary, args.capture)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
