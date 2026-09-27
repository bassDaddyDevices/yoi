// bdd-curve.js
// Bass Daddy Devices curve editor: draws a drawn envelope on a canvas and lets you edit its
// points, the way Max's `function` object works:
//
//   click empty space        add a point (and keep dragging to place it)
//   drag a point             move it (the first and last points stay at the ends)
//   shift-click a point      delete it (so does double-click or right-click)
//   option/alt-drag          bend the segment under the pointer: drag the line where you want it
//   hold ⌘ (Ctrl) dragging   snap to a 1/16 grid across and 1/8 up
//
// The editor only moves points. The plug-in works out the curve and sends back a table of it,
// which is what gets drawn, so the screen always shows exactly what the envelope plays.
//
// Shared by every Bass Daddy Devices synth; nothing here knows about YOI.
//
//   const editor = bdd.curveEditor(canvas, { onEdit(points) {...}, onHint(text) {...} });
//   editor.setPoints([[x, y, bend], ...]); editor.setTable([...]); editor.setPlayhead(x, y);

(function () {
    'use strict';

    const DEFAULTS = {
        maxPoints: 64,
        inset: 14,
        pointRadius: 5,
        hitRadius: 10,
        colors: {
            line: '#f5e27a',
            glow: 'rgba(245, 226, 122, 0.45)',
            bend: '#ffffff',
            point: '#f5e27a',
            pointFill: '#000000',
            gridMajor: '#262626',
            gridMinor: '#121212',
            playhead: 'rgba(245, 226, 122, 0.28)',
            playheadDot: '#ffffff',
            label: 'rgba(245, 226, 122, 0.55)',
        },
    };

    const mac = /Mac/.test(navigator.platform || navigator.userAgent);
    const HINTS = {
        idle: `CLICK ADD · DRAG MOVE · SHIFT-CLICK DELETE · ${mac ? 'OPTION' : 'ALT'}-DRAG BEND · ${mac ? '⌘' : 'CTRL'} SNAP`,
        point: 'DRAG TO MOVE · SHIFT-CLICK OR DOUBLE-CLICK TO DELETE',
        end: 'END POINT: DRAG UP AND DOWN',
        bend: 'DRAG UP OR DOWN TO BEND THIS SEGMENT',
        full: 'THAT’S THE MOST POINTS A DRAWING CAN HAVE',
    };

    function clamp(value, low, high) {
        return Math.min(high, Math.max(low, value));
    }

    function curveEditor(canvas, userOptions = {}) {
        const options = Object.assign({}, DEFAULTS, userOptions);
        const colors = Object.assign({}, DEFAULTS.colors, userOptions.colors || {});
        const context = canvas.getContext('2d');

        const state = {
            points: [[0, 0.5, 0], [1, 0.5, 0]],
            table: [],
            playhead: null,
            labels: { top: '', bottom: '' },
            action: null,     // { kind: 'move' | 'bend', index, ... } while the pointer is down
            hover: -1,        // point under the pointer
            hoverSegment: -1, // segment that an alt-drag would bend
            altHeld: false,
            pointerInside: false,
            lastPointer: null,
        };

        // MARK: Geometry (canvas CSS pixels, ignoring any scaling of the page around it)

        function box() {
            return { width: canvas.offsetWidth, height: canvas.offsetHeight };
        }

        function toScreen(x, y) {
            const { width, height } = box();
            const inset = options.inset;
            return [inset + x * (width - 2 * inset), inset + (1 - y) * (height - 2 * inset)];
        }

        function fromScreen(px, py) {
            const { width, height } = box();
            const inset = options.inset;
            return [(px - inset) / (width - 2 * inset), 1 - (py - inset) / (height - 2 * inset)];
        }

        /** Pointer position in canvas CSS pixels, correct even when the page is scaled. */
        function local(event) {
            const bounds = canvas.getBoundingClientRect();
            const { width, height } = box();
            return [
                (event.clientX - bounds.left) * (width / (bounds.width || 1)),
                (event.clientY - bounds.top) * (height / (bounds.height || 1)),
            ];
        }

        function pointAt(px, py) {
            let best = -1;
            let bestDistance = options.hitRadius;
            state.points.forEach(([x, y], index) => {
                const [sx, sy] = toScreen(x, y);
                const distance = Math.hypot(sx - px, sy - py);
                if (distance <= bestDistance) {
                    best = index;
                    bestDistance = distance;
                }
            });
            return best;
        }

        /** The segment spanning `x` (0...1); a vertical jump is never chosen. */
        function segmentAt(x) {
            const points = state.points;
            for (let i = 0; i < points.length - 1; i++) {
                if (points[i + 1][0] > points[i][0] && x >= points[i][0] && x <= points[i + 1][0]) {
                    return i;
                }
            }
            return -1;
        }

        function snap(value, steps, enabled) {
            return enabled ? Math.round(value * steps) / steps : value;
        }

        // MARK: Drawing

        let drawPending = false;
        function requestDraw() {
            if (!drawPending) {
                drawPending = true;
                requestAnimationFrame(() => {
                    drawPending = false;
                    draw();
                });
            }
        }

        function resize() {
            const bounds = canvas.getBoundingClientRect();
            const { width, height } = box();
            const ratio = (window.devicePixelRatio || 1) * (bounds.width / (width || 1) || 1);
            canvas.width = Math.max(1, Math.round(width * ratio));
            canvas.height = Math.max(1, Math.round(height * ratio));
            context.setTransform(ratio, 0, 0, ratio, 0, 0);
            draw();
        }

        function strokeTable(from, to, color, lineWidth, glow) {
            const table = state.table;
            if (table.length < 2) {
                return;
            }
            const last = table.length - 1;
            const first = Math.max(0, Math.floor(from * last));
            const end = Math.min(last, Math.ceil(to * last));
            context.save();
            context.strokeStyle = color;
            context.lineWidth = lineWidth;
            context.lineJoin = 'round';
            context.lineCap = 'round';
            if (glow) {
                context.shadowColor = glow;
                context.shadowBlur = 10;
            }
            context.beginPath();
            for (let i = first; i <= end; i++) {
                const [x, y] = toScreen(i / last, table[i]);
                if (i === first) {
                    context.moveTo(x, y);
                } else {
                    context.lineTo(x, y);
                }
            }
            context.stroke();
            context.restore();
        }

        function draw() {
            const { width, height } = box();
            const inset = options.inset;
            context.clearRect(0, 0, width, height);

            // Grid: sixteenths across, eighths up, with the quarters brighter.
            context.lineWidth = 1;
            for (let i = 1; i < 16; i++) {
                const [x] = toScreen(i / 16, 0);
                context.strokeStyle = i % 4 === 0 ? colors.gridMajor : colors.gridMinor;
                context.beginPath();
                context.moveTo(Math.round(x) + 0.5, inset);
                context.lineTo(Math.round(x) + 0.5, height - inset);
                context.stroke();
            }
            for (let i = 1; i < 8; i++) {
                const [, y] = toScreen(0, i / 8);
                context.strokeStyle = i % 4 === 0 ? colors.gridMajor : colors.gridMinor;
                context.beginPath();
                context.moveTo(inset, Math.round(y) + 0.5);
                context.lineTo(width - inset, Math.round(y) + 0.5);
                context.stroke();
            }

            // The range labels, if the synth gave any.
            context.font = '700 10px ui-monospace, "SF Mono", Menlo, Consolas, monospace';
            context.fillStyle = colors.label;
            context.textAlign = 'right';
            if (state.labels.top) {
                context.textBaseline = 'top';
                context.fillText(state.labels.top, width - inset - 4, inset + 3);
            }
            if (state.labels.bottom) {
                context.textBaseline = 'bottom';
                context.fillText(state.labels.bottom, width - inset - 4, height - inset - 3);
            }

            // The curve, as the plug-in plays it.
            strokeTable(0, 1, colors.line, 4, colors.glow);

            // The segment an alt-drag would bend (or is bending).
            const bending = state.action && state.action.kind === 'bend' ? state.action.index : -1;
            const highlighted = bending >= 0 ? bending : (state.altHeld && state.pointerInside ? state.hoverSegment : -1);
            if (highlighted >= 0 && highlighted < state.points.length - 1) {
                strokeTable(state.points[highlighted][0], state.points[highlighted + 1][0], colors.bend, 4, null);
            }

            // Playhead.
            if (state.playhead) {
                const [headX, headY] = toScreen(state.playhead.x, state.playhead.y);
                context.strokeStyle = colors.playhead;
                context.lineWidth = 2;
                context.beginPath();
                context.moveTo(Math.round(headX), inset);
                context.lineTo(Math.round(headX), height - inset);
                context.stroke();
                context.save();
                context.shadowColor = colors.glow;
                context.shadowBlur = 12;
                context.fillStyle = colors.playheadDot;
                context.beginPath();
                context.arc(headX, headY, 4, 0, Math.PI * 2);
                context.fill();
                context.restore();
            }

            // Points: a ring with a dark middle, as on the mock-up.
            const active = state.action && state.action.kind === 'move' ? state.action.index : -1;
            state.points.forEach(([x, y], index) => {
                const [px, py] = toScreen(x, y);
                const large = index === active || (index === state.hover && !state.altHeld);
                const radius = options.pointRadius + (large ? 2 : 0);
                context.beginPath();
                context.arc(px, py, radius, 0, Math.PI * 2);
                context.fillStyle = index === active ? colors.point : colors.pointFill;
                context.fill();
                context.lineWidth = 2.5;
                context.strokeStyle = colors.point;
                context.stroke();
                if (index !== active) {
                    context.beginPath();
                    context.arc(px, py, 1.5, 0, Math.PI * 2);
                    context.fillStyle = colors.point;
                    context.fill();
                }
            });
        }

        // MARK: Sending edits

        let sendPending = false;
        function emitSoon() {
            if (!sendPending) {
                sendPending = true;
                requestAnimationFrame(() => {
                    sendPending = false;
                    emitNow();
                });
            }
        }

        function emitNow() {
            if (options.onEdit) {
                options.onEdit(state.points.map((point) => point.slice()));
            }
        }

        // MARK: Hints and cursor

        let lastHint = null;
        function hint(text) {
            if (text !== lastHint) {
                lastHint = text;
                if (options.onHint) {
                    options.onHint(text);
                }
            }
        }

        function updateHover(px, py) {
            state.hover = pointAt(px, py);
            const [x] = fromScreen(px, py);
            state.hoverSegment = segmentAt(clamp(x, 0, 1));
            let cursor = 'crosshair';
            if (state.altHeld) {
                cursor = state.hoverSegment >= 0 ? 'ns-resize' : 'default';
                hint(HINTS.bend);
            } else if (state.hover >= 0) {
                const isEnd = state.hover === 0 || state.hover === state.points.length - 1;
                cursor = isEnd ? 'ns-resize' : 'move';
                hint(isEnd ? HINTS.end : HINTS.point);
            } else {
                hint(state.points.length >= options.maxPoints ? HINTS.full : HINTS.idle);
            }
            canvas.style.cursor = cursor;
            requestDraw();
        }

        // MARK: Editing

        function deletePoint(index) {
            if (index <= 0 || index >= state.points.length - 1) {
                return false;
            }
            state.points.splice(index, 1);
            state.hover = -1;
            emitNow();
            requestDraw();
            return true;
        }

        function movePoint(index, x, y, snapping) {
            const points = state.points;
            y = clamp(snap(y, 8, snapping), 0, 1);
            if (index === 0) {
                x = 0;
            } else if (index === points.length - 1) {
                x = 1;
            } else {
                x = clamp(snap(x, 16, snapping), points[index - 1][0], points[index + 1][0]);
            }
            points[index] = [x, y, points[index][2] || 0];
        }

        canvas.addEventListener('pointerdown', (event) => {
            if (event.button !== 0) {
                return;
            }
            const [px, py] = local(event);
            const [x, y] = fromScreen(px, py);
            state.altHeld = event.altKey;

            if (event.altKey) {
                const index = segmentAt(clamp(x, 0, 1));
                if (index < 0) {
                    return;
                }
                const rise = state.points[index + 1][1] - state.points[index][1];
                state.action = { kind: 'bend', index, startY: py, startBend: state.points[index][2] || 0, direction: Math.sign(rise) };
            } else {
                let index = pointAt(px, py);
                if (index >= 0 && event.shiftKey) {
                    deletePoint(index);
                    event.preventDefault();
                    return;
                }
                if (index < 0) {
                    if (state.points.length >= options.maxPoints) {
                        hint(HINTS.full);
                        return;
                    }
                    // Insert in x order, never before the first point or after the last.
                    const newX = clamp(x, 0, 1);
                    index = state.points.findIndex((point) => point[0] > newX);
                    index = clamp(index < 0 ? state.points.length - 1 : index, 1, state.points.length - 1);
                    state.points.splice(index, 0, [newX, clamp(y, 0, 1), 0]);
                    movePoint(index, newX, y, false);
                    emitSoon();
                }
                state.action = { kind: 'move', index };
            }
            try {
                canvas.setPointerCapture(event.pointerId);
            } catch (error) {
                // Not a live pointer (a synthetic event); dragging still works inside the canvas.
            }
            event.preventDefault();
            requestDraw();
        });

        canvas.addEventListener('pointermove', (event) => {
            const [px, py] = local(event);
            state.pointerInside = true;
            state.lastPointer = [px, py];
            const action = state.action;
            if (!action) {
                state.altHeld = event.altKey;
                updateHover(px, py);
                return;
            }
            if (action.kind === 'move') {
                const [x, y] = fromScreen(px, py);
                movePoint(action.index, x, y, event.metaKey || event.ctrlKey);
            } else {
                // Dragging up pulls the line up: bulge upward on a rise means starting fast.
                const up = (action.startY - py) / 80;
                let bend = clamp(action.startBend - action.direction * up, -1, 1);
                if (Math.abs(bend) < 0.04) {
                    bend = 0;   // easy to find straight again
                }
                state.points[action.index][2] = bend;
            }
            emitSoon();
            requestDraw();
        });

        const finish = () => {
            if (state.action) {
                state.action = null;
                emitNow();
                requestDraw();
            }
        };
        canvas.addEventListener('pointerup', finish);
        canvas.addEventListener('pointercancel', finish);

        canvas.addEventListener('dblclick', (event) => {
            const [px, py] = local(event);
            deletePoint(pointAt(px, py));
        });

        canvas.addEventListener('contextmenu', (event) => {
            event.preventDefault();
            const [px, py] = local(event);
            deletePoint(pointAt(px, py));
        });

        canvas.addEventListener('pointerleave', () => {
            state.pointerInside = false;
            state.hover = -1;
            hint(null);
            requestDraw();
        });

        const modifierChanged = (event) => {
            if (event.altKey !== state.altHeld && !state.action) {
                state.altHeld = event.altKey;
                if (state.pointerInside && state.lastPointer) {
                    updateHover(...state.lastPointer);
                }
            }
        };
        window.addEventListener('keydown', modifierChanged);
        window.addEventListener('keyup', modifierChanged);
        window.addEventListener('blur', () => {
            state.altHeld = false;
            requestDraw();
        });

        return {
            /** True while the user is holding a point or bending; incoming points wait until then. */
            get editing() {
                return state.action !== null;
            },
            setPoints(points) {
                if (!state.action && Array.isArray(points) && points.length >= 2) {
                    state.points = points.map((point) => [point[0], point[1], point[2] || 0]);
                    requestDraw();
                }
            },
            setTable(table) {
                state.table = table || [];
                requestDraw();
            },
            setPlayhead(x, y) {
                state.playhead = (x === null || x === undefined) ? null : { x: clamp(x, 0, 1), y: clamp(y, 0, 1) };
                requestDraw();
            },
            setLabels(top, bottom) {
                state.labels = { top: top || '', bottom: bottom || '' };
                requestDraw();
            },
            resize,
            redraw: requestDraw,
        };
    }

    window.bdd = window.bdd || {};
    window.bdd.curveEditor = curveEditor;
})();
