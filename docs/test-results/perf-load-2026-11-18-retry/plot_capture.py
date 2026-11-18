"""Export observed board summaries and host CSV latency with the existing Qt plotting stack."""
import csv
import gzip
import json
import math
import sys
from pathlib import Path
from collections import defaultdict
import pyqtgraph as pg
import pyqtgraph.exporters
from PySide6 import QtWidgets

source, destination = map(Path, sys.argv[1:3])
def open_text(path):
    return path.open() if path.exists() else gzip.open(str(path) + '.gz', 'rt')

with open_text(source / 'results.json') as stream:
    results = json.load(stream)
with open_text(source / 'offline-audit.json') as stream:
    audit = json.load(stream)
if not audit.get('passed'):
    raise ValueError('Require an independently replayed complete capture')
with open_text(source / 'performance.json') as stream:
    snapshots = json.load(stream)
with open_text(source / 'session/samples.csv') as stream:
    sent = {int(r['timestamp_ms']): float(r['tx_monotonic_s']) for r in csv.DictReader(stream)}
start = min(sent.values())
latencies = defaultdict(list)
with open_text(source / 'session/reports.csv') as stream:
    for row in csv.DictReader(stream):
        if int(row['flags']) & 1:
            timestamp = int(row['input_timestamp_ms'])
            if timestamp in sent:
                latencies[int((sent[timestamp] - start) // 60)].append(
                    (float(row['rx_monotonic_s']) - sent[timestamp]) * 1000)

def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]

app = QtWidgets.QApplication([])
pg.setConfigOptions(background='w', foreground='#172235', antialias=True)
widget = pg.GraphicsLayoutWidget()
widget.resize(1400, 1100)
widget.ci.layout.setSpacing(30)
widget.addLabel('STM32F746 30-minute capture | 50 Hz nominal + STATUS 1 Hz',
                color='#172235', size='17pt', colspan=2)
widget.nextRow()
widget.addLabel('Host pacing limit exceeded: this run did not meet all acceptance criteria',
                color='#b43b20', size='12pt', colspan=2)
widget.nextRow()
board = widget.addPlot(title='Board RX completion to TX submission | cumulative statistics')
board.addLegend(offset=(15, 10))
board.setLabel('left', 'Latency', units='ms')
board.setLabel('bottom', 'Board uptime', units='min')
board.showGrid(x=True, y=True, alpha=.2)
timed = [s for s in snapshots if s['rx_to_submit']['n'] > 0]
times = [s['header']['uptime_ms'] / 60000 for s in timed]
for field, color, label in [('p99_bin', '#1764bb', 'p99 (1 ms bins)'), ('max_ms', '#d56820', 'Observed maximum')]:
    values = [s['rx_to_submit'][field] for s in timed]
    if field == 'p99_bin' and any(v == 21 for v in values):
        raise ValueError('Overflow percentile bin cannot be plotted as exact milliseconds')
    board.plot(times, values, pen=pg.mkPen(color, width=2), name=label)
board.setYRange(0, max(2, max(s['rx_to_submit']['max_ms'] for s in snapshots) + .5))
widget.nextRow()
host = widget.addPlot(title='PC write to received TRACK | per-minute statistics, separate PC clock')
host.addLegend(offset=(15, 10))
host.setLabel('left', 'Latency', units='ms')
host.setLabel('bottom', 'Host load elapsed', units='min')
host.showGrid(x=True, y=True, alpha=.2)
minutes = sorted(latencies)
for quantile, color, label in [(.5, '#1764bb', 'p50'), (.99, '#d56820', 'p99')]:
    host.plot([m + .5 for m in minutes], [percentile(latencies[m], quantile) for m in minutes],
              pen=pg.mkPen(color, width=2), symbol='o', symbolSize=4, name=label)
widget.nextRow()
stacks = widget.addPlot(title='Sampled minimum unused stack | not an overflow-proof bound')
stacks.addLegend(offset=(15, 10))
stacks.setLabel('left', 'Unused stack', units='bytes')
stacks.setLabel('bottom', 'Board uptime', units='min')
stacks.showGrid(x=True, y=True, alpha=.2)
for name, color in [('rx', '#1764bb'), ('handler', '#2a8b58'), ('tx', '#d56820')]:
    stacks.plot([s['header']['uptime_ms'] / 60000 for s in snapshots], [s['stack ' + name]['min_unused'] for s in snapshots],
                pen=pg.mkPen(color, width=2), name=name.upper())
stacks.setYRange(0, 1850)
for plot in (board, host, stacks):
    for axis in ('left', 'bottom'):
        plot.getAxis(axis).enableAutoSIPrefix(False)
widget.show()
app.processEvents()
exporter = pg.exporters.ImageExporter(widget.scene())
exporter.parameters()['width'] = 1600
exporter.export(str(destination))
print(destination)
