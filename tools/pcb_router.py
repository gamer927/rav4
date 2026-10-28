#!/usr/bin/env python3
# SPDX-License-Identifier: CC-BY-NC-SA-4.0
# Copyright (c) 2026 gamer927. RAV4 TCU, https://github.com/gamer927/rav4
"""A small grid router for the TCU board (tools/pcb.py).

Tracks run on a 1.27 mm grid on both copper layers, in straight lines between
grid points (no diagonals), changing layer through vias. Every pad blocks the
grid around it for other nets, with the clearance and the track's own width
taken into account, so the result is legal by construction; KiCad's own design
rule check then has the last word.

Each net is joined pad by pad (A* search from the part of the net already
routed to the nearest unrouted pad). Nets that fail are moved to the front of
the queue and everything is routed again, a few times.
"""
import heapq
import math

G = 1.27          # grid pitch, mm
CL = 0.2          # copper-to-copper clearance, mm
EDGE = 0.6        # copper-to-board-edge clearance, mm
VIA_COST = 12.0
WRONG_WAY = 1.6   # top layer prefers left-right, bottom front-back


class Pad:
    def __init__(self, net, x, y, shape, sx, sy, angle=0.0, keepout=False):
        self.net, self.x, self.y, self.shape, self.sx, self.sy = net, x, y, shape, sx, sy
        self.angle = angle
        self.keepout = keepout

    def dist(self, px, py):
        """Distance from a point to the pad's copper (0 inside)."""
        dx, dy = px - self.x, py - self.y
        if self.angle:
            a = math.radians(self.angle)
            dx, dy = dx * math.cos(a) + dy * math.sin(a), -dx * math.sin(a) + dy * math.cos(a)
        hx, hy = self.sx / 2, self.sy / 2
        if self.shape == "circle":
            return max(0.0, math.hypot(dx, dy) - hx)
        if self.shape == "oval":
            r = min(hx, hy)
            ex, ey = max(abs(dx) - (hx - r), 0.0), max(abs(dy) - (hy - r), 0.0)
            return max(0.0, math.hypot(ex, ey) - r)
        ex, ey = max(abs(dx) - hx, 0.0), max(abs(dy) - hy, 0.0)
        return math.hypot(ex, ey)


class Router:
    def __init__(self, width, height, inside, half_width, via_half):
        """inside(x, y, margin) -> True if the point is that far inside the board.
        half_width: net -> half the track width (mm). via_half: net -> via pad radius."""
        self.nx, self.ny = int(width / G) + 1, int(height / G) + 1
        self.inside = inside
        self.hw = half_width
        self.vh = via_half
        self.pads = []
        self.keepouts = []   # (layer or None, x0, y0, x1, y1)
        self.occ = [dict(), dict()]      # node -> net, per layer
        self.occw = [dict(), dict()]     # node -> half width of what's there
        self.paths = {}                  # net -> list of paths (list of (layer, i, j))
        self.stubs = {}                  # net -> list of (layer, (x, y), (i, j))

    def xy(self, i, j):
        return i * G, j * G

    # ---- static obstacles
    def build(self, nets):
        self.nets = nets
        classes = sorted(set(self.hw[n] for n in nets) | set(self.vh[n] for n in nets))
        self.cls = {h: k for k, h in enumerate(classes)}
        # pad_block[k][node] = net allowed there (>= 0) or -2 (nobody) for class k; absent = free
        self.pad_block = [dict() for _ in classes]
        self.access = {}  # pad index -> list of nodes (i, j) on both layers
        for k, h in enumerate(classes):
            for pi, p in enumerate(self.pads):
                reach = max(p.sx, p.sy) / 2 + h + CL
                i0, i1 = int((p.x - reach) / G) - 1, int((p.x + reach) / G) + 2
                j0, j1 = int((p.y - reach) / G) - 1, int((p.y + reach) / G) + 2
                for i in range(max(i0, 0), min(i1, self.nx)):
                    for j in range(max(j0, 0), min(j1, self.ny)):
                        x, y = self.xy(i, j)
                        if p.dist(x, y) < h + CL:
                            cur = self.pad_block[k].get((i, j), -1)
                            owner = -2 if p.keepout else p.net
                            if cur == -1:
                                self.pad_block[k][(i, j)] = owner
                            elif cur != owner:
                                self.pad_block[k][(i, j)] = -2
        # Edge: nodes too close to the board edge, per class
        self.edge_bad = [set() for _ in classes]
        for k, h in enumerate(classes):
            for i in range(self.nx):
                for j in range(self.ny):
                    if not self.inside(*self.xy(i, j), EDGE + h):
                        self.edge_bad[k].add((i, j))

    def pad_nodes(self, pi):
        """Grid nodes a track can start from for pad pi: inside its copper, or the nearest."""
        p = self.pads[pi]
        h = self.hw[p.net]
        k = self.cls[h]
        best = []
        r = max(p.sx, p.sy) / 2 + G
        for i in range(int((p.x - r) / G), int((p.x + r) / G) + 2):
            for j in range(int((p.y - r) / G), int((p.y + r) / G) + 2):
                if not (0 <= i < self.nx and 0 <= j < self.ny):
                    continue
                x, y = self.xy(i, j)
                d = p.dist(x, y)
                if (i, j) in self.edge_bad[k] or self.pad_block[k].get((i, j), -1) not in (-1, p.net):
                    continue
                best.append((d, math.hypot(x - p.x, y - p.y), (i, j)))
        best.sort()
        inside = [n for d, _, n in best if d == 0]
        return inside if inside else [n for _, _, n in best[:1]]

    def free(self, net, layer, node, h, k):
        if node in self.edge_bad[k]:
            return False
        if self.pad_block[k].get(node, -1) not in (-1, net):
            return False
        for (ko_layer, x0, y0, x1, y1) in self.keepouts:
            if ko_layer in (None, layer):
                x, y = self.xy(*node)
                if x0 - h - CL < x < x1 + h + CL and y0 - h - CL < y < y1 + h + CL:
                    return False
        o = self.occ[layer]
        cur = o.get(node)
        if cur is not None and cur != net:
            return False
        i, j = node
        ow = self.occw[layer]
        for q in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
            other = o.get(q)
            if other is not None and other != net and h + ow[q] + CL > G + 1e-9:
                return False
        return True

    def occupy(self, net, layer, node, h):
        self.occ[layer][node] = net
        self.occw[layer][node] = max(self.occw[layer].get(node, 0.0), h)

    # ---- search
    def astar(self, net, sources, targets):
        h = self.hw[net]
        k = self.cls[h]
        vh = self.vh[net]
        kv = self.cls[vh]
        tset = set(targets)
        txs = [t[1] for t in targets]
        tys = [t[2] for t in targets]
        tx0, tx1, ty0, ty1 = min(txs), max(txs), min(tys), max(tys)

        def heur(i, j):
            dx = max(tx0 - i, 0, i - tx1)
            dy = max(ty0 - j, 0, j - ty1)
            return dx + dy

        openq = []
        g = {}
        came = {}
        for s in sources:
            g[s] = 0.0
            heapq.heappush(openq, (heur(s[1], s[2]), 0.0, s))
        while openq:
            f, gc, cur = heapq.heappop(openq)
            if gc > g.get(cur, 1e18):
                continue
            if cur in tset:
                path = [cur]
                while path[-1] in came:
                    path.append(came[path[-1]])
                return path[::-1]
            layer, i, j = cur
            steps = []
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ni, nj = i + di, j + dj
                if 0 <= ni < self.nx and 0 <= nj < self.ny:
                    cost = 1.0 if (layer == 0) == (dj == 0) else WRONG_WAY
                    steps.append(((layer, ni, nj), cost))
            other = 1 - layer
            if (self.free(net, other, (i, j), vh, kv) and self.free(net, layer, (i, j), vh, kv)):
                steps.append(((other, i, j), VIA_COST))
            for nxt, cost in steps:
                nl, ni, nj = nxt
                if nl == layer and not self.free(net, nl, (ni, nj), h, k):
                    continue
                ng = gc + cost
                if ng < g.get(nxt, 1e18):
                    g[nxt] = ng
                    came[nxt] = cur
                    heapq.heappush(openq, (ng + heur(ni, nj), ng, nxt))
        return None

    def route_net(self, net, pad_ids):
        h = self.hw[net]
        vh = self.vh[net]
        acc = {pi: self.pad_nodes(pi) for pi in pad_ids}
        if any(not acc[pi] for pi in pad_ids):
            return False
        tree = set()
        first = pad_ids[0]
        for n in acc[first]:
            tree.add((0, *n))
            tree.add((1, *n))
        self._stub(net, first, acc[first])
        left = set(pad_ids[1:])
        paths = []
        while left:
            targets = {}
            for pi in left:
                for n in acc[pi]:
                    targets[(0, *n)] = pi
                    targets[(1, *n)] = pi
            path = self.astar(net, list(tree), list(targets))
            if path is None:
                return False
            reached = targets[path[-1]]
            left.discard(reached)
            self._stub(net, reached, acc[reached])
            for a in path:
                tree.add(a)
            for n in acc[reached]:
                tree.add((0, *n))
                tree.add((1, *n))
            for idx, (l, i, j) in enumerate(path):
                self.occupy(net, l, (i, j), h)
                if idx and path[idx - 1][0] != l:     # a via: both layers
                    self.occupy(net, 0, (i, j), vh)
                    self.occupy(net, 1, (i, j), vh)
            paths.append(path)
        self.paths.setdefault(net, []).extend(paths)
        return True

    def _stub(self, net, pi, nodes):
        p = self.pads[pi]
        for n in nodes:
            for layer in (0, 1):
                self.occupy(net, layer, n, self.hw[net])
        self.stubs.setdefault(net, []).append((pi, nodes[0]))

    def clear(self):
        self.occ = [dict(), dict()]
        self.occw = [dict(), dict()]
        self.paths = {}
        self.stubs = {}

    def route_all(self, order, pads_of, passes=6, log=print):
        failed = []
        for attempt in range(passes):
            self.clear()
            # every pad's copper reserves its own access nodes first
            failed = []
            for net in order:
                if len(pads_of[net]) < 2:
                    continue
                if not self.route_net(net, pads_of[net]):
                    failed.append(net)
            log(f"  routing pass {attempt + 1}: {len(order) - len(failed)} nets routed, {len(failed)} failed"
                + (": " + " ".join(failed[:12]) if failed else ""))
            if not failed:
                return []
            order = failed + [n for n in order if n not in failed]
        return failed
