"""Run the three fixed scenarios against the connected USART6 adapter."""
from pathlib import Path
import json
from pc_sim.evaluate import run_scenario

root = Path(__file__).resolve().parent
results = []
for scenario in ('normal', 'noisy', 'dropout'):
    path, result = run_scenario('/dev/cu.usbserial-A5069RR4', scenario, root / scenario)
    results.append({'path': str(path.relative_to(root)), **result})
    print(scenario, json.dumps(result), flush=True)
(root / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
