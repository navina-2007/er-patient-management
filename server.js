/**
 * server.js
 * =========
 * Node.js web server for the ER Patient Management system.
 *
 * - No npm packages required (built-in modules only).
 * - Spawns er_web.exe once; keeps it alive (restarts on crash).
 * - GET  /         → serves index.html
 * - POST /api      → validates JSON body, sends command to C process,
 *                    returns the single-line JSON response.
 * - Listens on port 3000.
 *
 * Usage:
 *   node server.js
 */

'use strict';

const http     = require('http');
const fs       = require('fs');
const path     = require('path');
const { spawn } = require('child_process');
const readline = require('readline');

/* -----------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */
const PORT       = 3000;
const C_EXE      = path.join(__dirname, 'er_web.exe');
const INDEX_HTML = path.join(__dirname, 'index.html');

/* -----------------------------------------------------------------------
 * Validation helpers
 * ----------------------------------------------------------------------- */
function isPositiveInt(v) {
    return Number.isInteger(v) && v > 0;
}
function isValidName(v) {
    return typeof v === 'string' && /^[A-Za-z ]{1,40}$/.test(v);
}
function isValidAge(v) {
    return Number.isInteger(v) && v >= 0 && v <= 120;
}
function isValidSeverity(v) {
    return Number.isInteger(v) && v >= 1 && v <= 5;
}
function isValidMinutes(v) {
    return Number.isInteger(v) && v >= 1 && v <= 1000;
}
function isValidSimCount(v) {
    return Number.isInteger(v) && v >= 1 && v <= 50;
}
function isValidSeed(v) {
    return Number.isInteger(v) && v >= 0;
}

/* -----------------------------------------------------------------------
 * C-process manager
 * ----------------------------------------------------------------------- */
let cProcess   = null;
let rl         = null;           // readline interface for stdout

/** Queue of pending requests: {resolve, reject, timer} */
const pending  = [];

/** Timeout (ms) waiting for a response line from the C process */
const TIMEOUT_MS = 5000;

function startCProcess() {
    console.log('[er] Spawning', C_EXE);

    cProcess = spawn(C_EXE, [], {
        stdio: ['pipe', 'pipe', 'pipe'],
        windowsHide: true,
    });

    cProcess.stderr.on('data', (d) => {
        // C program should not write to stderr; log for debugging
        process.stderr.write('[er-stderr] ' + d);
    });

    rl = readline.createInterface({ input: cProcess.stdout });

    rl.on('line', (line) => {
        const entry = pending.shift();
        if (!entry) {
            console.warn('[er] Unexpected line from C process (no pending request):', line);
            return;
        }
        clearTimeout(entry.timer);
        try {
            const obj = JSON.parse(line);
            entry.resolve(obj);
        } catch (e) {
            entry.reject(new Error('C process returned invalid JSON: ' + line));
        }
    });

    cProcess.on('exit', (code, signal) => {
        console.warn(`[er] C process exited (code=${code} signal=${signal}). Restarting in 500ms…`);
        // Reject all pending requests
        while (pending.length > 0) {
            const entry = pending.shift();
            clearTimeout(entry.timer);
            entry.reject(new Error('C process exited unexpectedly'));
        }
        cProcess = null;
        rl       = null;
        setTimeout(startCProcess, 500);
    });

    cProcess.on('error', (err) => {
        console.error('[er] Failed to spawn C process:', err.message);
    });
}

/**
 * Send one command line to the C process and return a Promise that resolves
 * with the parsed JSON response object.
 */
function sendCommand(commandLine) {
    return new Promise((resolve, reject) => {
        if (!cProcess || cProcess.exitCode !== null) {
            return reject(new Error('C process not running'));
        }

        const timer = setTimeout(() => {
            // Remove this entry from pending
            const idx = pending.findIndex(e => e.timer === timer);
            if (idx !== -1) pending.splice(idx, 1);
            reject(new Error('Timeout waiting for C process response'));
        }, TIMEOUT_MS);

        pending.push({ resolve, reject, timer });

        try {
            cProcess.stdin.write(commandLine + '\n');
        } catch (e) {
            clearTimeout(timer);
            pending.pop();
            reject(e);
        }
    });
}

/* -----------------------------------------------------------------------
 * Request routing
 * ----------------------------------------------------------------------- */

/**
 * Validate and translate a POST /api body into a C command string.
 * Returns { ok: false, error: "..." } on validation failure.
 * Returns { ok: true, command: "..." } on success.
 */
function buildCommand(body) {
    const action = body.action;
    if (typeof action !== 'string') {
        return { ok: false, error: 'Missing or invalid "action" field' };
    }

    switch (action.toUpperCase()) {

        case 'ADMIT': {
            const { id, name, age, severity } = body;
            if (!isPositiveInt(id))      return { ok: false, error: 'id must be a positive integer' };
            if (!isValidName(name))      return { ok: false, error: 'name must be 1-40 letters/spaces only' };
            if (!isValidAge(age))        return { ok: false, error: 'age must be 0-120' };
            if (!isValidSeverity(severity)) return { ok: false, error: 'severity must be 1-5' };
            // name is safe (validated to letters/spaces only)
            return { ok: true, command: `ADMIT ${id}|${name}|${age}|${severity}` };
        }

        case 'TREAT':
            return { ok: true, command: 'TREAT' };

        case 'PEEK':
            return { ok: true, command: 'PEEK' };

        case 'RETRIAGE': {
            const { id, newSeverity } = body;
            if (!isPositiveInt(id))           return { ok: false, error: 'id must be a positive integer' };
            if (!isValidSeverity(newSeverity)) return { ok: false, error: 'newSeverity must be 1-5' };
            return { ok: true, command: `RETRIAGE ${id}|${newSeverity}` };
        }

        case 'DISCHARGE': {
            const { id } = body;
            if (!isPositiveInt(id)) return { ok: false, error: 'id must be a positive integer' };
            return { ok: true, command: `DISCHARGE ${id}` };
        }

        case 'SEARCH': {
            const { id } = body;
            if (!isPositiveInt(id)) return { ok: false, error: 'id must be a positive integer' };
            return { ok: true, command: `SEARCH ${id}` };
        }

        case 'UNDO':
            return { ok: true, command: 'UNDO' };

        case 'ADVANCE': {
            const { minutes } = body;
            if (!isValidMinutes(minutes)) return { ok: false, error: 'minutes must be 1-1000' };
            return { ok: true, command: `ADVANCE ${minutes}` };
        }

        case 'STATS':
            return { ok: true, command: 'STATS' };

        case 'HISTORY':
            return { ok: true, command: 'HISTORY' };

        case 'SIMULATE': {
            const { count, seed } = body;
            if (!isValidSimCount(count)) return { ok: false, error: 'count must be 1-50' };
            const s = (seed !== undefined) ? seed : 42;
            if (!isValidSeed(s))         return { ok: false, error: 'seed must be a non-negative integer' };
            return { ok: true, command: `SIMULATE ${count}|${s}` };
        }

        case 'RESET':
            return { ok: true, command: 'RESET' };

        case 'STATE':
            return { ok: true, command: 'STATE' };

        default:
            return { ok: false, error: `Unknown action: ${action}` };
    }
}

/* -----------------------------------------------------------------------
 * HTTP server
 * ----------------------------------------------------------------------- */
const server = http.createServer(async (req, res) => {

    /* --- CORS preflight (handy for local dev) --- */
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
    }

    /* --- GET / → serve index.html --- */
    if (req.method === 'GET' && req.url === '/') {
        fs.readFile(INDEX_HTML, (err, data) => {
            if (err) {
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('index.html not found');
            } else {
                res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
                res.end(data);
            }
        });
        return;
    }

    /* --- POST /api → command --- */
    if (req.method === 'POST' && req.url === '/api') {
        let rawBody = '';
        req.on('data', chunk => { rawBody += chunk; });
        req.on('end', async () => {
            let body;
            try {
                body = JSON.parse(rawBody);
            } catch {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ ok: 0, msg: 'Invalid JSON body' }));
                return;
            }

            // Validate and build command
            const result = buildCommand(body);
            if (!result.ok) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ ok: 0, msg: result.error }));
                return;
            }

            // Send to C process
            try {
                const cResponse = await sendCommand(result.command);
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(cResponse));
            } catch (e) {
                res.writeHead(500, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ ok: 0, msg: 'C process error: ' + e.message }));
            }
        });
        return;
    }

    /* --- Fallback --- */
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found');
});

/* -----------------------------------------------------------------------
 * Boot
 * ----------------------------------------------------------------------- */
startCProcess();
server.listen(PORT, '127.0.0.1', () => {
    console.log(`[server] Listening on http://127.0.0.1:${PORT}/`);
});
