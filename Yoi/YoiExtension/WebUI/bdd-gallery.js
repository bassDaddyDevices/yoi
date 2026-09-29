// bdd-gallery.js
// A development page for the modern controls (bdd-gallery.html): each one bound to a real YOI
// parameter through the browser stand-in. Not loaded by the plug-in's panel.
(function () {
    'use strict';
    const { dial, fader, choice, level, menuButton } = bdd.controls;
    const editor = bdd.curveEditor(document.getElementById('curve'), Object.assign(bdd.curveStyles.modern(), {
        onEdit: (points) => bdd.setCurve(points),
        onHint: (text) => { document.getElementById('hint').textContent = text; },
    }));
    window.addEventListener('resize', () => editor.resize());
    const byId = new Map();
    const controls = [];
    let built = false;

    function card(child) {
        const wrap = document.createElement('div');
        wrap.className = 'bdd-card grow';
        wrap.style.padding = '18px 0';
        wrap.style.display = 'flex';
        wrap.style.justifyContent = 'center';
        wrap.appendChild(child);
        return wrap;
    }
    function add(control) {
        controls.push(control);
        return control.element;
    }

    function build(descriptor) {
        const p = {};
        for (const parameter of descriptor.parameters) {
            p[parameter.identifier] = parameter;
            byId.set(parameter.id, parameter.identifier);
        }
        document.getElementById('motion').append(
            add(choice(p.envTimeMode, { labels: ['Sync', 'Free'] })),
            add(menuButton(p.envSyncLength, { columns: 4 })),
            add(menuButton(p.envDirection))
        );
        document.getElementById('amount').append(add(fader(p.envAmount, { label: 'Amount', inline: true, travel: 200 })));
        editor.resize();
        const dials = document.getElementById('dials');
        for (const [id, label] of [['macroVoice', 'Voice'], ['macroPower', 'Power'], ['macroControl', 'Control'], ['macroWidth', 'Width']]) {
            dials.appendChild(card(add(dial(p[id], { label }))));
        }
        const cutoff = document.createElement('div');
        cutoff.style.display = 'flex';
        cutoff.style.alignItems = 'center';
        cutoff.style.gap = '16px';
        cutoff.append(add(dial(p.cutoff, { label: 'Cutoff' })), add(choice(p.filterMode, { labels: ['LP', 'BP'], vertical: true })));
        dials.appendChild(card(cutoff));

        document.getElementById('inline').append(add(fader(p.resonance, { label: 'Resonance', inline: true, travel: 200 })));
        document.getElementById('choices').append(
            add(choice(p.envTimeMode, { labels: ['Sync', 'Free'] })),
            add(choice(p.dsMode, { labels: ['S&H', 'Downsample'], values: [1, 2] }))
        );

        const sheet = document.getElementById('sheet');
        sheet.append(
            add(fader(p.foldAmount, { label: 'Fold' })),
            add(choice(p.foldPosition, { labels: ['Filter', 'Pre', 'Post'], stretch: true })),
            add(fader(p.ottTime, { label: 'OTT time' })),
            add(fader(p.glideTime, { label: 'Glide' }))
        );

        const strip = document.getElementById('strip');
        const levelControl = level(p.outputLevel, { label: 'Level' });
        strip.appendChild(add(levelControl));
        levelControl.element.style.height = '100%';
        let t = 0;
        setInterval(() => {
            t += 0.12;
            levelControl.meter(0.55 + 0.08 * Math.sin(t), 0.52 + 0.08 * Math.sin(t * 1.3));
        }, 60);
        built = true;
    }

    bdd.onReceive((incoming) => {
        if (incoming.descriptor && !built) {
            build(incoming.descriptor);
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
        if (incoming.params) {
            for (const [id, value] of Object.entries(incoming.params)) {
                const identifier = byId.get(Number(id));
                for (const control of controls) {
                    if (control.parameter.identifier === identifier) {
                        control.set(value);
                    }
                }
            }
        }
    });
    bdd.hello();
})();
