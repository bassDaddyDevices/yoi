// yoi-panel.js
// YOI's panel: which controls go where, what they're called and what colour they are. The
// controls themselves (bdd-controls.js), the curve editor (bdd-curve.js), the pixel font
// (bdd-pixel.js) and the bridge (bdd-bridge.js) are shared with the other mini synths.
//
// Parameters are found by identifier, which never changes, rather than by position.

(function () {
    'use strict';

    const { knob, segmented, readout, format, openMenu } = bdd.controls;
    const STAGE_WIDTH = 940;
    const STAGE_HEIGHT = 600;

    // MARK: - Layout

    const percentOr = (low, high) => (value) => {
        if (value <= 0.5) return low;
        if (value >= 99.5) return high;
        return Math.round(value) + '%';
    };

    /** The four knobs beside the screen, and grit: the controls that make the yoi. */
    const CORE = [
        [
            {
                title: 'FILTER', color: 'var(--blue)',
                knobs: [
                    { id: 'cutoff', label: 'CUTOFF' },
                    { id: 'resonance', label: 'RES' },
                ],
                switches: [{ id: 'filterMode', column: '1 / span 2', labels: ['LP', 'BP'] }],
            },
            {
                title: 'ENV', color: 'var(--pink)',
                knobs: [{ id: 'envAmount', label: 'AMOUNT' }],
            },
        ],
        [
            {
                title: 'GRIT', color: 'var(--yellow)',
                knobs: [
                    { id: 'dsRate', label: 'S&H RATE' },
                    { id: 'dsAmount', label: 'DS AMOUNT' },
                    { id: 'cleanupMultiple', label: 'CLEAN-UP' },
                ],
                switches: [
                    { id: 'dsMode', column: '1 / span 2', labels: ['OFF', 'S&H', 'DOWNSAMPLE'] },
                    { id: 'cleanupMode', column: '3', labels: ['OFF', 'ON'] },
                ],
            },
        ],
    ];

    /** Everything else, along the bottom. */
    const RACK = [
        {
            title: 'OSC', color: 'var(--green)',
            knobs: [
                { id: 'oscShape', label: 'SHAPE', format: percentOr('Saw', 'Square') },
                { id: 'subLevel', label: 'SUB' },
                { id: 'subShape', label: 'SUB SHAPE', format: percentOr('Sine', 'Triangle') },
            ],
            switches: [{ id: 'subOctave', column: '2 / span 2', labels: ['-1 OCT', '-2 OCT'] }],
        },
        {
            title: 'FOLD', color: 'var(--orange)',
            knobs: [{ id: 'foldAmount', label: 'FOLD' }],
            switches: [{ id: 'foldPosition', column: '1', labels: ['PRE-FILT', 'PRE-DS'] }],
        },
        {
            title: 'AMP', color: 'var(--purple)',
            knobs: [
                { id: 'ampAttack', label: 'ATTACK' },
                { id: 'ampDecay', label: 'DECAY' },
                { id: 'ampSustain', label: 'SUSTAIN' },
                { id: 'ampRelease', label: 'RELEASE' },
            ],
        },
        {
            title: 'VOICE', color: 'var(--coral)',
            knobs: [
                { id: 'glideTime', label: 'GLIDE' },
                { id: 'bendRange', label: 'BEND' },
            ],
            switches: [{ id: 'glideMode', column: '1 / span 2', labels: ['LEGATO', 'ALWAYS'] }],
        },
        {
            title: 'OUT', color: 'var(--cream)',
            knobs: [{ id: 'outputLevel', label: 'LEVEL' }],
        },
    ];

    // Knob sizes, and column widths that line the knobs up (the core is a 3 × 2 grid).
    const CORE_KNOB = 64;
    const CORE_COLUMN = 122;
    const RACK_KNOB = 48;
    const RACK_COLUMN = 76;

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

    function buildGroup(spec, knobSize, columnWidth) {
        const group = document.createElement('div');
        group.className = 'group';
        group.style.setProperty('--group-color', spec.color);

        const title = document.createElement('div');
        title.className = 'group-title';
        title.appendChild(pixel(spec.title, 2));
        group.appendChild(title);

        const body = document.createElement('div');
        body.className = 'group-body';
        body.style.gridTemplateColumns = `repeat(${spec.knobs.length}, ${columnWidth}px)`;
        group.appendChild(body);

        for (const item of spec.knobs) {
            const parameter = parameters.get(item.id);
            if (!parameter) {
                continue;
            }
            const control = knob(parameter, {
                label: item.label,
                color: spec.color,
                size: knobSize,
                format: item.format,
                onChange: changed(item.id),
            });
            body.appendChild(control.element);
            register(item.id, control);
        }
        for (const item of spec.switches || []) {
            const parameter = parameters.get(item.id);
            if (!parameter) {
                continue;
            }
            const control = segmented(parameter, {
                labels: item.labels,
                color: spec.color,
                onChange: changed(item.id),
            });
            control.element.style.gridColumn = item.column;
            control.element.style.gridRow = '2';
            body.appendChild(control.element);
            register(item.id, control);
        }
        return group;
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

        const core = document.getElementById('core');
        core.textContent = '';
        for (const rowSpec of CORE) {
            const row = document.createElement('div');
            row.className = 'core-row';
            for (const spec of rowSpec) {
                row.appendChild(buildGroup(spec, CORE_KNOB, CORE_COLUMN));
            }
            core.appendChild(row);
        }

        const rack = document.getElementById('rack');
        rack.textContent = '';
        for (const spec of RACK) {
            rack.appendChild(buildGroup(spec, RACK_KNOB, RACK_COLUMN));
        }

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

    function refresh() {
        if (!built) {
            return;
        }
        const v = values;
        setDim('dsRate', Math.round(v.dsMode) !== 1);
        setDim('dsAmount', Math.round(v.dsMode) !== 2);
        setDim('cleanupMultiple', v.cleanupMode < 0.5);
        setDim('foldPosition', v.foldAmount <= 0);
        setDim('glideMode', v.glideTime <= 0);
        setDim('subShape', v.subLevel <= 0);
        setDim('subOctave', v.subLevel <= 0);

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
        // Kept to open panel: behind titles and knob names they'd hurt legibility.
        const placements = [
            ['square', 392, -62, 10],
            ['mean', 668, -74, 10],
            ['mean', -34, 440, 11],
            ['square', 452, 488, 9],
            ['checker', 812, 470, 12],
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
