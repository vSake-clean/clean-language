#!/usr/bin/env python3
"""Vertical bar charts showing performance relative to C (-O0) — więcej = lepiej."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({
    'font.family': 'DejaVu Sans',
    'font.size': 11,
    'axes.titlesize': 13,
    'axes.labelsize': 11,
    'figure.facecolor': 'white',
})

CLEAN_COLOR = '#2ecc71'
CLEAN_NOREG_COLOR = '#95a5a6'
C_COLOR = '#3498db'
OTHER_COLORS = ['#e74c3c', '#f39c12', '#9b59b6', '#1abc9c', '#e67e22', '#34495e']

def make_chart(data, title, ylabel, filename, baseline=None, suffix='×'):
    labels = [d[0] for d in data]
    values = [d[1] for d in data]

    colors = []
    for label in labels:
        if label == 'Clean':
            colors.append(CLEAN_COLOR)
        elif 'bez rej' in label:
            colors.append(CLEAN_NOREG_COLOR)
        elif label.startswith('C '):
            colors.append(C_COLOR)
        else:
            colors.append(OTHER_COLORS[len(colors) % len(OTHER_COLORS)])

    fig, ax = plt.subplots(figsize=(10, 6))

    x = np.arange(len(labels))
    width = 0.55
    bars = ax.bar(x, values, width, color=colors, edgecolor='white', linewidth=0.5)

    for bar, val in zip(bars, values):
        if val >= 100:
            txt = f'{val:.0f}{suffix}'
        elif val >= 10:
            txt = f'{val:.1f}{suffix}'
        else:
            txt = f'{val:.2f}{suffix}'
        yoff = bar.get_height() + max(values) * 0.015
        ax.text(bar.get_x() + bar.get_width() / 2., yoff,
                txt, ha='center', va='bottom', fontsize=9, fontweight='bold')

    if baseline is not None:
        ax.axhline(y=baseline, color='gray', linestyle='--', linewidth=0.8, alpha=0.5)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=25, ha='right', fontsize=9)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.set_title(title, fontsize=13, fontweight='bold', pad=12)
    ax.set_axisbelow(True)
    ax.yaxis.grid(True, linestyle='--', alpha=0.3)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    plt.tight_layout()
    plt.savefig(filename, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"  Saved {filename}")


# C (-O0) baseline times for each benchmark
C_TIMES = {
    'count': 3.65,
    'prime': 0.46,
    'fib': 0.17,
    'nqueens': 3.35,
    'mandelbrot': 0.44,
    'bintree': 3.80,
    'matrix': 0.20,
}

benchmarks = [
    {
        'file': 'bench/count_bench.png',
        'title': 'Count-to-1-billion (pusta pętla, 10⁹ iteracji)',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('Clean', C_TIMES['count'] / 1.10),
            ('C (-O0)', 1.00),
            ('Clean (bez rej.)', C_TIMES['count'] / 7.30),
            ('PHP 8.4', C_TIMES['count'] / 6.97),
            ('Ruby 3.3', C_TIMES['count'] / 32.52),
            ('Python 3.13', C_TIMES['count'] / 156.32),
        ],
    },
    {
        'file': 'bench/prime_bench.png',
        'title': 'Liczby pierwsze do 1.000.000 (sito z dzieleniem)',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('C (-O0)', 1.00),
            ('Clean', C_TIMES['prime'] / 1.72),
            ('PHP 8.4', C_TIMES['prime'] / 2.77),
            ('Ruby 3.3', C_TIMES['prime'] / 6.56),
            ('Python 3.13', C_TIMES['prime'] / 30.66),
        ],
    },
    {
        'file': 'bench/fib_bench.png',
        'title': 'Fibonacci(35) — rekurencyjny',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('C (-O0)', 1.00),
            ('Clean', C_TIMES['fib'] / 0.20),
            ('PHP 8.4', C_TIMES['fib'] / 1.57),
            ('Ruby 3.3', C_TIMES['fib'] / 2.46),
            ('Python 3.13', C_TIMES['fib'] / 3.52),
        ],
    },
    {
        'file': 'bench/nqueens_bench.png',
        'title': 'N-queens (13) — rekurencyjny backtracking',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('C (-O0)', 1.00),
            ('Clean', C_TIMES['nqueens'] / 7.89),
            ('PHP 8.4', C_TIMES['nqueens'] / 28.14),
            ('Python 3.13', C_TIMES['nqueens'] / 78.77),
            ('Ruby 3.3', C_TIMES['nqueens'] / 132.53),
        ],
    },
    {
        'file': 'bench/mandelbrot_bench.png',
        'title': 'Mandelbrot (800×800, max 200) — zmiennoprzecinkowy',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('C (-O0)', 1.00),
            ('Clean', C_TIMES['mandelbrot'] / 1.70),
            ('PHP 8.4', C_TIMES['mandelbrot'] / 4.04),
            ('Ruby 3.3', C_TIMES['mandelbrot'] / 14.03),
            ('Python 3.13', C_TIMES['mandelbrot'] / 33.65),
        ],
    },
    {
        'file': 'bench/bintree_bench.png',
        'title': 'Binary trees (depth 21 × 10) — alokacja sterty',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('C (-O0)', 1.00),
            ('Clean', C_TIMES['bintree'] / 4.70),
            ('PHP 8.4', C_TIMES['bintree'] / 29.44),
            ('Ruby 3.3', C_TIMES['bintree'] / 29.72),
            ('Python 3.13', C_TIMES['bintree'] / 55.16),
        ],
    },
    {
        'file': 'bench/matrix_bench.png',
        'title': 'Mnożenie macierzy (100×100×100)',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej',
        'data': [
            ('Clean', C_TIMES['matrix'] / 0.05),
            ('C (-O0)', 1.00),
        ],
    },
    {
        'file': 'bench/learn_bench.png',
        'title': 'Czas nauki (od zera do produktywnego)',
        'ylabel': 'Tygodnie — mniej = lepiej',
        'baseline': None,
        'suffix': ' tyg.',
        'data': [
            ('Python', 3),
            ('PHP', 4.5),
            ('Ruby', 6),
            ('Clean', 9),
            ('C', 12),
        ],
    },
]

if __name__ == '__main__':
    for b in benchmarks:
        make_chart(b['data'], b['title'], b['ylabel'], b['file'],
                   baseline=b.get('baseline', 1.0), suffix=b.get('suffix', '×'))
    print("Done!")
