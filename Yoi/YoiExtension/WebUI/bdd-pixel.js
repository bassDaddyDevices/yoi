// bdd-pixel.js
// Bass Daddy Devices pixel art: a 5×7 bitmap font and a way to turn any bitmap into crisp SVG.
// Shared by every Bass Daddy Devices synth; nothing here knows about YOI.
//
//   bdd.pixel.text('TIME: 1/8', 2)        an <svg> of the text, 2 CSS pixels per font pixel
//   bdd.pixel.bitmap(rows, 10)            an <svg> of a bitmap given as strings ('#' = filled)
//
// Both use `currentColor`, so CSS `color` sets the ink.

(function () {
    'use strict';

    const HEIGHT = 7;

    // Each glyph is 7 rows; a row's length is the glyph's width. '#' is ink.
    const GLYPHS = {
        'A': ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
        'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
        'C': ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
        'D': ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
        'E': ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
        'F': ['#####', '#....', '#....', '####.', '#....', '#....', '#....'],
        'G': ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.####'],
        'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
        'I': ['###', '.#.', '.#.', '.#.', '.#.', '.#.', '###'],
        'J': ['..###', '...#.', '...#.', '...#.', '...#.', '#..#.', '.##..'],
        'K': ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
        'L': ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
        'M': ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
        'N': ['#...#', '#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#'],
        'O': ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
        'P': ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
        'Q': ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
        'R': ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
        'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
        'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
        'U': ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
        'V': ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
        'W': ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '#.#.#', '.#.#.'],
        'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
        'Y': ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
        'Z': ['#####', '....#', '...#.', '..#..', '.#...', '#....', '#####'],
        '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
        '1': ['.#.', '##.', '.#.', '.#.', '.#.', '.#.', '###'],
        '2': ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
        '3': ['#####', '...#.', '..#..', '...#.', '....#', '#...#', '.###.'],
        '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
        '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
        '6': ['..##.', '.#...', '#....', '####.', '#...#', '#...#', '.###.'],
        '7': ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
        '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
        '9': ['.###.', '#...#', '#...#', '.####', '....#', '...#.', '.##..'],
        ' ': ['..', '..', '..', '..', '..', '..', '..'],
        '.': ['.', '.', '.', '.', '.', '.', '#'],
        ',': ['..', '..', '..', '..', '..', '.#', '#.'],
        ':': ['.', '.', '#', '.', '.', '#', '.'],
        '!': ['#', '#', '#', '#', '#', '.', '#'],
        "'": ['#', '#', '.', '.', '.', '.', '.'],
        '/': ['....#', '...#.', '...#.', '..#..', '.#...', '.#...', '#....'],
        '-': ['....', '....', '....', '####', '....', '....', '....'],
        '+': ['.....', '..#..', '..#..', '#####', '..#..', '..#..', '.....'],
        '=': ['....', '....', '####', '....', '####', '....', '....'],
        '<': ['...#', '..#.', '.#..', '#...', '.#..', '..#.', '...#'],
        '>': ['#...', '.#..', '..#.', '...#', '..#.', '.#..', '#...'],
        '~': ['.....', '.....', '.#...', '#.#.#', '...#.', '.....', '.....'],
        '?': ['.###.', '#...#', '....#', '...#.', '..#..', '.....', '..#..'],
        '%': ['##..#', '##..#', '...#.', '..#..', '.#...', '#..##', '#..##'],
        '&': ['.##..', '#..#.', '#.#..', '.#...', '#.#.#', '#..#.', '.##.#'],
        '(': ['..#', '.#.', '#..', '#..', '#..', '.#.', '..#'],
        ')': ['#..', '.#.', '..#', '..#', '..#', '.#.', '#..'],
        '_': ['.....', '.....', '.....', '.....', '.....', '.....', '#####'],
        '×': ['.....', '.....', '#...#', '.#.#.', '..#..', '.#.#.', '#...#'],
        '▾': ['.....', '.....', '#####', '.###.', '..#..', '.....', '.....'],
        '▸': ['#..', '##.', '###', '##.', '#..', '...', '...'],
        '•': ['...', '...', '###', '###', '###', '...', '...'],
    };

    const SVG = 'http://www.w3.org/2000/svg';

    /** Path data for filled cells, one rectangle per horizontal run. */
    function runs(rows, offsetX, commands) {
        rows.forEach((row, y) => {
            let x = 0;
            while (x < row.length) {
                if (row[x] !== '#') {
                    x++;
                    continue;
                }
                let end = x;
                while (end < row.length && row[end] === '#') {
                    end++;
                }
                commands.push(`M${offsetX + x} ${y}h${end - x}v1h${x - end}z`);
                x = end;
            }
        });
    }

    function glyph(character) {
        return GLYPHS[character] || GLYPHS[character.toUpperCase()] || GLYPHS['?'];
    }

    function makeSvg(width, height, scale, pathData) {
        const svg = document.createElementNS(SVG, 'svg');
        svg.setAttribute('viewBox', `0 0 ${Math.max(width, 1)} ${height}`);
        svg.setAttribute('width', String(Math.max(width, 1) * scale));
        svg.setAttribute('height', String(height * scale));
        svg.setAttribute('shape-rendering', 'crispEdges');
        svg.setAttribute('aria-hidden', 'true');
        svg.classList.add('pixel');
        const path = document.createElementNS(SVG, 'path');
        path.setAttribute('d', pathData);
        path.setAttribute('fill', 'currentColor');
        svg.appendChild(path);
        return svg;
    }

    /** Path data and width (in font pixels) for a line of text. */
    function layout(text) {
        const commands = [];
        let x = 0;
        for (const character of String(text)) {
            const rows = glyph(character);
            runs(rows, x, commands);
            x += rows[0].length + 1;
        }
        return { d: commands.join(''), width: Math.max(0, x - 1) };
    }

    window.bdd = window.bdd || {};
    window.bdd.pixel = {
        HEIGHT,

        /** An <svg> showing `text` in the pixel font. */
        text(text, scale = 2) {
            const { d, width } = layout(text);
            const svg = makeSvg(width, HEIGHT, scale, d);
            svg.dataset.text = String(text);
            return svg;
        },

        /** Changes the text of an <svg> made by `text`, keeping its scale. */
        setText(svg, text, scale = 2) {
            if (svg.dataset.text === String(text)) {
                return;
            }
            const { d, width } = layout(text);
            svg.setAttribute('viewBox', `0 0 ${Math.max(width, 1)} ${HEIGHT}`);
            svg.setAttribute('width', String(Math.max(width, 1) * scale));
            svg.firstChild.setAttribute('d', d);
            svg.dataset.text = String(text);
        },

        /** An <svg> of a bitmap: an array of equal-length strings, '#' for a filled cell. */
        bitmap(rows, scale = 1) {
            const commands = [];
            runs(rows, 0, commands);
            return makeSvg(rows[0].length, rows.length, scale, commands.join(''));
        },
    };
})();
