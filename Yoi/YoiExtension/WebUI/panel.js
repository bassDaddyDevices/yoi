// panel.js
// The editor foundation: a control for every parameter the plug-in reports, grouped as in its
// parameter tree, and the drawn envelope with its playhead.
//
// Drag a point to reshape the drawing. The page only moves points: the plug-in works out the
// curve and sends it back, so what's drawn here is exactly what the envelope plays.
//
// The funky 8-bit YOI panel replaces the look and layout; the bridge and this behaviour stay.

(function () {
    'use strict';

    const groupOrder = ['envelope', 'grit', 'filter', 'oscillators', 'amp', 'voice', 'output'];
    const groupNames = {
        envelope: 'Drawn Envelope',
        grit: 'Grit',
        filter: 'Filter',
        oscillators: 'Oscillators',
        amp: 'Amp Envelope',
        voice: 'Voice',
        output: 'Output',
    };

    const state = {
        controls: new Map(),   // parameter id -> { set(value) }
        points: [],            // the drawing, [[x, y, bend], ...]
        table: [],             // the curve as the plug-in plays it
        display: { position: 0, value: 0 },
        dragging: null,        // index of the point being dragged
    };

    // MARK: - Values

    /** Slider position 0...1 for a value, on a log scale where the parameter asks for one. */
    function toPosition(parameter, value) {
        if (parameter.log && parameter.min > 0) {
            return Math.log(value / parameter.min) / Math.log(parameter.max / parameter.min);
        }
        return (value - parameter.min) / (parameter.max - parameter.min);
    }

    function fromPosition(parameter, position) {
        if (parameter.log && parameter.min > 0) {
            return parameter.min * Math.pow(parameter.max / parameter.min, position);
        }
        return parameter.min + position * (parameter.max - parameter.min);
    }

    function format(parameter, value) {
        switch (parameter.unit) {
            case 'hz': return value >= 1000 ? (value / 1000).toFixed(2) + ' kHz' : Math.round(value) + ' Hz';
            case 'ms':
                if (value >= 1000) return (value / 1000).toFixed(2) + ' s';
                return (value < 10 ? value.toFixed(1) : Math.round(value)) + ' ms';
            case 'percent': return Math.round(value) + '%';
            case 'db': return value.toFixed(1) + ' dB';
            case 'octaves': return value.toFixed(1) + ' oct';
            case 'ratio': return '×' + value.toFixed(1);
            case 'rate': return value.toFixed(2) + '×';
            case 'semitones': return Math.round(value) + ' st';
            default: return value.toFixed(2);
        }
    }

    // MARK: - Controls

    function build(descriptor) {
        const container = document.getElementById('controls');
        container.textContent = '';
        state.controls.clear();

        const groups = new Map();
        for (const parameter of descriptor.parameters) {
            if (!groups.has(parameter.group)) {
                groups.set(parameter.group, []);
            }
            groups.get(parameter.group).push(parameter);
        }

        const rank = (key) => (groupOrder.includes(key) ? groupOrder.indexOf(key) : groupOrder.length);
        for (const key of [...groups.keys()].sort((a, b) => rank(a) - rank(b))) {
            const section = document.createElement('section');
            section.className = 'group';
            const heading = document.createElement('h2');
            heading.textContent = groupNames[key] || key;
            section.appendChild(heading);
            for (const parameter of groups.get(key)) {
                section.appendChild(makeControl(parameter));
            }
            container.appendChild(section);
        }

        const shapes = document.getElementById('shapes');
        shapes.textContent = '';
        const prompt = new Option('choose…', '');
        prompt.disabled = true;
        shapes.appendChild(prompt);
        descriptor.shapes.forEach((name, index) => shapes.appendChild(new Option(name, String(index))));
        shapes.selectedIndex = 0;
        shapes.onchange = () => {
            if (shapes.value !== '') {
                bdd.loadShape(Number(shapes.value));
            }
            shapes.selectedIndex = 0;
        };
    }

    function makeControl(parameter) {
        const wrapper = document.createElement('div');
        wrapper.className = 'control';
        const label = document.createElement('div');
        label.className = 'label';
        const name = document.createElement('span');
        name.textContent = parameter.name;
        const valueText = document.createElement('span');
        valueText.className = 'value';
        label.append(name, valueText);
        wrapper.appendChild(label);

        const choices = parameter.options || (parameter.unit === 'boolean' ? ['Off', 'On'] : null);
        if (choices && choices.length <= 4) {
            const row = document.createElement('div');
            row.className = 'segments';
            const buttons = choices.map((text, index) => {
                const button = document.createElement('button');
                button.textContent = text;
                button.onclick = () => choose(parameter, index);
                row.appendChild(button);
                return button;
            });
            wrapper.appendChild(row);
            state.controls.set(parameter.id, {
                set(value) {
                    const index = Math.round(value);
                    buttons.forEach((button, i) => button.classList.toggle('selected', i === index));
                },
            });
        } else if (choices) {
            const select = document.createElement('select');
            choices.forEach((text, index) => select.appendChild(new Option(text, String(index))));
            select.onchange = () => choose(parameter, Number(select.value));
            wrapper.appendChild(select);
            state.controls.set(parameter.id, {
                set(value) {
                    select.value = String(Math.round(value));
                },
            });
        } else {
            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = '0';
            slider.max = '1000';
            slider.step = '1';
            let editing = false;
            const begin = () => {
                if (!editing) {
                    editing = true;
                    bdd.beginEdit(parameter.id);
                }
            };
            const end = () => {
                if (editing) {
                    editing = false;
                    bdd.endEdit(parameter.id);
                }
            };
            slider.addEventListener('pointerdown', begin);
            slider.addEventListener('input', () => {
                begin();   // keyboard changes arrive without a pointer
                const value = fromPosition(parameter, Number(slider.value) / 1000);
                valueText.textContent = format(parameter, value);
                bdd.edit(parameter.id, value);
            });
            slider.addEventListener('change', end);
            slider.addEventListener('pointerup', end);
            wrapper.appendChild(slider);
            state.controls.set(parameter.id, {
                set(value) {
                    if (!editing) {
                        slider.value = String(Math.round(toPosition(parameter, value) * 1000));
                    }
                    valueText.textContent = format(parameter, value);
                },
            });
        }
        return wrapper;
    }

    /** A click on a choice is a whole gesture: begin, set, end. */
    function choose(parameter, index) {
        bdd.beginEdit(parameter.id);
        bdd.edit(parameter.id, index);
        bdd.endEdit(parameter.id);
        state.controls.get(parameter.id).set(index);
    }

    // MARK: - The drawing

    const canvas = document.getElementById('curve');
    const context = canvas.getContext('2d');
    const inset = 12;
    const accent = '#f6e27a';
    let drawPending = false;

    function size() {
        const bounds = canvas.getBoundingClientRect();
        return { width: bounds.width, height: bounds.height };
    }

    function resize() {
        const ratio = window.devicePixelRatio || 1;
        const { width, height } = size();
        canvas.width = Math.round(width * ratio);
        canvas.height = Math.round(height * ratio);
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        draw();
    }

    function toScreen(x, y) {
        const { width, height } = size();
        return [inset + x * (width - 2 * inset), inset + (1 - y) * (height - 2 * inset)];
    }

    function fromScreen(px, py) {
        const { width, height } = size();
        return [(px - inset) / (width - 2 * inset), 1 - (py - inset) / (height - 2 * inset)];
    }

    function requestDraw() {
        if (!drawPending) {
            drawPending = true;
            requestAnimationFrame(() => {
                drawPending = false;
                draw();
            });
        }
    }

    function draw() {
        const { width, height } = size();
        context.clearRect(0, 0, width, height);

        context.strokeStyle = '#1d1d22';
        context.lineWidth = 1;
        for (let quarter = 1; quarter < 4; quarter++) {
            const [x] = toScreen(quarter / 4, 0);
            context.beginPath();
            context.moveTo(x, inset);
            context.lineTo(x, height - inset);
            context.stroke();
        }

        if (state.table.length > 1) {
            context.strokeStyle = accent;
            context.lineWidth = 3;
            context.lineJoin = 'round';
            context.beginPath();
            state.table.forEach((y, index) => {
                const [x, screenY] = toScreen(index / (state.table.length - 1), y);
                if (index === 0) {
                    context.moveTo(x, screenY);
                } else {
                    context.lineTo(x, screenY);
                }
            });
            context.stroke();
        }

        state.points.forEach(([x, y], index) => {
            const [px, py] = toScreen(x, y);
            context.beginPath();
            context.arc(px, py, index === state.dragging ? 6 : 4.5, 0, Math.PI * 2);
            context.fillStyle = '#000';
            context.fill();
            context.lineWidth = 2;
            context.strokeStyle = accent;
            context.stroke();
        });

        const [headX] = toScreen(state.display.position, 0);
        const [, headY] = toScreen(0, state.display.value);
        context.strokeStyle = 'rgba(246, 226, 122, 0.35)';
        context.lineWidth = 1;
        context.beginPath();
        context.moveTo(headX, inset);
        context.lineTo(headX, height - inset);
        context.stroke();
        context.fillStyle = '#fff';
        context.beginPath();
        context.arc(headX, headY, 3.5, 0, Math.PI * 2);
        context.fill();
    }

    function nearestPoint(px, py) {
        let best = -1;
        let bestDistance = 12;   // pixels
        state.points.forEach(([x, y], index) => {
            const [sx, sy] = toScreen(x, y);
            const distance = Math.hypot(sx - px, sy - py);
            if (distance < bestDistance) {
                best = index;
                bestDistance = distance;
            }
        });
        return best;
    }

    let sendPending = false;
    function sendCurveSoon() {
        if (!sendPending) {
            sendPending = true;
            requestAnimationFrame(() => {
                sendPending = false;
                bdd.setCurve(state.points);
            });
        }
    }

    canvas.addEventListener('pointerdown', (event) => {
        const index = nearestPoint(event.offsetX, event.offsetY);
        if (index < 0) {
            return;
        }
        state.dragging = index;
        canvas.setPointerCapture(event.pointerId);
        event.preventDefault();
        requestDraw();
    });

    canvas.addEventListener('pointermove', (event) => {
        if (state.dragging === null) {
            return;
        }
        const points = state.points;
        const index = state.dragging;
        let [x, y] = fromScreen(event.offsetX, event.offsetY);
        y = Math.min(1, Math.max(0, y));
        if (index === 0) {
            x = 0;
        } else if (index === points.length - 1) {
            x = 1;
        } else {
            x = Math.min(points[index + 1][0], Math.max(points[index - 1][0], x));
        }
        points[index] = [x, y, points[index][2] || 0];
        sendCurveSoon();
        requestDraw();
    });

    const finishDrag = () => {
        if (state.dragging !== null) {
            state.dragging = null;
            bdd.setCurve(state.points);
            requestDraw();
        }
    };
    canvas.addEventListener('pointerup', finishDrag);
    canvas.addEventListener('pointercancel', finishDrag);

    // MARK: - From the plug-in

    bdd.onReceive((incoming) => {
        if (incoming.descriptor) {
            build(incoming.descriptor);
            const status = document.getElementById('status');
            status.textContent = 'connected';
            status.classList.add('connected');
        }
        if (incoming.params) {
            for (const [id, value] of Object.entries(incoming.params)) {
                const control = state.controls.get(Number(id));
                if (control) {
                    control.set(value);
                }
            }
        }
        if (incoming.curve) {
            // While dragging, keep the points under the finger; take only the curve.
            if (state.dragging === null && incoming.curve.points) {
                state.points = incoming.curve.points.map((point) => point.slice());
            }
            if (incoming.curve.table) {
                state.table = incoming.curve.table;
            }
        }
        if (incoming.display) {
            state.display = incoming.display;
        }
        requestDraw();
    });

    window.addEventListener('resize', resize);
    resize();
    if (bdd.connected || window.bddStandIn) {
        bdd.hello();
    } else {
        document.getElementById('status').textContent = 'not in a plug-in: open YOI in a host to connect';
    }
    if (!bdd.connected && window.bddStandIn) {
        const status = document.getElementById('status');
        status.textContent = 'browser stand-in (not the plug-in)';
        status.classList.remove('connected');
    }
})();
