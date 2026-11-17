#!/usr/bin/env python3
"""Generate report plots from CSV results and the stated systolic scaling model.

Requires matplotlib and numpy. Plots are written as vector PDFs and preview PNGs.
"""
from pathlib import Path
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
plt.rcParams.update({'font.family': 'serif', 'font.size': 9, 'axes.spines.top': False,
                     'axes.spines.right': False, 'axes.axisbelow': True,
                     'pdf.fonttype': 42, 'ps.fonttype': 42})


def rows(section, file):
    with (ROOT / section / 'results' / file).open() as stream:
        return list(csv.DictReader(stream))


def save(fig, section, name):
    target = ROOT / section / 'report/figures'
    target.mkdir(parents=True, exist_ok=True)
    fig.savefig(target / (name + '.pdf'), metadata={'CreationDate': None, 'ModDate': None})
    fig.savefig(target / (name + '.png'), dpi=180)
    plt.close(fig)
    print((target / (name + '.pdf')).relative_to(ROOT))


def bars(section, labels, series, ylabel, name, legend_columns=3):
    fig, ax = plt.subplots(figsize=(6.3, 3.15), layout='constrained')
    x = np.arange(len(labels))
    width = .8 / len(series)
    shades = ['0.90', '0.72', '0.54', '0.36', '0.18']
    hatches = ['', '//', '..', '\\\\', 'xx']
    for i, (label, values) in enumerate(series):
        ax.bar(x + (i - (len(series)-1)/2) * width, values, width,
               label=label, color=shades[i], edgecolor='black', linewidth=.5, hatch=hatches[i])
    ax.set_xticks(x, labels)
    ax.set_ylabel(ylabel)
    ax.set_ylim(bottom=0)
    ax.grid(axis='y', alpha=.22)
    ax.legend(loc='lower center', bbox_to_anchor=(.5, 1.01), ncol=legend_columns,
              frameon=False, fontsize=8)
    save(fig, section, name)


section = 'superscalar_pipeline'
data = rows(section, 'summary.csv')
bars(section, [r['workload'] for r in data],
     [(label, [float(r[key]) for r in data]) for key, label in
      [('scalar', 'Scalar'), ('dual', 'Dual'), ('dual_forwarding', 'Forwarding'),
       ('always_taken', 'Always taken'), ('gshare', 'Gshare')]],
     'Cycles per instruction', 'cpi')

section = 'out_of_order_execution'
data = rows(section, 'recorded_cpi.csv')
works = ['bzip2', 'gcc', 'libq', 'mcf']
lookup = {(r['workload'], r['scheduling'], r['load_latency']): float(r['cpi'])
          for r in data if r['width'] == '2'}
bars(section, works, [(label, [lookup[w, policy, latency] for w in works])
     for policy, latency, label in [('in_order', '1', 'In-order, 1-cycle load'),
                                    ('out_of_order', '1', 'OOO, 1-cycle load'),
                                    ('in_order', '4', 'In-order, 4-cycle load'),
                                    ('out_of_order', '4', 'OOO, 4-cycle load')]],
     'Cycles per instruction', 'scheduling_cpi', 2)

section = 'cache_memory_system'
data = rows(section, 'summary.csv')
lookup = {(r['mix'], r['policy']): float(r['sum_ipc']) for r in data}
mixes = ['mix1', 'mix2', 'mix3']
bars(section, mixes, [(label, [lookup[m, key] for m in mixes]) for key, label in
     [('shared_lru', 'LRU'), ('static_4ways', 'Static: 4 ways'),
      ('static_8ways', 'Static: 8 ways'), ('static_12ways', 'Static: 12 ways'),
      ('dynamic_utility', 'Dynamic')]],
     'Sum of separately reported core IPCs', 'partitioning_ipc')

section = 'matrix_computing/systolic_array'
data = rows(section, 'analytical_scaling.csv')
x = np.array([int(r['n']) for r in data])
latency = 3*x-1
util = x / latency * 100
avg_bw = 2*x*x / latency
peak_bw = 2*x
fig, axes = plt.subplots(2, 2, figsize=(6.3, 3.55), layout='constrained')
ax = axes[0, 0]
ax.plot(x, latency, 'o--', color='black', markerfacecolor='white', markersize=4, label='Model')
ax.plot(x[0], latency[0], 'o', color='black', markersize=5, label='Checked N=4')
ax.set_ylabel('Latency (cycles)')
ax.set_yscale('log', base=2)
ax.legend(frameon=False, fontsize=7)
axes[0, 1].plot(x, util, 'o--', color='black', markerfacecolor='white', markersize=4)
axes[0, 1].set_ylabel('Utilization (%)')
axes[0, 1].set_ylim(0, 40)
axes[1, 0].plot(x, x**3 / latency, 'o--', color='black', markerfacecolor='white', markersize=4)
axes[1, 0].set_ylabel('Average MACs / cycle')
axes[1, 0].set_yscale('log')
axes[1, 1].plot(x, avg_bw, 'o--', color='black', markerfacecolor='white', markersize=4, label='Operation average')
axes[1, 1].plot(x, peak_bw, 's:', color='.4', markerfacecolor='white', markersize=4, label='Peak boundary input')
axes[1, 1].set_ylabel('Input bytes / cycle')
axes[1, 1].set_yscale('log', base=2)
axes[1, 1].legend(frameon=False, fontsize=7)
for ax in axes.flat:
    ax.set_xscale('log', base=2)
    ax.set_xticks(x, [str(n) for n in x])
    ax.set_xlabel('Array dimension N')
    ax.grid(alpha=.22)
save(fig, section, 'analytical_scaling')
