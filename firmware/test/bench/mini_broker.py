# -*- coding: utf-8 -*-
"""Minimal MQTT 3.1.1 broker for bench tests: accepts one client, answers CONNACK /
SUBACK / PUBACK / PINGRESP, logs every PUBLISH (topic, retain, payload) to a file.
It can also inject a message to the client (send_file: topic|payload lines).
Not a real broker: no persistence, no QoS 2, no multiple clients."""
import socket, sys, time, json, os, threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 1883
LOG = sys.argv[2] if len(sys.argv) > 2 else 'broker.log'
INJECT = sys.argv[3] if len(sys.argv) > 3 else 'inject.txt'
DURATION = int(sys.argv[4]) if len(sys.argv) > 4 else 120

def log(line):
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write('%s %s\n' % (time.strftime('%H:%M:%S'), line))

def read_exact(conn, n):
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('closed')
        buf += chunk
    return buf

def read_packet(conn):
    h = read_exact(conn, 1)[0]
    mult, rem, i = 1, 0, 0
    while True:
        b = read_exact(conn, 1)[0]
        rem += (b & 0x7F) * mult
        mult *= 128
        i += 1
        if not (b & 0x80) or i > 4:
            break
    body = read_exact(conn, rem) if rem else b''
    return h, body

def encode_len(n):
    out = b''
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out += bytes([b])
        if not n:
            return out

def publish_to(conn, topic, payload):
    t = topic.encode()
    body = len(t).to_bytes(2, 'big') + t + payload.encode()
    conn.sendall(bytes([0x30]) + encode_len(len(body)) + body)
    log('INJECTED %s = %s' % (topic, payload))

def injector(conn, stop):
    seen = 0
    while not stop.is_set():
        try:
            if os.path.exists(INJECT):
                lines = open(INJECT, encoding='utf-8').read().splitlines()
                while seen < len(lines):
                    t, _, p = lines[seen].partition('|')
                    if t:
                        publish_to(conn, t, p)
                    seen += 1
        except Exception as e:
            log('inject error %r' % e)
        time.sleep(0.5)

def handle(conn, addr):
    log('TCP from %s' % (addr,))
    stop = threading.Event()
    try:
        while True:
            h, body = read_packet(conn)
            typ = h >> 4
            if typ == 1:  # CONNECT
                i = 2 + int.from_bytes(body[0:2], 'big')  # protocol name
                level = body[i]; flags = body[i + 1]; keep = int.from_bytes(body[i + 2:i + 4], 'big')
                i += 4
                def s():
                    nonlocal i
                    n = int.from_bytes(body[i:i + 2], 'big'); v = body[i + 2:i + 2 + n]; i += 2 + n; return v
                cid = s().decode()
                will = None
                if flags & 0x04:
                    wt = s().decode(); wp = s().decode()
                    will = (wt, wp, (flags >> 3) & 3, bool(flags & 0x20))
                user = s().decode() if flags & 0x80 else None
                pw = s().decode() if flags & 0x40 else None
                log('CONNECT client=%s level=%d keepalive=%d user=%s pw=%s will=%s' % (cid, level, keep, user, '***' if pw else None, will))
                conn.sendall(bytes([0x20, 0x02, 0x00, 0x00]))
                threading.Thread(target=injector, args=(conn, stop), daemon=True).start()
            elif typ == 8:  # SUBSCRIBE
                pid = body[0:2]
                i = 2; topics = []
                while i < len(body):
                    n = int.from_bytes(body[i:i + 2], 'big'); topics.append((body[i + 2:i + 2 + n].decode(), body[i + 2 + n])); i += 3 + n
                log('SUBSCRIBE %s' % topics)
                conn.sendall(bytes([0x90, 2 + len(topics)]) + pid + bytes([q for _, q in topics]))
            elif typ == 3:  # PUBLISH
                qos = (h >> 1) & 3; retain = bool(h & 1)
                n = int.from_bytes(body[0:2], 'big'); topic = body[2:2 + n].decode(); i = 2 + n
                pid = None
                if qos:
                    pid = body[i:i + 2]; i += 2
                payload = body[i:].decode('utf-8', 'replace')
                log('PUBLISH qos=%d retain=%d %s = %s' % (qos, retain, topic, payload))
                if qos == 1:
                    conn.sendall(bytes([0x40, 0x02]) + pid)
            elif typ == 12:  # PINGREQ
                conn.sendall(bytes([0xD0, 0x00]))
                log('PING')
            elif typ == 14:  # DISCONNECT
                log('DISCONNECT (clean)')
                break
            else:
                log('packet type %d len %d' % (typ, len(body)))
    except Exception as e:
        log('connection ended: %r' % e)
    finally:
        stop.set()
        conn.close()

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('0.0.0.0', PORT))
    srv.listen(2)
    srv.settimeout(1.0)
    log('listening on %d for %d s' % (PORT, DURATION))
    end = time.time() + DURATION
    while time.time() < end:
        try:
            conn, addr = srv.accept()
        except socket.timeout:
            continue
        threading.Thread(target=handle, args=(conn, addr), daemon=True).start()
    log('broker stopped')

if __name__ == '__main__':
    main()
