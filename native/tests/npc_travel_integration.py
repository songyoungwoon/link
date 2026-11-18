"""Native npc-travel-v1 observer against disposable LINK socket fixtures.

No player JOIN/token/pose/action is sent. This tests transport and presentation
inputs; real server-to-server migration is tested in the sibling game repository.
"""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import socketserver
import struct
import subprocess
import threading
import time

HEADER = struct.Struct('!4sBBBBII')


def npc(epoch=1, journey=0, x=0, animation='Idle'):
    return dict(id='npc:liora:001',kind='liora',name='Liora \ud0d0\ud5d8\uac00',position=[x,0,-3],yaw=90,animation=animation,epoch=epoch,journey=journey)


class Fixture(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    block_on_close = False

    def __init__(self, mode='normal'):
        self.mode = mode
        self.failures = []
        self.steps = []
        super().__init__(('127.0.0.1', 0), Handler)


class Handler(socketserver.BaseRequestHandler):
    def exact(self, count):
        result = b''
        while len(result) < count:
            part = self.request.recv(count-len(result))
            if not part:
                raise EOFError
            result += part
        return result

    def receive(self):
        magic, major, minor, kind, flags, request, size = HEADER.unpack(self.exact(16))
        assert (magic, major, minor, flags) == (b'LINK', 0, 1, 0) and size <= 65536
        body = self.exact(size)
        return kind, request, json.loads(body) if body else None

    def send(self, kind, request, value, **changes):
        body = json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(',', ':')).encode()
        header = HEADER.pack(changes.get('magic', b'LINK'), changes.get('major', 0), changes.get('minor', 1), kind, changes.get('flags', 0), changes.get('id', request), changes.get('size', len(body)))
        packet = header + body
        if self.server.mode == 'fragmented':
            for offset in range(0, len(packet), 7):
                self.request.sendall(packet[offset:offset+7])
        else:
            self.request.sendall(packet)

    def snapshot(self, tick):
        state = dict(tick=tick, time=tick*.05, world='fixture', npcs=[npc()])
        if tick == 2:
            state['npcs'] = [npc(x=.1, animation='Walk')]
        elif tick == 3:
            state['npcs'] = []
        elif tick >= 4:
            state['npcs'] = [npc(epoch=2, journey=1, x=tick*.1, animation='Walk')]
        mode = self.server.mode
        if mode == 'world-mismatch': state['world'] = 'other-world'
        if mode == 'outer-field': state['credential'] = 'must-not-forward'
        if mode == 'missing-field': del state['time']
        if mode == 'tick-fraction': state['tick'] = .5
        if mode == 'tick-replay' and tick > 1: state['tick'] = 0
        if mode == 'time-negative': state['time'] = -1
        if mode == 'npc-slots': state['npcs'] = [npc() for _ in range(17)]
        if mode == 'duplicate-id': state['npcs'] = [npc(), npc()]
        if state['npcs']:
            entry = state['npcs'][0]
            if mode == 'npc-extra': entry['token'] = 'must-not-forward'
            if mode == 'npc-missing': del entry['kind']
            if mode == 'npc-id': entry['id'] = '../remote-id'
            if mode == 'npc-kind': entry['kind'] = 'https://remote-kind'
            if mode == 'npc-name': entry['name'] = 'control\nname'
            if mode == 'npc-name-size': entry['name'] = 'x'*129
            if mode == 'npc-animation': entry['animation'] = ''
            if mode == 'npc-position': entry['position'] = [1000001,0,0]
            if mode == 'npc-vector': entry['position'] = [0,0]
            if mode == 'npc-position-type': entry['position'] = [False,0,0]
            if mode == 'npc-yaw': entry['yaw'] = 1000001
            if mode == 'npc-epoch-zero': entry['epoch'] = 0
            if mode == 'npc-epoch-fraction': entry['epoch'] = 1.5
            if mode == 'npc-epoch-boolean': entry['epoch'] = True
            if mode == 'npc-journey-negative': entry['journey'] = -1
            if mode == 'npc-journey-fraction': entry['journey'] = .5
        return state

    def handle(self):
        try:
            self.request.settimeout(4)
            kind, request, join = self.receive()
            assert (kind, request) == (16, 0)
            assert join == dict(version=1,world='fixture',profile='npc-travel-v1'), 'observer JOIN leaked player fields'
            mode = self.server.mode
            if mode == 'silent':
                time.sleep(.4)
                return
            response = dict(snapshot=self.snapshot(1))
            if mode == 'joined-token': response['token'] = 'must-not-forward'
            if mode == 'oversize': self.send(17,0,response,size=65537);return
            if mode == 'wrong-id': self.send(17,0,response,id=8);return
            if mode == 'wrong-version': self.send(17,0,response,minor=2);return
            if mode == 'wrong-flags': self.send(17,0,response,flags=1);return
            if mode == 'wrong-type': self.send(19,0,response);return
            if mode == 'duplicate-key':
                body = b'{"snapshot":{},"snapshot":{}}'
                self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body)
                return
            if mode == 'nonfinite':
                body = json.dumps(response).replace('"yaw": 90', '"yaw": NaN').encode()
                self.request.sendall(HEADER.pack(b'LINK',0,1,17,0,0,len(body))+body)
                return
            self.send(17,0,response)
            expected = 1
            while True:
                kind, request, body = self.receive()
                if kind == 20:
                    assert body is None
                    return
                assert request == expected, 'observer request sequence mismatch'
                assert kind == 18 and body == {}, 'observer STEP must contain no pose/actions'
                self.server.steps.append(time.monotonic())
                self.send(19,request,self.snapshot(expected+1),**({'id':request-1} if mode=='state-replay' else {}))
                expected += 1
        # Cancellation may interrupt a framed send before the best-effort LEAVE.
        # The peer is already closed; incomplete terminal JSON is not a new request.
        except (ConnectionError,EOFError,OSError,json.JSONDecodeError):
            pass
        except Exception as error:
            self.server.failures.append(str(error))


def fixture_case(probe, mode, *arguments):
    server = Fixture(mode)
    thread = threading.Thread(target=lambda:server.serve_forever(poll_interval=.01),daemon=True)
    thread.start()
    try:
        address = f'link://127.0.0.1:{server.server_address[1]}/fixture'
        result = subprocess.run([str(probe),'--address',address,*arguments],capture_output=True,timeout=12)
        if result.returncode:
            raise AssertionError(f'{mode}: native observer probe failed: '+result.stderr.decode('utf-8','replace'))
        report = json.loads(result.stdout)
        assert report['passed'] is True
        assert not server.failures, f'{mode}: client violated observer contract: {server.failures}'
        return report, server
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe',required=True,type=Path)
    options = parser.parse_args()
    for mode in ('normal','fragmented'):
        report, server = fixture_case(options.probe,mode,'--expect-cycle')
        assert report['credentialsAbsent'] and report['snapshots'] >= 4
        assert report['sawNpc'] and report['sawEmpty'] and report['sawWalk'] and report['positionChanged']
        assert report['maxEpoch'] == 2 and report['maxJourney'] == 1
        assert report['identities'] == ['npc:liora:001'] and len(server.steps) >= 3
    report, _ = fixture_case(options.probe,'silent','--stop-after-ms','40')
    assert report['stopMs'] < 500
    invalid = ('world-mismatch','outer-field','missing-field','tick-fraction','tick-replay','time-negative','npc-slots','duplicate-id','npc-extra','npc-missing','npc-id','npc-kind','npc-name','npc-name-size','npc-animation','npc-position','npc-vector','npc-position-type','npc-yaw','npc-epoch-zero','npc-epoch-fraction','npc-epoch-boolean','npc-journey-negative','npc-journey-fraction','joined-token','oversize','wrong-id','wrong-version','wrong-flags','wrong-type','duplicate-key','nonfinite','state-replay')
    for mode in invalid:
        report, _ = fixture_case(options.probe,mode,'--duration-ms','250','--expect-error')
        assert report['rejected'] is True, mode
    print('PASS NPC observer: no player credentials/actions/pose, repeat snapshots, Unicode, movement/animation, departure/return, epoch/journey continuity, fragmentation, bounded cancellation and 33 malformed/replay rejections')


if __name__ == '__main__':
    main()
