"""Run the actual authored RPG through LINK's Runtime and real shared transport.

Starts only an owned localhost test server with an isolated SQLite database.
This is a headless Script/presentation integration test, not a GPU or VR test.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--probe',type=Path,required=True)
    parser.add_argument('--server',type=Path,required=True)
    parser.add_argument('--worlds',type=Path,required=True)
    args=parser.parse_args()
    probe,server,worlds=args.probe.resolve(),args.server.resolve(),args.worlds.resolve()
    if not probe.is_file() or not server.is_file() or not (worlds/'sao'/'world.link').is_file():
        raise AssertionError('Actual world, native probe and test server must exist')
    with tempfile.TemporaryDirectory(prefix='link-shared-world-') as temporary:
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        process=subprocess.Popen([sys.executable,str(server),'--host','127.0.0.1','--port',str(port),'--worlds',str(worlds),'--database',str(Path(temporary)/'characters.sqlite3')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                if process.poll() is not None:raise AssertionError('Owned test server exited during startup')
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=.15):break
                except OSError:time.sleep(.05)
            else:raise AssertionError('Owned test server startup timed out')
            result=subprocess.run([str(probe),'--worlds',str(worlds),'--address',f'link://127.0.0.1:{port}/sao'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            if result.returncode:
                raise AssertionError('Actual world native integration failed: '+result.stderr.decode('utf-8','replace'))
            report=json.loads(result.stdout.decode('utf-8-sig'))
            assert report['passed'] is True and report['bindingErrors']==0
            assert report['activePlayers']==2 and report['networkSnapshots']>=5
            assert report['nodes']<=500 and report['bindings']>=20 and report['actionContracts']==6
            for feature in ('snapshotNullStart','selfAndInactiveHidden','otherAndNearVisibility','monstersAndHealth','allWeaponStyles','viewerWeaponPose','hudText','jointAnimations'):
                assert report[feature] is True, feature
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
    print('PASS actual RPG world: null-snapshot startup, real two-player snapshots, zero Script binding errors, visibility/health, three weapon styles, complete viewer pose, live HUD, six action contracts, articulated animation; no graphics/headset claim')


if __name__=='__main__':main()
