#!/usr/bin/env python3
"""Generate vertical bar chart benchmark images (pionowe) for Clean language README."""

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

def make_chart(data, title, ylabel, filename, highlight_clean=True):
    labels = [d[0] for d in data]
    values = [d[1] for d in data]
    
    colors = []
    for label in labels:
        if 'Clean' in label and 'bez rej' not in label and highlight_clean:
            colors.append(CLEAN_COLOR)
        elif 'bez rej' in label:
            colors.append(CLEAN_NOREG_COLOR)
        elif label.startswith('C '):
            colors.append(C_COLOR)
        else:
            idx = len(colors)
            colors.append(OTHER_COLORS[idx % len(OTHER_COLORS)])
    
    fig, ax = plt.subplots(figsize=(10, 6))
    
    x = np.arange(len(labels))
    width = 0.55
    
    bars = ax.bar(x, values, width, color=colors, edgecolor='white', linewidth=0.5)
    
    suffix = ylabel.split('|')[-1] if '|' in ylabel else 's'
    for bar, val in zip(bars, values):
        if val >= 100:
            ax.text(bar.get_x() + bar.get_width()/2., bar.get_height(),
                    f'{val:.1f}{suffix}', ha='center', va='bottom', fontsize=9, fontweight='bold')
        elif val >= 10:
            ax.text(bar.get_x() + bar.get_width()/2., bar.get_height(),
                    f'{val:.1f}{suffix}', ha='center', va='bottom', fontsize=9, fontweight='bold')
        elif val >= 1:
            ax.text(bar.get_x() + bar.get_width()/2., bar.get_height(),
                    f'{val:.2f}{suffix}', ha='center', va='bottom', fontsize=9, fontweight='bold')
        else:
            ax.text(bar.get_x() + bar.get_width()/2., bar.get_height(),
                    f'{val:.2f}{suffix}', ha='center', va='bottom', fontsize=9, fontweight='bold')
    
    # add dashed baseline at 1.0 for relative-performance charts
    has_baseline = any(v == 1.0 or ('|' in ylabel) for v in values)
    if has_baseline:
        ax.axhline(y=1.0, color='gray', linestyle='--', linewidth=0.8, alpha=0.5)
    
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=25, ha='right', fontsize=9)
    ax.set_ylabel(ylabel.split('|')[0] if '|' in ylabel else ylabel, fontsize=11)
    ax.set_title(title, fontsize=13, fontweight='bold', pad=12)
    ax.set_axisbelow(True)
    ax.yaxis.grid(True, linestyle='--', alpha=0.3)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    
    plt.tight_layout()
    plt.savefig(filename, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"  Saved {filename}")


benchmarks = [
    {
        'file': 'bench/count_bench.png',
        'title': 'Count-to-1-billion (pusta pętla, 10⁹ iteracji)',
        'ylabel': 'Wydajność względem C (-O0) (×) — więcej = lepiej|×',
        'data': [
            ('Clean', 3.65/1.10),
            ('C (-O0)', 1.00),
            ('Clean (bez rej.)', 3.65/7.30),
            ('PHP 8.4', 3.65/6.97),
            ('Ruby 3.3', 3.65/32.52),
            ('Python 3.13', 3.65/156.32),
        ],
    },
    {
        'file': 'bench/prime_bench.png',
        'title': 'Liczby pierwsze do 1.000.000 (sito z dzieleniem)',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('C (-O0)', 0.46),
            ('Clean', 1.72),
            ('PHP 8.4', 2.77),
            ('Ruby 3.3', 6.56),
            ('Python 3.13', 30.66),
        ],
    },
    {
        'file': 'bench/fib_bench.png',
        'title': 'Fibonacci(35) — rekurencyjny',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('C (-O0)', 0.17),
            ('Clean', 0.20),
            ('PHP 8.4', 1.57),
            ('Ruby 3.3', 2.46),
            ('Python 3.13', 3.52),
        ],
    },
    {
        'file': 'bench/nqueens_bench.png',
        'title': 'N-queens (13) — rekurencyjny backtracking',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('C (-O0)', 3.35),
            ('Clean', 7.89),
            ('PHP 8.4', 28.14),
            ('Python 3.13', 78.77),
            ('Ruby 3.3', 132.53),
        ],
    },
    {
        'file': 'bench/mandelbrot_bench.png',
        'title': 'Mandelbrot (800×800, max 200) — zmiennoprzecinkowy',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('C (-O0)', 0.44),
            ('Clean', 1.70),
            ('PHP 8.4', 4.04),
            ('Ruby 3.3', 14.03),
            ('Python 3.13', 33.65),
        ],
    },
    {
        'file': 'bench/bintree_bench.png',
        'title': 'Binary trees (depth 21 × 10) — alokacja sterty',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('C (-O0)', 3.80),
            ('Clean', 4.70),
            ('PHP 8.4', 29.44),
            ('Ruby 3.3', 29.72),
            ('Python 3.13', 55.16),
        ],
    },
    {
        'file': 'bench/matrix_bench.png',
        'title': 'Mnożenie macierzy (100×100×100)',
        'ylabel': 'Czas (s) — mniej = lepiej',
        'data': [
            ('Clean', 0.05),
            ('C (-O0)', 0.20),
        ],
    },
    {
        'file': 'bench/learn_bench.png',
        'title': 'Czas nauki (od zera do produktywnego)',
        'ylabel': 'Tygodnie',
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
        make_chart(b['data'], b['title'], b['ylabel'], b['file'])
    print("Done!")
