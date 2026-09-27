// yoi-panel.js
// YOI's panel: which controls go where, what they're called and what colour they are. The
// controls themselves (bdd-controls.js), the curve editor (bdd-curve.js), the pixel font
// (bdd-pixel.js) and the bridge (bdd-bridge.js) are shared with the other mini synths.
//
// Parameters are found by identifier, which never changes, rather than by position.

(function () {
    'use strict';

    const { knob, slider, tabs, segmented, readout, format, openMenu } = bdd.controls;
    const STAGE_WIDTH = 940;
    const STAGE_HEIGHT = 410;

    // MARK: - Layout

    const percentOr = (low, high) => (value) => {
        if (value <= 0.5) return low;
        if (value >= 99.5) return high;
        return Math.round(value) + '%';
    };

    const BLUE = 'var(--blue)';
    const PINK = 'var(--pink)';
    const YELLOW = 'var(--yellow)';
    const ORANGE = 'var(--orange)';
    const GREEN = 'var(--green)';
    const PURPLE = 'var(--purple)';
    const CORAL = 'var(--coral)';
    const CREAM = 'var(--cream)';

    /**
     * The pages beside the screen, picked with the tabs above them. Each knob page is a 3 × 2
     * grid; a switch sits under the knob it belongs to (`column` is a CSS grid column).
     * A knob with two `ids` is one knob that shows whichever parameter its switch has selected.
     */
    const PAGES = [
        {
            name: 'YOI',
            rows: [
                {
                    knobs: [
                        { id: 'cutoff', label: 'CUTOFF', color: BLUE },
                        { id: 'resonance', label: 'RES', color: BLUE },
                        { id: 'envAmount', label: 'ENV AMOUNT', color: PINK },
                    ],
                    switches: [{ id: 'filterMode', labels: ['LP', 'BP'], color: BLUE, column: '1 / span 2' }],
                },
                {
                    knobs: [
                        { ids: ['dsRate', 'dsAmount'], label: 'DOWNSAMPLE', color: YELLOW },
                        { id: 'cleanupMultiple', label: 'CLEAN-UP', color: YELLOW },
                        { id: 'foldAmount', label: 'FOLD', color: ORANGE },
                    ],
                    switches: [
                        { id: 'dsMode', labels: ['OFF', 'kHz', '%'], color: YELLOW, column: '1' },
                        { id: 'cleanupMode', labels: ['OFF', 'ON'], color: YELLOW, column: '2' },
                        { id: 'foldPosition', labels: ['PRE-FILT', 'PRE-DS'], color: ORANGE, column: '3' },
                    ],
                },
            ],
        },
        {
            name: 'OSC',
            rows: [
                {
                    knobs: [
                        { id: 'oscShape', label: 'SHAPE', color: GREEN, format: percentOr('Saw', 'Square') },
                        { id: 'subLevel', label: 'SUB', color: GREEN },
                        { id: 'subShape', label: 'SUB SHAPE', color: GREEN, format: percentOr('Sine', 'Triangle') },
                    ],
                    switches: [{ id: 'subOctave', labels: ['-1 OCT', '-2 OCT'], color: GREEN, column: '2' }],
                },
                {
                    knobs: [
                        { id: 'subCrossover', label: 'X-OVER', color: GREEN },
                        { id: 'glideTime', label: 'GLIDE', color: CORAL },
                        { id: 'bendRange', label: 'BEND', color: CORAL },
                    ],
                    switches: [{ id: 'glideMode', labels: ['LEGATO', 'ALWAYS'], color: CORAL, column: '2 / span 2' }],
                },
            ],
        },
        {
            name: 'AMP',
            sliders: [
                { id: 'ampAttack', label: 'ATTACK', color: PURPLE },
                { id: 'ampDecay', label: 'DECAY', color: PURPLE },
                { id: 'ampSustain', label: 'SUSTAIN', color: PURPLE },
                { id: 'ampRelease', label: 'RELEASE', color: PURPLE },
            ],
            knobs: [{ id: 'outputLevel', label: 'LEVEL', color: CREAM }],
        },
    ];

    const KNOB_SIZE = 64;
    const COLUMN_WIDTH = 122;
    const SLIDER_HEIGHT = 196;

    /** Directions as the mock-up shows them ("DIR: <-->"), short on the screen, long in the menu. */
    const DIRECTIONS = [
        ['-->', 'FWD', 'FORWARD'],
        ['<--', 'BWD', 'BACKWARD'],
        ['<->', 'PING', 'PINGPONG'],
        ['~~', 'SINE', 'SINE'],
        ['??', 'RAND', 'RANDOM'],
        ['>>', 'ACCEL', 'ACCELERATE'],
    ];
    const ACCELERATE = 5;

    // MARK: - State

    const parameters = new Map();   // identifier -> parameter
    const identifiers = new Map();  // address -> identifier
    const controls = new Map();     // identifier -> [control]
    const values = {};              // identifier -> value
    let built = false;
    let shapeNames = [];

    function register(identifier, control) {
        if (!controls.has(identifier)) {
            controls.set(identifier, []);
        }
        controls.get(identifier).push(control);
        values[identifier] = control.value;
        return control;
    }

    /** Called for the user's own edits, so dependent controls react at once. */
    function changed(identifier) {
        return (value) => {
            values[identifier] = value;
            for (const control of controls.get(identifier) || []) {
                if (!control.gesture.active) {
                    control.set(value);
                }
            }
            refresh();
        };
    }

    // MARK: - Building

    function pixel(text, scale, className) {
        const svg = bdd.pixel.text(text, scale);
        if (className) {
            svg.classList.add(className);
        }
        return svg;
    }

    function makeKnob(identifier, item) {
        const parameter = parameters.get(identifier);
        if (!parameter) {
            return null;
        }
        const control = knob(parameter, {
            label: item.label,
            color: item.color,
            size: KNOB_SIZE,
            format: item.format,
            onChange: changed(identifier),
        });
        control.element.dataset.parameter = identifier;
        register(identifier, control);
        return control.element;
    }

    function buildKnobPage(page, container) {
        const grid = document.createElement('div');
        grid.className = 'page-grid';
        grid.style.gridTemplateColumns = `repeat(3, ${COLUMN_WIDTH}px)`;
        page.rows.forEach((row, rowIndex) => {
            row.knobs.forEach((item, columnIndex) => {
                const cell = document.createElement('div');
                cell.className = 'page-cell';
                cell.style.gridColumn = String(columnIndex + 1);
                cell.style.gridRow = String(rowIndex * 2 + 1);
                for (const identifier of item.ids || [item.id]) {
                    const element = makeKnob(identifier, item);
                    if (element) {
                        cell.appendChild(element);
                    }
                }
                grid.appendChild(cell);
            });
            for (const item of row.switches || []) {
                const parameter = parameters.get(item.id);
                if (!parameter) {
                    continue;
                }
                const control = segmented(parameter, {
                    labels: item.labels,
                    color: item.color,
                    onChange: changed(item.id),
                });
                control.element.style.gridColumn = item.column;
                control.element.style.gridRow = String(rowIndex * 2 + 2);
                grid.appendChild(control.element);
                register(item.id, control);
            }
        });
        container.appendChild(grid);
    }

    function buildSliderPage(page, container) {
        const row = document.createElement('div');
        row.className = 'page-sliders';
        const faders = document.createElement('div');
        faders.className = 'slider-group';
        for (const item of page.sliders) {
            const parameter = parameters.get(item.id);
            if (!parameter) {
                continue;
            }
            const control = slider(parameter, {
                label: item.label,
                color: item.color,
                height: SLIDER_HEIGHT,
                onChange: changed(item.id),
            });
            faders.appendChild(control.element);
            register(item.id, control);
        }
        row.appendChild(faders);
        for (const item of page.knobs || []) {
            const element = makeKnob(item.id, item);
            if (element) {
                row.appendChild(element);
            }
        }
        container.appendChild(row);
    }

    let selectedPage = 0;

    function buildSide() {
        const side = document.getElementById('side');
        side.textContent = '';
        const pages = PAGES.map((page) => {
            const panel = document.createElement('div');
            panel.className = 'page';
            panel.setAttribute('role', 'tabpanel');
            panel.setAttribute('aria-label', page.name);
            if (page.rows) {
                buildKnobPage(page, panel);
            } else {
                buildSliderPage(page, panel);
            }
            return panel;
        });
        const show = (index) => {
            selectedPage = index;
            pages.forEach((panel, i) => {
                panel.hidden = i !== index;
            });
        };
        const selector = tabs(PAGES.map((page) => page.name), {
            selected: selectedPage,
            label: 'Control pages',
            onSelect: show,
        });
        side.append(selector.element, ...pages);
        show(selectedPage);
    }

    function stripGroup(...children) {
        const group = document.createElement('div');
        group.className = 'strip-group';
        group.append(...children);
        return group;
    }

    function screenReadout(identifier, options) {
        const parameter = parameters.get(identifier);
        if (!parameter) {
            return document.createElement('span');
        }
        const control = readout(parameter, Object.assign({ onChange: changed(identifier) }, options));
        register(identifier, control);
        return control.element;
    }

    function buildStrip() {
        const timeRow = document.getElementById('strip-time');
        const moreRow = document.getElementById('strip-more');
        timeRow.textContent = '';
        moreRow.textContent = '';

        const syncNames = (parameters.get('envSyncLength')?.options || []).map((name) => name.toUpperCase());
        const syncTime = screenReadout('envSyncLength', {
            label: 'TIME:',
            menuColumns: 4,
            menuItems: syncNames,
            format: (value) => syncNames[Math.round(value)] || '',
        });
        syncTime.id = 'time-sync';
        const freeTime = screenReadout('envFreeTime', {
            label: 'TIME:',
            format: (value) => format(parameters.get('envFreeTime'), value).toUpperCase(),
        });
        freeTime.id = 'time-free';
        const timeMode = screenReadout('envTimeMode', {
            format: (value) => (value >= 0.5 ? 'FREE' : 'SYNC'),
        });
        timeMode.title = 'Sync to the host tempo, or free time in milliseconds';

        const direction = screenReadout('envDirection', {
            label: 'DIR:',
            menuItems: DIRECTIONS.map(([glyph, , name]) => `${glyph} ${name}`),
            format: (value) => {
                const [glyph, short] = DIRECTIONS[Math.round(value)] || DIRECTIONS[0];
                return `${glyph} ${short}`;
            },
        });

        const retrigger = screenReadout('envRetrigger', {
            label: 'RETRIG:',
            format: (value) => (value >= 0.5 ? 'ON' : 'OFF'),
        });
        retrigger.title = 'Restart the drawing on every new note';

        const rate = (value) => value.toFixed(2) + '×';
        const accel = stripGroup(
            pixel('ACCEL:', 2, 'strip-text'),
            screenReadout('accelStart', { format: rate }),
            pixel('>', 2, 'strip-text'),
            screenReadout('accelEnd', { format: rate }),
            screenReadout('accelCurve', {
                label: 'CURVE:',
                format: (value) => (value > 0.004 ? '+' : value < -0.004 ? '-' : ' ') + Math.abs(value).toFixed(2),
            }),
        );
        accel.id = 'accel';
        accel.title = 'Accelerate: how fast the drawing plays at the start and end of each pass';

        timeRow.append(stripGroup(syncTime, freeTime, timeMode), stripGroup(direction));
        moreRow.append(stripGroup(retrigger), accel);
    }

    function build(descriptor) {
        parameters.clear();
        identifiers.clear();
        controls.clear();
        for (const parameter of descriptor.parameters) {
            parameters.set(parameter.identifier, parameter);
            identifiers.set(parameter.id, parameter.identifier);
        }
        shapeNames = descriptor.shapes || [];

        buildSide();
        buildStrip();
        built = true;
        refresh();
    }

    // MARK: - Things that depend on other settings

    function setDim(identifier, dim) {
        for (const control of controls.get(identifier) || []) {
            control.element.classList.toggle('dim', dim);
        }
    }

    function setHidden(identifier, hidden) {
        for (const control of controls.get(identifier) || []) {
            control.element.hidden = hidden;
        }
    }

    function refresh() {
        if (!built) {
            return;
        }
        const v = values;
        // One DOWNSAMPLE knob: the S&H rate in kHz mode, the amount in % mode.
        const downsampleMode = Math.round(v.dsMode);
        setHidden('dsRate', downsampleMode === 2);
        setHidden('dsAmount', downsampleMode !== 2);
        setDim('dsRate', downsampleMode === 0);
        setDim('cleanupMultiple', v.cleanupMode < 0.5);
        setDim('foldPosition', v.foldAmount <= 0);
        setDim('glideMode', v.glideTime <= 0);
        setDim('subShape', v.subLevel <= 0);
        setDim('subOctave', v.subLevel <= 0);
        setDim('subCrossover', v.subLevel <= 0);   // with no sub there's nothing to split

        const free = v.envTimeMode >= 0.5;
        document.getElementById('time-sync').hidden = free;
        document.getElementById('time-free').hidden = !free;
        document.getElementById('accel').classList.toggle('dim', Math.round(v.envDirection) !== ACCELERATE);

        // The top of the drawing is the cutoff; the bottom is Amount octaves below it.
        const cutoff = parameters.get('cutoff');
        if (cutoff && v.cutoff !== undefined && v.envAmount !== undefined) {
            editor.setLabels(format(cutoff, v.cutoff).toUpperCase(),
                             format(cutoff, v.cutoff / Math.pow(2, v.envAmount)).toUpperCase());
        }
    }

    // MARK: - Screen

    const hint = document.getElementById('hint');
    const editor = bdd.curveEditor(document.getElementById('curve'), {
        onEdit: (points) => bdd.setCurve(points),
        onHint: (text) => {
            hint.textContent = text || '';
        },
    });

    const drawings = document.getElementById('drawings');
    drawings.appendChild(pixel('DRAWINGS ▾', 2));
    drawings.addEventListener('click', () => {
        openMenu(drawings, {
            items: shapeNames.map((name) => name.toUpperCase()),
            onPick: (index) => bdd.loadShape(index),
        });
    });

    // MARK: - Chrome

    function buildChrome() {
        document.getElementById('logo').appendChild(pixel('YOI', 5));
        document.querySelector('.maker').textContent = '';
        document.querySelector('.maker').appendChild(pixel('BASS DADDY DEVICES', 2));

        // Pixel skulls, after the mock-up. '#' is grey; the gaps show the panel through.
        const skulls = {
            square: [
                '..#########..',
                '.###########.',
                '#############',
                '#############',
                '##...###...##',
                '##...###...##',
                '##...###...##',
                '#############',
                '######.######',
                '.###########.',
                '..##.###.##..',
                '..##.###.##..',
            ],
            checker: [
                '..#########..',
                '.###########.',
                '#############',
                '##.#.###.#.##',
                '###.#####.###',
                '##.#.###.#.##',
                '#############',
                '#####.#.#####',
                '.###########.',
                '..#.#####.#..',
                '..#.#####.#..',
            ],
            mean: [
                '..#########..',
                '.###########.',
                '#############',
                '##.#######.##',
                '##..#####..##',
                '##...###...##',
                '#############',
                '######.######',
                '.###########.',
                '..#.#.#.#.#..',
                '..#.#.#.#.#..',
            ],
        };
        // Kept to open panel: behind knob names they'd hurt legibility.
        const placements = [
            ['square', 392, -62, 10],
            ['mean', 668, -78, 10],
            ['checker', 912, 150, 10],
            ['mean', -58, 300, 11],
        ];
        const layer = document.getElementById('skulls');
        for (const [kind, x, y, size] of placements) {
            const svg = bdd.pixel.bitmap(skulls[kind], size);
            svg.style.left = `${x}px`;
            svg.style.top = `${y}px`;
            layer.appendChild(svg);
        }

        if (!bdd.connected) {
            const badge = document.getElementById('badge');
            badge.hidden = false;
            badge.appendChild(pixel(window.bddStandIn ? 'BROWSER STAND-IN' : 'NOT CONNECTED', 1));
        }
    }

    function fit() {
        const stage = document.getElementById('stage');
        const scale = Math.min(window.innerWidth / STAGE_WIDTH, window.innerHeight / STAGE_HEIGHT);
        stage.style.transform = `scale(${scale})`;
        stage.style.left = `${Math.max(0, (window.innerWidth - STAGE_WIDTH * scale) / 2)}px`;
        stage.style.top = `${Math.max(0, (window.innerHeight - STAGE_HEIGHT * scale) / 2)}px`;
        editor.resize();
    }

    // MARK: - From the plug-in

    bdd.onReceive((incoming) => {
        if (incoming.descriptor && !built) {
            build(incoming.descriptor);
        }
        if (incoming.params) {
            for (const [id, value] of Object.entries(incoming.params)) {
                const identifier = identifiers.get(Number(id));
                if (!identifier) {
                    continue;
                }
                values[identifier] = value;
                for (const control of controls.get(identifier) || []) {
                    control.set(value);
                }
            }
            refresh();
        }
        if (incoming.curve) {
            editor.setPoints(incoming.curve.points);
            if (incoming.curve.table) {
                editor.setTable(incoming.curve.table);
            }
        }
        if (incoming.display) {
            editor.setPlayhead(incoming.display.position, incoming.display.value);
        }
    });

    buildChrome();
    window.addEventListener('resize', fit);
    fit();
    bdd.hello();
})();
