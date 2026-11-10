"""st を起動し、stderr に pattern を含む行が出てから 1 秒後に SIGINT を送る（Ctrl+C の代わり）。

使い方: python3 interrupt.py <pattern> <st の引数...>
st の stderr はそのまま流す。終了コードは st のもの。
"""
import signal
import subprocess
import sys
import threading

pattern, args = sys.argv[1], sys.argv[2:]
p = subprocess.Popen([sys.executable, *args], stderr=subprocess.PIPE, text=True)
sent = False
for line in p.stderr:
    sys.stderr.write(line)
    if not sent and pattern in line:
        threading.Timer(1.0, p.send_signal, args=(signal.SIGINT,)).start()
        sent = True
sys.exit(p.wait())
