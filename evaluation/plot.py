#!/usr/bin/env /usr/bin/python3
from typing import Iterable
from matplotlib import pyplot as plt
from matplotlib.patches import Patch
import pandas as pd
import numpy as np
import gen_bench_config
gen_bench_config.generate_bench_configs(lazy=True)
from benchmarks import PACKAGE_2_PROGRAMS, find_bench_config
from benchmarking import Bench, SANITIZERS
from itertools import combinations, chain
import sys

BENCH_TO_PLOT = [
    *PACKAGE_2_PROGRAMS['binutils'],
]

FORCE = False


def get_overheads(data: np.ndarray) -> np.ndarray:
    '''
    get the overheads of the data

    Input: M * N * K, M = len(sanitizers), N = len(corpus), K = REPEAT_TIMES
       data[0] is the base
    '''
    return (data[1:] / data[0]) * 100


def sumup_data(overheads: np.ndarray, san_comb: Iterable, sanitizers: list) -> (np.ndarray, str):
    '''
    extend the data to include the new 'sanitizer', e.g.,, ASan + TSan + MSan + UBSan
    1. Find the indices of the sanitizers in the sanitizers list
    2. Compute the overhead of the new 'sanitizer' as the sum of the overheads of the sanitizers in the sanitizer combination
    3. Concatenate the new 'sanitizer' data
    '''
    indices = [sanitizers.index(san) for san in san_comb]
    if overheads.ndim == 2:
        # Handle N * M case
        new_overhead = np.sum(overheads[:, indices], axis=1)
    elif overheads.ndim == 1:
        # Handle M case
        new_overhead = np.sum(overheads[indices])
    else:
        raise ValueError("Overheads dimension not supported: {}".format(overheads.shape))
    return new_overhead, '+'.join(san_comb)


def get_xsan_data(overheads: np.ndarray, san_comb: Iterable, sanitizers: list) -> (np.ndarray, str):
    '''
    get the data of the xsan
    '''
    name = 'xsan-' + '-'.join(san_comb)
    index = sanitizers.index(name)
    if overheads.ndim == 2:
        overhead = overheads[:, index]
    elif overheads.ndim == 1:
        overhead = overheads[index]
    else:
        raise ValueError("Overheads dimension not supported: {}".format(overheads.shape))
    return overhead, name


def plot_figure(is_time: bool):
    '''
    Plot the figure of the overheads of the sanitizers on the benchmarks
    Args:
        is_time: bool, if True, plot the figure of the time overheads of the sanitizers on the benchmarks
            if False, plot the figure of the RSS overheads of the sanitizers on the benchmarks
    '''
    overheads = []
    sanitizers = SANITIZERS.copy()
    sanitizers.remove('raw')
    group = "time" if is_time else "rss"
    for bench in BENCH_TO_PLOT:
        config = find_bench_config(bench)
        bench = Bench(config)
        data = bench.load_overhead_batchly(sanitizers, group, depth=2)
        draw_per_bench_figures(data, sanitizers, bench, is_time)
        overheads.append(data)

    overheads = np.array(overheads)
    draw_overall_figures(overheads, sanitizers,
                         ignore_small=False, is_rss=not is_time)
    # Draw Sum vs XSan comparison charts
    draw_overall_compare_figure(overheads, sanitizers, is_time)


def draw_per_bench_figures(overheads: np.ndarray, sanitizers: list, bench: Bench, is_time: bool):
    '''
    Draw the bar chart of the overhead of the sanitizers on the benchmark
    Args:
        overhead: np.ndarray, the overhead of the sanitizers on the benchmark
        sanitizers: list, the sanitizers
        bench: Bench, the benchmark
    '''
    name = 'time' if is_time else 'rss'
    bar_fig_path = bench.get_data_file(f'overhead_{name}.png')
    cmp_fig_path = bench.get_data_file(f'compare_{name}.png')

    def draw_bar():
        M = len(sanitizers)
        x_indices = np.arange(M)
        bar_width = 0.8

        # 创建 figure，分三行：标题 / 图例 / 主图
        fig = plt.figure(figsize=(12, 6))
        gs = fig.add_gridspec(3, 1, height_ratios=[
                              0.08, 0.14, 1.0], hspace=0.05)

        # 标题行
        ax_title = fig.add_subplot(gs[0])
        ax_title.axis("off")
        title_text = '{} Overheads of Sanitizers on ${}$'.format(
            "CPU" if is_time else "Memory", bench.program)
        ax_title.text(0.5, 0.5, title_text,
                      ha="center", va="center",
                      fontsize=14, fontweight="bold")

        # 图例行
        ax_legend = fig.add_subplot(gs[1])
        ax_legend.axis("off")

        # 主图
        ax = fig.add_subplot(gs[2])

        linewidth = 0.4

        # 生成每个柱子的不同颜色
        cmap = plt.get_cmap("tab20")
        colors = [cmap(i % 20) for i in range(M)]

        # 绘制每个柱子，分别指定颜色
        bars = ax.bar(
            x_indices,
            overheads,
            width=bar_width,
            color=colors,
            label=sanitizers,
            alpha=0.9,
            edgecolor="black",
            linewidth=linewidth
        )

        # 在每个柱子上方标注具体数值
        for rect, value in zip(bars, overheads):
            height = rect.get_height()
            ax.text(
                rect.get_x() + rect.get_width() / 2,
                height + 1,  # 数值稍微高于柱子顶部
                f'{value:.2f}',
                ha='center',
                va='bottom',
                fontsize=9
            )

        # 设置 x 轴
        ax.set_xticks(x_indices)
        ax.set_xticklabels([])

        ax.set_xlabel("Benchmarks")
        ax.set_ylabel("Overhead (%)")

        # baseline
        ax.axhline(y=100, color="red", linestyle="--", linewidth=1)

        # 在单独的 legend 行绘制图例
        ncol = min(6, M)  # 一行放几个图例
        ax_legend.legend(
            handles=bars,
            labels=sanitizers,
            loc="center",
            ncol=ncol,
            frameon=False,
            fontsize=9
        )

        # 保存
        plt.savefig(bar_fig_path, dpi=300, bbox_inches='tight')
        plt.close(fig)

    def draw_compare_figure():
        # --- prepare sanitizer combinations (asan + powerset of other san) ---
        san_comb = list(chain.from_iterable(
            combinations(('msan', 'tsan', 'ubsan'), r)
            for r in range(0, len(sanitizers) + 1)
        ))
        san_comb = [('asan', *comb) for comb in san_comb]

        data_pair_to_compare = []
        san_pair_to_compare = []
        for comb in san_comb:
            sum_overhead, new_san = sumup_data(overheads, comb, sanitizers)
            xsan_overhead, xsan_name = get_xsan_data(
                overheads, comb, sanitizers)
            data_pair_to_compare.append(
                (np.asarray(sum_overhead), np.asarray(xsan_overhead)))
            san_pair_to_compare.append((new_san, xsan_name))

        # shared values
        M = len(data_pair_to_compare)

        x_indices = np.arange(M)
        group_width = 0.9
        pair_width = group_width
        bar_width = pair_width * 0.5

        fig = plt.figure(figsize=(8, 6))
        gs = fig.add_gridspec(3, 1, height_ratios=[
                              0.06, 0.16, 1.0], hspace=0.02)

        # title
        ax_title = fig.add_subplot(gs[0])
        ax_title.axis('off')
        title_text = f'{"CPU" if is_time else "Memory"} Overhead Comparison on ${bench.program}$: Sum vs XSan'
        ax_title.text(0.5, 0.5, title_text, ha='center',
                      va='center', fontsize=16, fontweight='bold')

        # legend row
        ax_legend = fig.add_subplot(gs[1])
        ax_legend.axis('off')

        # main axis
        ax = fig.add_subplot(gs[2])

        cmap = plt.get_cmap('tab20')
        colors = [cmap(i % 20) for i in range(M)]
        legend_handles = []
        linewidth = 0.4

        # 用于存储bar容器，方便后续标注
        sum_bars = []
        xsan_bars = []

        for i, ((sum_data, xsan_data), (sum_name, xsan_name)) in enumerate(
            zip(data_pair_to_compare, san_pair_to_compare)
        ):
            # sum bar
            bar1 = ax.bar(
                x_indices[i] - bar_width / 2,
                sum_data,
                width=bar_width,
                color=colors[i],
                edgecolor='black',
                linewidth=linewidth,
                zorder=3,
                alpha=0.8  # 设置透明度为0.8
            )
            sum_bars.append(bar1)

            # xsan bar
            bar2 = ax.bar(
                x_indices[i] + bar_width / 2,
                xsan_data,
                width=bar_width,
                color=colors[i],
                edgecolor='black',
                linewidth=linewidth,
                hatch='///',
                zorder=3,
                alpha=0.8  # 设置透明度为0.8
            )
            xsan_bars.append(bar2)

            legend_handles.append(Patch(facecolor=colors[i],
                                        edgecolor='black',
                                        linewidth=linewidth,
                                        label=f'{sum_name} (Sum)'))
            legend_handles.append(Patch(facecolor=colors[i],
                                        edgecolor='black',
                                        hatch='///',
                                        linewidth=linewidth,
                                        label=f'{sum_name} (XSan)'))

        # 在每个柱子上方标注具体数值
        for i in range(M):
            # 合并 sum_bars 和 xsan_bars 的循环
            for rect in zip(sum_bars[i], xsan_bars[i]):
                # (Sum-Bar, XSan-Bar)
                for r in rect:
                    height = r.get_height()
                    ax.text(
                        r.get_x() + r.get_width() / 2,
                        height + 1,  # 数值稍微高于柱子顶部
                        f'{height:.2f}',
                        ha='center',
                        va='bottom',
                        fontsize=8
                    )

        # ticks & labels
        ax.set_xticks(x_indices)
        ax.set_xticklabels([])
        ax.margins(x=0.01)

        # baseline
        ax.axhline(y=100, color='red', linestyle='--', linewidth=1, zorder=1)

        ax.set_ylabel("Overhead (%)")

        # legend
        if legend_handles:
            ncol = min(4, max(1, len(legend_handles)))
            ax_legend.legend(handles=legend_handles,
                             loc='center',
                             ncol=ncol,
                             frameon=False,
                             handlelength=1.2,
                             fontsize=9)

        fig.subplots_adjust(top=0.93, left=0.06, right=0.94, bottom=0.08)

        plt.savefig(cmp_fig_path, dpi=300, bbox_inches='tight')
        plt.close(fig)

    if FORCE or not bar_fig_path.exists():
        draw_bar()
    if FORCE or not cmp_fig_path.exists():
        draw_compare_figure()


def draw_overall_compare_figure(overheads: np.ndarray, sanitizers: list, is_time: bool):
    """
    Draw comparison figures (bar + violin) for Sum vs XSan overheads.
    Saves two files:
      - compare_sum_vs_xsan_{time|rss}.png   (bar chart)
      - compare_sum_vs_xsan_violin_{time|rss}.png  (violin plot)
    """
    # --- prepare sanitizer combinations (asan + powerset of other san) ---
    san_comb = list(chain.from_iterable(
        combinations(('msan', 'tsan', 'ubsan'), r)
        for r in range(0, len(sanitizers) + 1)
    ))
    san_comb = [('asan', *comb) for comb in san_comb]

    data_pair_to_compare = []
    san_pair_to_compare = []
    for comb in san_comb:
        sum_overhead, new_san = sumup_data(overheads, comb, sanitizers)
        xsan_overhead, xsan_name = get_xsan_data(overheads, comb, sanitizers)
        data_pair_to_compare.append(
            (np.asarray(sum_overhead), np.asarray(xsan_overhead)))
        san_pair_to_compare.append((new_san, xsan_name))

    mlen = max(len(bench) for bench in BENCH_TO_PLOT)
    for (new_san, xsan_name), (sum_overhead, xsan_overhead) in zip(san_pair_to_compare, data_pair_to_compare):
        speedups = 1 - (xsan_overhead / sum_overhead)
        print(f'{new_san}')
        for bench, speedup in zip(BENCH_TO_PLOT, speedups):
            print(f'\t{bench:{mlen}s} : {speedup * 100:8.2f}%')

        # shared values
    N = len(BENCH_TO_PLOT)
    M = len(data_pair_to_compare)

    def draw_bar():
        group_width = 0.8
        pair_width = group_width / max(M, 1)
        bar_width = pair_width * 0.45

        # 子图排布：每行最多 5 个
        ncols = 5
        nrows = int(np.ceil(N / ncols))

        # 整个 figure 大小根据行列数缩放
        fig_width = 3 * ncols
        fig_height = 2.5 * nrows + 2  # +2 留给标题和legend
        fig = plt.figure(figsize=(fig_width, fig_height))
        gs = fig.add_gridspec(nrows + 2, ncols,  # +2 行用于 title + legend
                              height_ratios=[0.1, 0.3] + [1.0] * nrows,
                              hspace=0.35, wspace=0.25)

        # --- title ---
        ax_title = fig.add_subplot(gs[0, :])
        ax_title.axis("off")
        title_text = f'{"CPU" if is_time else "Memory"} Overhead Comparison: Sum vs XSan'
        ax_title.text(0.5, 0.5, title_text, ha="center",
                      va="center", fontsize=16, fontweight="bold")

        # --- legend row ---
        ax_legend = fig.add_subplot(gs[1, :])
        ax_legend.axis("off")

        cmap = plt.get_cmap("tab20")
        colors = [cmap(i % 20) for i in range(M)]
        linewidth = 0.4

        # --- 逐 benchmark 绘制 ---
        for bi, bench in enumerate(BENCH_TO_PLOT):
            row = bi // ncols + 2   # 前两行是 title+legend
            col = bi % ncols
            ax = fig.add_subplot(gs[row, col])

            for i, ((sum_data, xsan_data), (sum_name, xsan_name)) in enumerate(
                zip(data_pair_to_compare, san_pair_to_compare)
            ):
                offset = (i - (M - 1) / 2) * pair_width + pair_width / 2

                # sum bar
                ax.bar(
                    [offset - bar_width / 2],
                    [sum_data[bi]],
                    width=bar_width,
                    color=colors[i],
                    edgecolor="black",
                    linewidth=linewidth,
                    zorder=3,
                    alpha=0.8
                )

                # xsan bar
                ax.bar(
                    [offset + bar_width / 2],
                    [xsan_data[bi]],
                    width=bar_width,
                    color=colors[i],
                    edgecolor="black",
                    linewidth=linewidth,
                    hatch="///",
                    zorder=3,
                    alpha=0.8
                )

            # baseline
            ax.axhline(y=100, color="red", linestyle="--",
                       linewidth=1, zorder=1)

            # 坐标轴 & 标题
            ax.set_title(bench, fontsize=10)
            ax.set_xticks([])   # 去掉x轴刻度
            ax.set_xlabel("")
            if col == 0:
                ax.set_ylabel("Overhead (%)")
            else:
                # Set ylabel for the all columns
                pass
                # ax.set_yticklabels([])

        # --- legend ---
        legend_handles = []
        for i, (sum_name, _) in enumerate(san_pair_to_compare):
            legend_handles.append(Patch(facecolor=colors[i],
                                        edgecolor='black',
                                        linewidth=linewidth,
                                        label=f'{sum_name} (Sum)'))
            legend_handles.append(Patch(facecolor=colors[i],
                                        edgecolor='black',
                                        hatch='///',
                                        linewidth=linewidth,
                                        label=f'{sum_name} (XSan)'))
        if legend_handles:
            ncol = min(4, max(1, len(legend_handles)))
            ax_legend.legend(handles=legend_handles,
                             loc="center",
                             ncol=ncol,
                             frameon=False,
                             handlelength=1.2,
                             fontsize=9)

        typ = "time" if is_time else "rss"
        plt.savefig(f"compare_{typ}.png",
                    dpi=300, bbox_inches="tight")
        plt.close(fig)

    def draw_violin():
        # --- 1. 准备数据 ---
        rows = []
        groups = []   # 用来记录分组名 -> 颜色
        for idx, ((sum_data, xsan_data), (sum_name, xsan_name)) in enumerate(
            zip(data_pair_to_compare, san_pair_to_compare)
        ):
            group_id = f"{sum_name}"   # 组名（不带 Sum/XSan）
            groups.append(group_id)
            for v in sum_data:
                rows.append({"Group": group_id, "Type": "Sum",
                            "Overhead": v, "X": idx})
            for v in xsan_data:
                rows.append({"Group": group_id, "Type": "XSan",
                            "Overhead": v, "X": idx})
        df = pd.DataFrame(rows)

        # --- 2. 配色 ---
        cmap = plt.get_cmap("tab10")
        group_colors = {g: cmap(i % 10) for i, g in enumerate(groups)}

        fig = plt.figure(figsize=(10, 6))
        gs = fig.add_gridspec(3, 1, height_ratios=[
                              0.06, 0.2, 1.0], hspace=0.02)

        # title
        ax_title = fig.add_subplot(gs[0])
        ax_title.axis('off')
        title_text = f'{"CPU" if is_time else "Memory"} Overhead Distribution: Sum vs XSan'
        ax_title.text(0.5, 0.5, title_text, ha='center',
                      va='center', fontsize=16, fontweight='bold')

        # legend row
        ax_legend = fig.add_subplot(gs[1])
        ax_legend.axis('off')

        # main axis
        ax = fig.add_subplot(gs[2])

        # --- 3. 逐组绘制 violin ---
        for idx, g in enumerate(groups):
            gdf = df[df["Group"] == g]

            # Sum vs XSan
            for t, hatch, shift in [("Sum", "", -0.2), ("XSan", "///", 0.2)]:
                sdf = gdf[gdf["Type"] == t]
                color = group_colors[g]

                # 绘制 violin
                parts = ax.violinplot(
                    dataset=[sdf["Overhead"]],
                    positions=[idx + shift],
                    widths=0.35,
                    showmedians=True,
                    showmeans=False,
                )
                for pc in parts['bodies']:
                    pc.set_facecolor(color)
                    pc.set_edgecolor("black")
                    pc.set_alpha(0.5)
                    if hatch:
                        pc.set_hatch(hatch)
                for partname in ('cbars', 'cmins', 'cmaxes', 'cmedians'):
                    vp = parts[partname]
                    vp.set_edgecolor("black")
                    vp.set_linewidth(1)

                # 中位数数值
                median_val = sdf["Overhead"].median()
                ax.text(idx + shift,
                        median_val,
                        f"{median_val:.0f}%",
                        ha="center", va="bottom",
                        fontsize=8, fontweight="bold")

        # --- 4. 美化 ---
        # baseline
        ax.axhline(y=100, color="red", linestyle="--", linewidth=1.2, zorder=1)

        ax.set_xticks(range(len(groups)))
        ax.set_xticklabels([])  # 不显示组名
        ax.set_xlim(-0.5, len(groups) - 0.5)
        ax.set_ylabel("Overhead (%)")

        # Legend 替代横坐标
        legend_handles = []
        for gi, (sum_name, _) in enumerate(san_pair_to_compare):
            base_color = group_colors[sum_name]
            legend_handles.append(
                Patch(facecolor=base_color, edgecolor="black", linewidth=0.4, alpha=0.65,
                      label=f"{sum_name} (Sum)")
            )
            legend_handles.append(
                Patch(facecolor=base_color, edgecolor="black", linewidth=0.4, alpha=0.65,
                      hatch="///", label=f"{sum_name} (XSan)")
            )
        if legend_handles:
            ncol = min(4, max(1, len(legend_handles)))
            ax_legend.legend(handles=legend_handles,
                             loc='center',
                             ncol=ncol,
                             frameon=False,
                             handlelength=1.2,
                             fontsize=9)

        # 压缩上下左右空白
        # plt.tight_layout(rect=[0.05, 0.05, 0.98, 0.95])
        typ = 'time' if is_time else 'rss'
        plt.savefig(f'compare_violin_{typ}.png',
                    dpi=300, bbox_inches='tight')
        plt.close()

    # --- run both ---
    draw_bar()
    draw_violin()
    print("------ Compare figures (bar + violin) saved ------")


def plot_figures():
    '''
    plot the figure of the overheads of the sanitizers on the benchmarks
    '''
    plot_figure(True)
    plot_figure(False)


def draw_overall_figures(
    overheads: np.ndarray,
    sanitizers: list,
    ignore_small: bool = False,
    is_rss: bool = False,
):
    '''
    draw the bar chart of the overhead of the sanitizers over the benchmarks
    overheads_over_benchmarks : N * M, N = len(BENCHMARKS), M = len(sanitizers)
    '''
    # 获取 N 和 M
    N = overheads.shape[0]  # 基准数量
    M = overheads.shape[1]  # sanitizer 数量

    def draw_bar():
        benchmarks = BENCH_TO_PLOT
        n_bench = len(benchmarks)
        x_indices = np.arange(M)  # 每个子图里画 M 个 sanitizer
        bar_width = 0.8

        # 子图排布：每行最多 5 个
        ncols = 5
        nrows = int(np.ceil(n_bench / ncols))

        # Figure 大小随行列数调整
        fig_width = 3 * ncols
        fig_height = 2.5 * nrows + 2
        fig = plt.figure(figsize=(fig_width, fig_height))
        gs = fig.add_gridspec(
            nrows + 2, ncols,   # +2 行用于 title+legend
            height_ratios=[0.1, 0.2] + [1.0] * nrows,
            hspace=0.35, wspace=0.25
        )

        # --- 标题行 ---
        ax_title = fig.add_subplot(gs[0, :])
        ax_title.axis("off")
        title_text = f'{"CPU" if not is_rss else "Memory"} Overheads of Sanitizers on Benchmarks{" (ignore small)" if ignore_small else ""}'
        ax_title.text(0.5, 0.5, title_text,
                      ha="center", va="center",
                      fontsize=14, fontweight="bold")

        # --- 图例行 ---
        ax_legend = fig.add_subplot(gs[1, :])
        ax_legend.axis("off")

        linewidth = 0.4
        colors = plt.get_cmap("tab20").colors

        # --- 每个 benchmark 一个子图 ---
        for bi, bench in enumerate(benchmarks):
            row = bi // ncols + 2
            col = bi % ncols
            ax = fig.add_subplot(gs[row, col])

            bars = []
            for j in range(M):
                ax.bar(
                    x_indices[j],
                    overheads[bi, j],
                    width=bar_width * 0.9,
                    label=sanitizers[j],
                    color=colors[j % len(colors)],
                    edgecolor="black",
                    linewidth=linewidth,
                    alpha=0.9
                )

            # baseline
            ax.axhline(y=100, color="red", linestyle="--", linewidth=1)

            # 标题 & 坐标
            ax.set_title(bench, fontsize=10)
            ax.set_xticks([])
            if col == 0:
                ax.set_ylabel("Overhead (%)")

        # --- 图例 ---
        legend_handles = [
            plt.Rectangle((0, 0), 1, 1,
                          facecolor=colors[j % len(colors)],
                          edgecolor="black",
                          linewidth=linewidth,
                          label=sanitizers[j])
            for j in range(M)
        ]
        ncol = min(6, M)
        ax_legend.legend(
            handles=legend_handles,
            labels=sanitizers,
            loc="center",
            ncol=ncol,
            frameon=False,
            fontsize=9
        )

        # --- 保存 ---
        typ = 'rss' if is_rss else 'time'
        typ = f'{typ}_big' if ignore_small else typ
        plt.savefig(f'overheads_{typ}.png',
                    dpi=300, bbox_inches='tight')
        plt.close(fig)

    def draw_violin():
        # --- 1. 准备数据 ---
        # overheads: shape (N_benchmarks, M_sanitizers, repeat_times) 或者 (repeat_times, M)
        # 这里 flatten 成 repeat_times * M
        df = pd.DataFrame(overheads, columns=sanitizers)

        # --- 2. 配色 ---
        cmap = plt.get_cmap("tab20")
        colors = [cmap(i % 20) for i in range(len(sanitizers))]

        fig = plt.figure(figsize=(10, 6))
        gs = fig.add_gridspec(3, 1, height_ratios=[
                              0.08, 0.14, 1.0], hspace=0.05)

        # title
        ax_title = fig.add_subplot(gs[0])
        ax_title.axis("off")
        title_text = (
            "CPU" if not is_rss else "Memory"
        ) + " Overhead Distribution of Sanitizers"
        if ignore_small:
            title_text += " (ignore small)"
        ax_title.text(
            0.5,
            0.5,
            title_text,
            ha="center",
            va="center",
            fontsize=16,
            fontweight="bold",
        )

        # legend row
        ax_legend = fig.add_subplot(gs[1])
        ax_legend.axis("off")

        # main axis
        ax = fig.add_subplot(gs[2])

        # --- 3. 逐 sanitizer 绘制 violin ---
        legend_handles = []
        for idx, san in enumerate(sanitizers):
            sdf = df[san].dropna()  # 单个 sanitizer 的数据
            parts = ax.violinplot(
                dataset=[sdf],
                positions=[idx],
                widths=0.6,
                showmedians=True,
                showmeans=False,
            )
            # 上色
            for pc in parts["bodies"]:
                pc.set_facecolor(colors[idx])
                pc.set_edgecolor("black")
                pc.set_alpha(0.7)
            for partname in ("cbars", "cmins", "cmaxes", "cmedians"):
                vp = parts[partname]
                vp.set_edgecolor("black")
                vp.set_linewidth(1)

            # 中位数数值
            median_val = sdf.median()
            ax.text(
                idx,
                median_val,
                f"{median_val:.0f}%",
                ha="center",
                va="bottom",
                fontsize=8,
                fontweight="bold",
            )

            # legend handle
            legend_handles.append(
                Patch(
                    facecolor=colors[idx],
                    edgecolor="black",
                    linewidth=0.6,
                    label=san,
                )
            )

        # --- 4. 美化 ---
        ax.axhline(y=100, color="red", linestyle="--", linewidth=1.2, zorder=1)
        ax.set_xticks(range(len(sanitizers)))
        ax.set_xticklabels([])  # 不在主图上写名字，交给 legend
        ax.set_xlim(-0.5, len(sanitizers) - 0.5)
        ax.set_ylabel("Overhead (%)")

        # legend
        ncol = min(6, max(1, len(legend_handles)))
        ax_legend.legend(
            handles=legend_handles,
            loc="center",
            ncol=ncol,
            frameon=False,
            handlelength=1.2,
            fontsize=9,
        )

        # 保存
        typ = "rss" if is_rss else "time"
        typ = f"{typ}_big" if ignore_small else typ
        plt.savefig(f"overheads_violin_{typ}.png",
                    dpi=300, bbox_inches="tight")
        plt.close(fig)

    draw_bar()
    draw_violin()
    print('--------------- Done! ----------------')


def main():
    plot_figures()


if __name__ == '__main__':
    if len(sys.argv) > 1:
        FORCE = sys.argv[1] == '-f'
    main()
