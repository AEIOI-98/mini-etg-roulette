"""Build the same C++ sources as the simulator, with the browser bindings."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

web = Path(__file__).resolve().parent
repo = web.parent
compiler = os.environ.get('EMXX') or shutil.which('em++')
if not compiler:
    sys.exit('Activate Emscripten 4.0.22 (source emsdk_env.sh), then run npm run build.')
out = web / 'public' / 'engine'
out.mkdir(parents=True, exist_ok=True)
sources = sorted((repo / 'core/src').glob('*.cpp'))
sources += sorted((repo / 'sim/shared/src').glob('*.cpp'))
sources += sorted((web / 'wasm').glob('*.cpp'))
subprocess.run([
    compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-exceptions',
    '-I' + str(repo / 'core/include'), '-I' + str(repo / 'sim/shared/include'),
    *map(str, sources), '--bind', '--no-entry', '-sWASM_BIGINT=1',
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,node',
    '-sSTACK_SIZE=1048576', '-sALLOW_MEMORY_GROWTH=1', '-o', str(out / 'roulette.mjs')
], check=True)
