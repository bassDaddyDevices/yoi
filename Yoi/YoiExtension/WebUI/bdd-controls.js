// bdd-controls.js
// Bass Daddy Devices controls: knobs, sliders, switches, screen readouts and pop-up menus, each
// bound to one plug-in parameter from the descriptor, plus page tabs. Shared by every Bass Daddy
// Devices synth; the synth's panel decides where they go, what they're called and what colour
// they are.
//
// Every change goes to the plug-in as a gesture (bdd.beginEdit, bdd.edit, bdd.endEdit), which is
// what hosts record automation from. Each control has `set(value)` for values coming back from
// the plug-in, and ignores them while the user is holding it.
//
// Needs bdd-bridge.js and bdd-pixel.js.

(function () {
    'use strict';

    const SVG = 'http://www.w3.org/2000/svg';

    // MARK: - Values

    function isStepped(parameter) {
        return Boolean(parameter.options) || parameter.unit === 'indexed' || parameter.unit === 'boolean';
    }

    /** 0...1 for a value, on a log scale where the parameter asks for one. */
    function toPosition(parameter, value) {
        const span = parameter.max - parameter.min;
        if (span <= 0) {
            return 0;
        }
        let position;
        if (parameter.log && parameter.min > 0) {
            position = Math.log(value / parameter.min) / Math.log(parameter.max / parameter.min);
        } else {
            position = (value - parameter.min) / span;
        }
        return Math.min(1, Math.max(0, position));
    }

    function fromPosition(parameter, position) {
        const clamped = Math.min(1, Math.max(0, position));
        let value;
        if (parameter.log && parameter.min > 0) {
            value = parameter.min * Math.pow(parameter.max / parameter.min, clamped);
        } else {
            value = parameter.min + clamped * (parameter.max - parameter.min);
        }
        return isStepped(parameter) ? Math.round(value) : value;
    }

    function trim(number, digits) {
        return number.toFixed(digits);
    }

    /** The value as text, e.g. "800 Hz", "1.40 kHz", "45%", "×5.0". */
    function format(parameter, value) {
        if (parameter.options) {
            return parameter.options[Math.round(value)] ?? String(Math.round(value));
        }
        switch (parameter.unit) {
            case 'hz':
                return value >= 1000 ? trim(value / 1000, 2) + ' kHz' : Math.round(value) + ' Hz';
            case 'ms':
                if (value >= 1000) return trim(value / 1000, 2) + ' s';
                return (value < 10 ? trim(value, 1) : Math.round(value)) + ' ms';
            case 'percent': return Math.round(value) + '%';
            case 'db': return (value <= parameter.min ? '-inf' : trim(value, 1)) + ' dB';
            case 'octaves': return trim(value, 1) + ' oct';
            case 'ratio': return '×' + trim(value, 1);
            case 'rate': return trim(value, 2) + '×';
            case 'semitones': return Math.round(value) + ' st';
            case 'boolean': return value >= 0.5 ? 'On' : 'Off';
            default: return (value > 0.004 ? '+' : '') + trim(value, 2);
        }
    }

    // MARK: - Gestures

    /** Begin/edit/end for one parameter, sending only real changes. */
    function makeGesture(parameter, apply) {
        let active = false;
        let last = null;
        let wheelTimer = null;
        return {
            get active() {
                return active;
            },
            begin() {
                if (!active) {
                    active = true;
                    last = null;
                    bdd.beginEdit(parameter.id);
                }
            },
            edit(value) {
                this.begin();
                if (value !== last) {
                    last = value;
                    bdd.edit(parameter.id, value);
                    apply(value);
                }
            },
            end() {
                if (active) {
                    active = false;
                    bdd.endEdit(parameter.id);
                }
            },
            /** A complete gesture setting one value, as a click or a reset does. */
            once(value) {
                this.begin();
                this.edit(value);
                this.end();
            },
            /** Scroll-wheel edits have no natural end, so they end after a short pause. */
            wheel(value) {
                this.begin();
                this.edit(value);
                clearTimeout(wheelTimer);
                wheelTimer = setTimeout(() => this.end(), 300);
            },
        };
    }

    /**
     * Vertical (and horizontal) dragging, the scroll wheel, the keyboard and double-click-to-reset
     * for anything that holds a value. `step` is the position change per keyboard press.
     */
    function attachDragging(element, parameter, control, options = {}) {
        const pixelsForFullRange = options.pixelsForFullRange || 220;
        const stepped = isStepped(parameter);
        const stepCount = Math.max(1, parameter.max - parameter.min);
        let drag = null;

        const positionOf = () => toPosition(parameter, control.value);

        element.addEventListener('pointerdown', (event) => {
            if (event.button !== 0) {
                return;
            }
            try {
                element.setPointerCapture(event.pointerId);
            } catch (error) {
                // Not a live pointer (a synthetic event).
            }
            element.focus({ preventScroll: true });
            const start = positionOf();
            drag = { x: event.clientX, y: event.clientY, start, position: start, pending: 0, total: 0, travel: 0, moved: false };
            // Knobs touch on press (hosts latch touch automation then). Anything a click can
            // also act on waits for real movement, so a click sends no empty gesture.
            if (!options.onClick) {
                control.gesture.begin();
            }
            element.classList.add('held');
            event.preventDefault();
        });

        element.addEventListener('pointermove', (event) => {
            if (!drag) {
                return;
            }
            // Up and right both increase. Movement is applied in steps, so holding Shift for
            // fine control part-way through a drag doesn't make the value jump.
            const delta = (drag.y - event.clientY) + (event.clientX - drag.x);
            drag.x = event.clientX;
            drag.y = event.clientY;
            drag.pending += delta;
            drag.travel += Math.abs(delta);
            if (!drag.moved && drag.travel < 3) {
                return;
            }
            drag.moved = true;
            const applied = drag.pending;
            drag.pending = 0;
            let position;
            if (stepped) {
                drag.total += applied;
                position = drag.start + Math.trunc(drag.total / (options.pixelsPerStep || 14)) / stepCount;
            } else {
                const fine = event.shiftKey ? 0.1 : 1;
                drag.position = Math.min(1, Math.max(0, drag.position + (applied / pixelsForFullRange) * fine));
                position = drag.position;
            }
            control.gesture.edit(fromPosition(parameter, position));
        });

        const finish = (event) => {
            if (!drag) {
                return;
            }
            const clicked = !drag.moved;
            drag = null;
            element.classList.remove('held');
            control.gesture.end();
            if (clicked && options.onClick) {
                options.onClick(event);
            }
        };
        element.addEventListener('pointerup', finish);
        element.addEventListener('pointercancel', () => {
            drag = null;
            element.classList.remove('held');
            control.gesture.end();
        });

        element.addEventListener('dblclick', () => {
            if (options.resetOnDoubleClick !== false && parameter.default !== undefined) {
                control.gesture.once(parameter.default);
            }
        });

        element.addEventListener('wheel', (event) => {
            event.preventDefault();
            const delta = -(event.deltaY || 0) + (event.deltaX || 0);
            if (delta === 0) {
                return;
            }
            let position;
            if (stepped) {
                position = positionOf() + Math.sign(delta) / stepCount;
            } else {
                // Trackpads send many small deltas, mouse wheels a few big ones whose size
                // varies by browser; capping each event keeps a wheel notch a small nudge.
                const scale = event.deltaMode === 1 ? 0.02 : 0.0015;
                const change = Math.sign(delta) * Math.min(Math.abs(delta * scale), 0.02);
                position = positionOf() + change * (event.shiftKey ? 0.1 : 1);
            }
            control.gesture.wheel(fromPosition(parameter, position));
        }, { passive: false });

        element.addEventListener('keydown', (event) => {
            const small = stepped ? 1 / stepCount : (event.shiftKey ? 0.001 : 0.01);
            const large = stepped ? 1 / stepCount : 0.1;
            let position = null;
            switch (event.key) {
                case 'ArrowUp': case 'ArrowRight': position = positionOf() + small; break;
                case 'ArrowDown': case 'ArrowLeft': position = positionOf() - small; break;
                case 'PageUp': position = positionOf() + large; break;
                case 'PageDown': position = positionOf() - large; break;
                case 'Home': position = 0; break;
                case 'End': position = 1; break;
                case 'Enter': case ' ':
                    if (options.onClick) {
                        options.onClick(event);
                        event.preventDefault();
                    }
                    return;
                default: return;
            }
            event.preventDefault();
            control.gesture.once(fromPosition(parameter, position));
        });
    }

    function describe(element, parameter, value) {
        element.setAttribute('aria-valuenow', String(value));
        element.setAttribute('aria-valuetext', format(parameter, value));
    }

    function makeSlider(element, parameter, label) {
        element.tabIndex = 0;
        element.setAttribute('role', 'slider');
        element.setAttribute('aria-label', label || parameter.name);
        element.setAttribute('aria-valuemin', String(parameter.min));
        element.setAttribute('aria-valuemax', String(parameter.max));
        element.title = parameter.name;
    }

    // MARK: - Knob

    /**
     * A chunky knob: a coloured dial with a thick black rim and a pointer, its name curving over
     * the top and its value in a little screen underneath.
     *   options: { label, color, size (dial diameter, px), format(value), onChange(value) }
     */
    function knob(parameter, options = {}) {
        const size = options.size || 56;
        const ring = Math.max(4, Math.round(size * 0.11));
        const radius = size / 2;
        const labelRadius = radius + ring / 2 + 4;
        const fontSize = options.fontSize || (size >= 64 ? 12 : 11);
        const width = Math.ceil(2 * (labelRadius + fontSize * 0.6));
        const centerX = width / 2;
        const centerY = labelRadius + fontSize + 2;
        const height = Math.ceil(centerY + radius + ring / 2 + 2);
        const arcId = `bdd-knob-arc-${parameter.id}-${Math.random().toString(36).slice(2, 7)}`;

        const element = document.createElement('div');
        element.className = 'knob';
        makeSlider(element, parameter, options.label);

        const svg = document.createElementNS(SVG, 'svg');
        svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
        svg.setAttribute('width', String(width));
        svg.setAttribute('height', String(height));
        svg.setAttribute('aria-hidden', 'true');
        svg.innerHTML = `
            <defs><path id="${arcId}" d="M ${centerX - labelRadius} ${centerY} A ${labelRadius} ${labelRadius} 0 0 1 ${centerX + labelRadius} ${centerY}"/></defs>
            <text class="knob-label" style="font-size:${fontSize}px"><textPath href="#${arcId}" startOffset="50%" text-anchor="middle"></textPath></text>
            <circle class="knob-shadow" cx="${centerX + 2}" cy="${centerY + 3}" r="${radius}"/>
            <circle class="knob-body" cx="${centerX}" cy="${centerY}" r="${radius}" stroke-width="${ring}"/>
            <circle class="knob-shine" cx="${centerX - radius * 0.28}" cy="${centerY - radius * 0.3}" r="${radius * 0.22}"/>
            <line class="knob-pointer" x1="${centerX}" y1="${centerY}" x2="${centerX}" y2="${centerY - radius + ring * 0.2}" stroke-width="${Math.max(2, size * 0.045)}"/>`;
        svg.querySelector('textPath').textContent = (options.label || parameter.name).toUpperCase();
        svg.querySelector('.knob-body').style.fill = options.color || '#ffd000';
        const pointer = svg.querySelector('.knob-pointer');
        element.appendChild(svg);

        const readout = document.createElement('div');
        readout.className = 'knob-value';
        element.appendChild(readout);

        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (control.gesture.active) {
                    return;
                }
                show(value);
            },
        };
        function show(value) {
            control.value = value;
            const angle = -135 + 270 * toPosition(parameter, value);
            pointer.setAttribute('transform', `rotate(${angle} ${centerX} ${centerY})`);
            readout.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control);
        show(control.value);
        return control;
    }

    // MARK: - Slider

    /**
     * A chunky vertical fader: a black track that fills with colour, a cap to grab, the name on
     * top and the value underneath. Drags like a knob, one pixel per pixel, so the cap stays
     * under the pointer.
     *   options: { label, color, height (track, px), format(value), onChange(value) }
     */
    function slider(parameter, options = {}) {
        const height = options.height || 180;
        const capHeight = 18;
        const travel = height - capHeight;

        const element = document.createElement('div');
        element.className = 'slider';
        makeSlider(element, parameter, options.label);
        element.setAttribute('aria-orientation', 'vertical');
        if (options.color) {
            element.style.setProperty('--slider-color', options.color);
        }

        const name = document.createElement('div');
        name.className = 'slider-label';
        name.textContent = (options.label || parameter.name).toUpperCase();
        const track = document.createElement('div');
        track.className = 'slider-track';
        track.style.height = `${height}px`;
        const fill = document.createElement('div');
        fill.className = 'slider-fill';
        const cap = document.createElement('div');
        cap.className = 'slider-cap';
        cap.style.height = `${capHeight}px`;
        track.append(fill, cap);
        const readout = document.createElement('div');
        readout.className = 'slider-value';
        element.append(name, track, readout);

        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (control.gesture.active) {
                    return;
                }
                show(value);
            },
        };
        function show(value) {
            control.value = value;
            const offset = toPosition(parameter, value) * travel;
            cap.style.bottom = `${offset}px`;
            fill.style.height = `${offset + capHeight / 2}px`;
            readout.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control, { pixelsForFullRange: travel });
        show(control.value);
        return control;
    }

    // MARK: - Tabs

    /**
     * Page tabs, like Max's live.tab: a row of buttons, one lit. Not tied to a parameter.
     *   options: { selected, onSelect(index), label }
     */
    function tabs(names, options = {}) {
        const element = document.createElement('div');
        element.className = 'tabs';
        element.setAttribute('role', 'tablist');
        if (options.label) {
            element.setAttribute('aria-label', options.label);
        }
        let current = -1;
        const buttons = names.map((name, index) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'tab';
            button.setAttribute('role', 'tab');
            button.setAttribute('aria-label', name);
            button.appendChild(bdd.pixel.text(name.toUpperCase(), 2));
            button.addEventListener('click', () => select(index, true));
            button.addEventListener('keydown', (event) => {
                const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
                if (step !== 0) {
                    event.preventDefault();
                    const next = (current + step + names.length) % names.length;
                    select(next, true);
                    buttons[next].focus();
                }
            });
            element.appendChild(button);
            return button;
        });
        function select(index, fromUser) {
            if (index === current || index < 0 || index >= names.length) {
                return;
            }
            current = index;
            buttons.forEach((button, i) => {
                button.classList.toggle('selected', i === index);
                button.setAttribute('aria-selected', String(i === index));
                button.tabIndex = i === index ? 0 : -1;
            });
            if (fromUser && options.onSelect) {
                options.onSelect(index);
            }
        }
        select(options.selected || 0, false);
        return {
            element,
            buttons,
            select: (index) => select(index, true),
            get selected() {
                return current;
            },
        };
    }

    // MARK: - Switch

    /**
     * A row of buttons for a parameter with a few named values.
     *   options: { labels (defaults to the parameter's options), values (the value each button
     *              sets, defaulting to one per option in order), color, onChange(value) }
     * A value no button stands for (an option left off the panel) lights no button.
     */
    function segmented(parameter, options = {}) {
        const labels = options.labels || parameter.options || ['Off', 'On'];
        const values = options.values || labels.map((_, index) => index + parameter.min);
        const element = document.createElement('div');
        element.className = 'switch';
        element.setAttribute('role', 'radiogroup');
        element.setAttribute('aria-label', options.label || parameter.name);
        element.title = parameter.name;
        if (options.color) {
            element.style.setProperty('--switch-color', options.color);
        }

        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                show(value);
            },
        };
        // `options.icons`: one SVG per button, drawn in place of its label. The label still names
        // the button, for screen readers and as its tooltip. The markup must come from the
        // product's own code, never from a parameter or preset name.
        const icons = options.icons || [];
        const buttons = labels.map((label, index) => {
            const button = document.createElement('button');
            button.type = 'button';
            if (icons[index]) {
                button.innerHTML = icons[index];
                button.classList.add('icon');
                button.setAttribute('aria-label', label);
                button.title = label;
            } else {
                button.textContent = label;
            }
            button.setAttribute('role', 'radio');
            button.addEventListener('click', () => control.gesture.once(values[index]));
            element.appendChild(button);
            return button;
        });
        function show(value) {
            control.value = value;
            const index = values.indexOf(Math.round(value));
            buttons.forEach((button, i) => {
                button.classList.toggle('selected', i === index);
                button.setAttribute('aria-checked', String(i === index));
            });
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        show(control.value);
        return control;
    }

    // MARK: - Screen readout

    /**
     * A value on a screen, in the pixel font, that works like a Max number box: drag it up and
     * down, scroll it, double-click to reset. A click flips a two-way setting or, for lists,
     * opens a menu.
     *   options: { label, format(value) (pixel font text), menuColumns, menuItems, scale, onChange(value) }
     */
    function readout(parameter, options = {}) {
        const scale = options.scale || 2;
        const element = document.createElement('div');
        element.className = 'readout';
        makeSlider(element, parameter, options.label);

        if (options.label) {
            const name = bdd.pixel.text(options.label, scale);
            name.classList.add('readout-name');
            element.appendChild(name);
        }
        const valueSvg = bdd.pixel.text('', scale);
        valueSvg.classList.add('readout-value');
        element.appendChild(valueSvg);

        const text = options.format || ((value) => format(parameter, value).toUpperCase());
        const stepped = isStepped(parameter);
        const count = Math.round(parameter.max - parameter.min) + 1;
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (control.gesture.active) {
                    return;
                }
                show(value);
            },
        };
        function show(value) {
            control.value = value;
            bdd.pixel.setText(valueSvg, text(value), scale);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });

        let onClick = null;
        if (stepped && count === 2) {
            onClick = () => control.gesture.once(control.value >= parameter.min + 0.5 ? parameter.min : parameter.max);
        } else if (stepped) {
            onClick = () => {
                const items = options.menuItems || (parameter.options || []).map((name) => name.toUpperCase());
                openMenu(element, {
                    items,
                    selected: Math.round(control.value - parameter.min),
                    columns: options.menuColumns || 1,
                    onPick: (index) => control.gesture.once(parameter.min + index),
                });
            };
        }
        if (onClick) {
            element.classList.add('clickable');
        }
        attachDragging(element, parameter, control, {
            onClick,
            pixelsPerStep: 10,
            pixelsForFullRange: 260,
            resetOnDoubleClick: !stepped,
        });
        show(control.value);
        return control;
    }

    // MARK: - Menu

    let openedMenu = null;

    function closeMenu() {
        if (openedMenu) {
            openedMenu.remove();
            openedMenu = null;
        }
    }

    /**
     * A pop-up list under `anchor`, inside the page's stage so it scales with it. Inside the
     * modern look (a `.bdd-modern` ancestor) its items are plain text; elsewhere pixel text.
     *   { items: [text], selected, columns, onPick(index) }
     */
    function openMenu(anchor, { items, selected = -1, columns = 1, onPick }) {
        closeMenu();
        const modern = Boolean(anchor.closest('.bdd-modern'));
        const stage = document.getElementById('stage') || document.body;
        const scale = stage.getBoundingClientRect().width / (stage.offsetWidth || 1) || 1;
        const stageBox = stage.getBoundingClientRect();
        const anchorBox = anchor.getBoundingClientRect();

        const menu = document.createElement('div');
        menu.className = 'menu';
        menu.setAttribute('role', 'listbox');
        menu.style.gridTemplateColumns = `repeat(${columns}, auto)`;
        items.forEach((item, index) => {
            const option = document.createElement('button');
            option.type = 'button';
            option.className = 'menu-item';
            option.setAttribute('role', 'option');
            option.setAttribute('aria-selected', String(index === selected));
            option.setAttribute('aria-label', item);
            if (index === selected) {
                option.classList.add('selected');
            }
            if (modern) {
                option.textContent = item;
            } else {
                option.appendChild(bdd.pixel.text(item, 2));
            }
            option.addEventListener('click', (event) => {
                event.stopPropagation();
                closeMenu();
                onPick(index);
            });
            menu.appendChild(option);
        });
        stage.appendChild(menu);
        openedMenu = menu;

        // Below the anchor if it fits in the stage, otherwise above it.
        const left = (anchorBox.left - stageBox.left) / scale;
        const below = (anchorBox.bottom - stageBox.top) / scale + 4;
        const above = (anchorBox.top - stageBox.top) / scale - 4 - menu.offsetHeight;
        const top = below + menu.offsetHeight <= stage.offsetHeight - 4 ? below : Math.max(4, above);
        menu.style.left = `${Math.max(4, Math.min(left, stage.offsetWidth - menu.offsetWidth - 4))}px`;
        menu.style.top = `${top}px`;
        const current = menu.querySelector('.selected') || menu.firstChild;
        if (current) {
            current.focus({ preventScroll: true });
        }
    }

    document.addEventListener('pointerdown', (event) => {
        if (openedMenu && !openedMenu.contains(event.target)) {
            closeMenu();
        }
    }, true);
    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
            closeMenu();
        }
    });

    // MARK: - Modern controls
    // The redesign's controls (YOI_DOCS decisions/redesign-macros), styled by bdd-modern.css.
    // Same contract as the pixel ones above: { element, parameter, value, set(value), gesture }.

    /** An SVG arc for a dial: 270 degrees clockwise from the bottom left, `fraction` of the way. */
    function dialArc(center, radius, fraction) {
        const at = (f) => {
            const angle = (-225 + 270 * f) * Math.PI / 180;
            return [center + radius * Math.cos(angle), center + radius * Math.sin(angle)];
        };
        const [x0, y0] = at(0);
        if (fraction <= 0.001) {
            return `M ${x0.toFixed(2)} ${y0.toFixed(2)}`;
        }
        const [x1, y1] = at(Math.min(1, fraction));
        const large = 270 * fraction > 180 ? 1 : 0;
        return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${radius} ${radius} 0 ${large} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
    }

    /**
     * A dial: an accent arc on a track around a softly lit body with a pointer, the name and the
     * value underneath.
     *   options: { label, size (px, default 84), format(value), onChange(value) }
     */
    function dial(parameter, options = {}) {
        const size = options.size || 84;
        const center = size / 2;
        const stroke = Math.max(3, Math.round(size * 0.048));
        const radius = center - stroke;
        const bodySize = Math.round(size * 0.667);
        const element = document.createElement('div');
        element.className = 'bdd-dial';
        makeSlider(element, parameter, options.label);
        const face = document.createElement('div');
        face.className = 'bdd-dial-face';
        face.style.width = face.style.height = `${size}px`;
        const svg = document.createElementNS(SVG, 'svg');
        svg.setAttribute('viewBox', `0 0 ${size} ${size}`);
        svg.setAttribute('width', String(size));
        svg.setAttribute('height', String(size));
        svg.setAttribute('aria-hidden', 'true');
        const track = document.createElementNS(SVG, 'path');
        track.setAttribute('class', 'bdd-dial-track');
        track.setAttribute('stroke-width', String(stroke));
        track.setAttribute('d', dialArc(center, radius, 1));
        const arc = document.createElementNS(SVG, 'path');
        arc.setAttribute('class', 'bdd-dial-arc');
        arc.setAttribute('stroke-width', String(stroke));
        svg.append(track, arc);
        const body = document.createElement('div');
        body.className = 'bdd-dial-body';
        body.style.width = body.style.height = `${bodySize}px`;
        body.style.left = body.style.top = `${(size - bodySize) / 2}px`;
        const pointer = document.createElement('div');
        pointer.className = 'bdd-dial-pointer';
        body.appendChild(pointer);
        face.append(svg, body);
        const name = document.createElement('span');
        name.className = 'bdd-label';
        name.textContent = options.label || parameter.name;
        const readout = document.createElement('span');
        readout.className = 'bdd-value';
        element.append(face, name, readout);
        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (!control.gesture.active) {
                    show(value);
                }
            },
        };
        function show(value) {
            control.value = value;
            const position = toPosition(parameter, value);
            arc.setAttribute('d', dialArc(center, radius, position));
            body.style.transform = `rotate(${-135 + 270 * position}deg)`;
            readout.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control);
        show(control.value);
        return control;
    }

    /**
     * A slim horizontal slider with its name and value above it, or all three in a row (`inline`).
     *   options: { label, inline, travel (px for the full range, default 280), format(value), onChange(value) }
     */
    function fader(parameter, options = {}) {
        const element = document.createElement('div');
        element.className = options.inline ? 'bdd-fader inline' : 'bdd-fader';
        makeSlider(element, parameter, options.label);
        const name = document.createElement('span');
        name.className = options.inline ? 'bdd-label' : 'bdd-fader-name';
        name.textContent = options.label || parameter.name;
        const readout = document.createElement('span');
        readout.className = 'bdd-value';
        const track = document.createElement('div');
        track.className = 'bdd-fader-track';
        const fill = document.createElement('div');
        fill.className = 'bdd-fader-fill';
        const thumb = document.createElement('div');
        thumb.className = 'bdd-fader-thumb';
        track.append(fill, thumb);
        if (options.inline) {
            element.append(name, track, readout);
        } else {
            const head = document.createElement('div');
            head.className = 'bdd-fader-head';
            head.append(name, readout);
            element.append(head, track);
        }
        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (!control.gesture.active) {
                    show(value);
                }
            },
        };
        function show(value) {
            control.value = value;
            const percent = `${toPosition(parameter, value) * 100}%`;
            fill.style.width = percent;
            thumb.style.left = percent;
            readout.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control, { pixelsForFullRange: options.travel || 280 });
        show(control.value);
        return control;
    }

    /** Segmented choice buttons in a well. The options of segmented(), plus { vertical, stretch }. */
    function choice(parameter, options = {}) {
        const control = segmented(parameter, options);
        control.element.className = 'bdd-choice' + (options.vertical ? ' vertical' : '') + (options.stretch ? ' stretch' : '');
        control.element.style.removeProperty('--switch-color');
        return control;
    }

    /**
     * LEVEL: a tall fader with a stereo meter beside it. `control.meter(left, right)` takes 0...1.
     *   options: { label, travel (px for the full range, default 240), format(value), onChange(value) }
     */
    function level(parameter, options = {}) {
        const element = document.createElement('div');
        element.className = 'bdd-level';
        makeSlider(element, parameter, options.label);
        const body = document.createElement('div');
        body.className = 'bdd-level-body';
        const track = document.createElement('div');
        track.className = 'bdd-level-track';
        const fill = document.createElement('div');
        fill.className = 'bdd-level-fill';
        const thumb = document.createElement('div');
        thumb.className = 'bdd-level-thumb';
        track.append(fill, thumb);
        const meter = document.createElement('div');
        meter.className = 'bdd-level-meter';
        meter.setAttribute('aria-hidden', 'true');
        const bars = [0, 1].map(() => {
            const bar = document.createElement('div');
            bar.className = 'bdd-level-bar';
            const lit = document.createElement('div');
            lit.style.height = '0%';
            bar.appendChild(lit);
            meter.appendChild(bar);
            return lit;
        });
        body.append(track, meter);
        const name = document.createElement('span');
        name.className = 'bdd-label';
        name.textContent = options.label || parameter.name;
        const readout = document.createElement('span');
        readout.className = 'bdd-value';
        element.append(body, name, readout);
        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (!control.gesture.active) {
                    show(value);
                }
            },
            meter(left, right) {
                bars[0].style.height = `${Math.min(1, Math.max(0, left)) * 100}%`;
                bars[1].style.height = `${Math.min(1, Math.max(0, right)) * 100}%`;
            },
        };
        function show(value) {
            control.value = value;
            const percent = `${toPosition(parameter, value) * 100}%`;
            fill.style.height = percent;
            thumb.style.bottom = percent;
            readout.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control, { pixelsForFullRange: options.travel || 240 });
        show(control.value);
        return control;
    }

    /**
     * A button showing a list parameter's value that opens a menu of its options, as the rate and
     * direction over the drawing do.
     *   options: { label (for screen readers), columns, format(value), onChange(value) }
     */
    function menuButton(parameter, options = {}) {
        const element = document.createElement('button');
        element.type = 'button';
        element.className = 'bdd-menu-button';
        element.setAttribute('aria-label', options.label || parameter.name);
        element.setAttribute('aria-haspopup', 'listbox');
        const items = parameter.options || [];
        const text = options.format || ((value) => items[Math.round(value) - parameter.min] ?? format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                show(value);
            },
        };
        function show(value) {
            control.value = value;
            element.textContent = text(value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        element.addEventListener('click', () => {
            openMenu(element, {
                items,
                selected: Math.round(control.value) - parameter.min,
                columns: options.columns || 1,
                onPick: (index) => control.gesture.once(index + parameter.min),
            });
        });
        show(control.value);
        return control;
    }

    /**
     * A button-shaped readout for a continuous value, dragged up and down like a dial (the
     * drawing's free time, where a menu can't list the values). Wheel, arrow keys and
     * double-click-to-reset work as on the dial.
     *   options: { label, travel (px for the full range, default 300), format(value), onChange(value) }
     */
    function valueButton(parameter, options = {}) {
        const element = document.createElement('div');
        element.className = 'bdd-menu-button bdd-value-button';
        makeSlider(element, parameter, options.label);
        const text = options.format || ((value) => format(parameter, value));
        const control = {
            element,
            parameter,
            value: parameter.default ?? parameter.min,
            set(value) {
                if (!control.gesture.active) {
                    show(value);
                }
            },
        };
        function show(value) {
            control.value = value;
            element.textContent = text(value);
            describe(element, parameter, value);
        }
        control.gesture = makeGesture(parameter, (value) => {
            show(value);
            if (options.onChange) {
                options.onChange(value);
            }
        });
        attachDragging(element, parameter, control, { pixelsForFullRange: options.travel || 300 });
        show(control.value);
        return control;
    }

    /**
     * Tabs in the modern look, for pages of a panel (not bound to a parameter): segmented buttons
     * that stretch to fill their row.
     *   options: { label, selected, onSelect(index) }
     * Returns { element, select(index) }.
     */
    function tabBar(names, options = {}) {
        const element = document.createElement('div');
        element.className = 'bdd-choice stretch bdd-tabs';
        element.setAttribute('role', 'tablist');
        if (options.label) {
            element.setAttribute('aria-label', options.label);
        }
        const buttons = names.map((name, index) => {
            const button = document.createElement('button');
            button.type = 'button';
            button.textContent = name;
            button.setAttribute('role', 'tab');
            button.addEventListener('click', () => select(index, true));
            element.appendChild(button);
            return button;
        });
        function select(index, fromUser) {
            buttons.forEach((button, i) => {
                button.classList.toggle('selected', i === index);
                button.setAttribute('aria-selected', String(i === index));
                button.tabIndex = i === index ? 0 : -1;
            });
            if (fromUser && options.onSelect) {
                options.onSelect(index);
            }
        }
        element.addEventListener('keydown', (event) => {
            const current = buttons.findIndex((button) => button.classList.contains('selected'));
            const move = { ArrowLeft: -1, ArrowRight: 1 }[event.key];
            if (move) {
                event.preventDefault();
                const next = (current + move + buttons.length) % buttons.length;
                select(next, true);
                buttons[next].focus();
            }
        });
        select(options.selected || 0, false);
        return { element, select: (index) => select(index, false) };
    }

    /**
     * An XY pad for two parameters (the CHARACTER pad): X across, Y up. A drag sets both as one
     * move, a gesture on each, so hosts record two lanes; a click jumps the dot there. Arrow keys
     * nudge (shift for bigger steps), the wheel moves Y (X with shift), double-click resets both.
     * Positions come from the screen, so it stays right when the panel is scaled.
     *   options: { label, size (px, default 300), inset (px kept clear at the edges, default 16),
     *              xLabel, yLabel, onChange(xValue, yValue) }
     * Returns { element, controls: [x, y] }, each control with the usual contract.
     */
    function pad(xParameter, yParameter, options = {}) {
        const size = options.size || 300;
        // The dot travels inside this margin, so at the extremes it and its halo stay whole.
        const inset = options.inset ?? 16;
        const clamp01 = (value) => Math.min(1, Math.max(0, value));
        const element = document.createElement('div');
        element.className = 'bdd-pad bdd-well';
        element.style.width = element.style.height = `${size}px`;
        element.tabIndex = 0;
        element.setAttribute('role', 'slider');
        element.setAttribute('aria-label', options.label || `${xParameter.name} and ${yParameter.name}`);
        element.innerHTML = `
            <svg class='bdd-pad-art' viewBox='0 0 300 300' preserveAspectRatio='none' aria-hidden='true'>
                <path class='bdd-pad-grid' d='M75 0V300M150 0V300M225 0V300M0 75H300M0 150H300M0 225H300'></path>
                <path class='bdd-pad-peak-fill' d='M0 250 C 70 250, 150 248, 196 232 C 222 222, 232 150, 246 150 C 260 150, 266 238, 300 262 L300 300 L0 300Z'></path>
                <path class='bdd-pad-peak' d='M0 250 C 70 250, 150 248, 196 232 C 222 222, 232 150, 246 150 C 260 150, 266 238, 300 262'></path>
            </svg>
            <div class='bdd-pad-cross-x'></div>
            <div class='bdd-pad-cross-y'></div>
            <div class='bdd-pad-dot'></div>
            <span class='bdd-pad-axis bottom'></span>
            <span class='bdd-pad-axis top'></span>`;
        element.querySelector('.bdd-pad-axis.bottom').textContent = options.xLabel || `${xParameter.name} \u2192`;
        element.querySelector('.bdd-pad-axis.top').textContent = options.yLabel || `\u2191 ${yParameter.name}`;
        const dot = element.querySelector('.bdd-pad-dot');
        const crossX = element.querySelector('.bdd-pad-cross-x');
        const crossY = element.querySelector('.bdd-pad-cross-y');

        function make(parameter) {
            const control = {
                element,
                parameter,
                value: parameter.default ?? parameter.min,
                set(value) {
                    if (!control.gesture.active) {
                        control.value = value;
                        draw();
                    }
                },
            };
            control.gesture = makeGesture(parameter, (value) => {
                control.value = value;
                draw();
                if (options.onChange) {
                    options.onChange(x.value, y.value);
                }
            });
            return control;
        }
        const x = make(xParameter);
        const y = make(yParameter);

        function draw() {
            const across = toPosition(xParameter, x.value);
            const up = toPosition(yParameter, y.value);
            dot.style.left = crossY.style.left = `calc(${inset}px + ${across} * (100% - ${2 * inset}px))`;
            dot.style.top = crossX.style.top = `calc(${inset}px + ${1 - up} * (100% - ${2 * inset}px))`;
            element.setAttribute('aria-valuetext',
                `${xParameter.name} ${format(xParameter, x.value)}, ${yParameter.name} ${format(yParameter, y.value)}`);
        }
        function moveTo(event) {
            const box = element.getBoundingClientRect();
            const margin = inset * (box.width / element.offsetWidth);   // in screen pixels, as scaled
            x.gesture.edit(fromPosition(xParameter, clamp01((event.clientX - box.left - margin) / (box.width - 2 * margin))));
            y.gesture.edit(fromPosition(yParameter, clamp01(1 - (event.clientY - box.top - margin) / (box.height - 2 * margin))));
        }

        let dragging = false;
        element.addEventListener('pointerdown', (event) => {
            if (event.button !== 0) {
                return;
            }
            element.setPointerCapture(event.pointerId);
            element.focus();
            dragging = true;
            x.gesture.begin();
            y.gesture.begin();
            moveTo(event);
            event.preventDefault();
        });
        element.addEventListener('pointermove', (event) => {
            if (dragging) {
                moveTo(event);
            }
        });
        const end = () => {
            if (dragging) {
                dragging = false;
                x.gesture.end();
                y.gesture.end();
            }
        };
        element.addEventListener('pointerup', end);
        element.addEventListener('pointercancel', end);
        element.addEventListener('dblclick', () => {
            x.gesture.once(xParameter.default ?? xParameter.min);
            y.gesture.once(yParameter.default ?? yParameter.min);
        });
        element.addEventListener('keydown', (event) => {
            const step = event.shiftKey ? 0.1 : 0.01;
            const moves = { ArrowLeft: [x, -step], ArrowRight: [x, step], ArrowDown: [y, -step], ArrowUp: [y, step] };
            const move = moves[event.key];
            if (!move) {
                return;
            }
            event.preventDefault();
            const [target, delta] = move;
            target.gesture.once(fromPosition(target.parameter, clamp01(toPosition(target.parameter, target.value) + delta)));
        });
        element.addEventListener('wheel', (event) => {
            event.preventDefault();
            const target = event.shiftKey ? x : y;
            const amount = event.shiftKey ? (event.deltaX || event.deltaY) : event.deltaY;
            target.gesture.wheel(fromPosition(target.parameter, clamp01(toPosition(target.parameter, target.value) - amount * 0.002)));
        }, { passive: false });

        draw();
        return { element, controls: [x, y] };
    }

    window.bdd = window.bdd || {};
    window.bdd.controls = {
        toPosition,
        fromPosition,
        format,
        knob,
        slider,
        tabs,
        segmented,
        readout,
        openMenu,
        closeMenu,
        dial,
        fader,
        choice,
        level,
        menuButton,
        valueButton,
        tabBar,
        pad,
    };
})();
