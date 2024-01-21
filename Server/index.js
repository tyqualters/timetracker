/* Configuration */
const databaseFile = "timetracker.sqlite3"
const port = 5540;

/* Imports */
const path = require('path');
const { readFileSync } = require('fs');
const express = require('express');
const sqlite = require('sqlite3').verbose();
const jwt = require('jose');
const { assert } = require('console');

/* Global variables */
const app = express();
const db = new sqlite.Database(databaseFile);
const packageConfig = JSON.parse(readFileSync(path.join(__dirname, 'package.json')));

// Enable HTTP POST JSON body
app.use(express.urlencoded({ extended: true }));

// Get data from the database
async function dbGet(query, params) {
    assert(typeof query == 'string' && typeof params == 'object' && params instanceof Array, 'dbGet args malformed.');

    return new Promise((res, rej) => {
        db.serialize(() => {
            db.all(query, params, (error, rows) => {
                if (error) rej(error);
                if (rows == null || rows.length == 0) rej("Entries do not exist.");
                res(rows);
            });
        });
    });
}

// Run commands for the database
function dbRun(queries) {
    assert(typeof queries == 'object' && queries instanceof Array, 'dbRun args malformed.');

    db.serialize(() => {
        for (let i = 0; i < queries.length; i += 2) {
            // TODO: Fix so string, string or string, undefined can be used instead of string, array for no params
            assert(typeof queries[i] == 'string' && typeof queries[i + 1] == 'object' && queries[i + 1] instanceof Array, 'dbRun arg %s:%s malformed.', String(queries[i]), JSON.stringify(queries[i + 1]));
            db.run(queries[i], queries[i + 1]);
        }
    });
}

// Set up tables
dbRun([
    "CREATE TABLE IF NOT EXISTS accounts (uid INTEGER PRIMARY KEY AUTOINCREMENT, user TEXT, pass TEXT)", [],
    "CREATE TABLE IF NOT EXISTS tracks (uid INTEGER, track TEXT, seconds INTEGER)", []
]);

app.get('/api', (req, res) => {
    res.setHeader('Content-Type', 'text/html;charset=UTF-8');
    return res.sendFile(path.join(__dirname, 'index.html'));
})

app.get('/api/version', (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    let data = {
        behavior: 'VERSION',
        name: packageConfig.name,
        description: packageConfig.description,
        version: packageConfig.version
    };

    return res.end(JSON.stringify(data));
});

app.post('/api/login', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.username || !req.body.password) return res.end(JSON.stringify({ 'error': 'No login data provided.' }));

    console.log(`Login Request. Received: ${JSON.stringify(req.body)}`);

    // Get user from accounts
    return await dbGet("SELECT * FROM accounts WHERE user=? COLLATE NOCASE AND pass=?", [req.body.username, req.body.password]).then(rows => {
        // User is found
        console.log(`Authenticated user ${rows[0].user} ID ${rows[0].uid}.`);
        res.end(JSON.stringify({ 'behavior': 'AUTHENTICATION', 'username': rows[0].user, 'uid': rows[0].uid }));
    }, error => {
        // User is not found
        res.end(JSON.stringify({ 'error': 'Login invalid.' }));
    });
});

app.post('/api/account', async(req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if(!req.body || !req.body.uid) return res.end(JSON.stringify({'error': 'Did not supply a user id.'}));

    var details = {
        behavior: 'ACCOUNT',
        tracks: []
    };
    
    // Get the account details
    return await dbGet("SELECT * FROM accounts WHERE uid=?", [req.body.uid]).then((rows) => {
        details.userId = rows[0].uid;
        details.username = rows[0].user;
        // Get the track details
        dbGet("SELECT * FROM tracks WHERE uid=?", [rows[0].uid]).then(rows => {
            for(let x in rows) {
                details.tracks.push({'track': rows[x].track, 'seconds': rows[x].seconds});
            }
            res.end(JSON.stringify(details));
        }, err => /* Tracks not found */ res.end(JSON.stringify(details)));
    }, err => {
        // Account not found
        res.end(JSON.stringify({'error': 'User with ID not found.'}));
    });
});

app.post('/api/register', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.username || !req.body.password) return res.end(JSON.stringify({ 'error': 'No registration data provided.' }));

    console.log(`Register Request. Received: ${JSON.stringify(req.body)}`);

    // Get user from accounts
    return await dbGet("SELECT * FROM accounts WHERE user=? COLLATE NOCASE", [req.body.username]).then(async rows => {
        // User exists
        console.log(`Polled usernames: ${req.body.username}`)
        console.table(rows);
        res.end(JSON.stringify({ 'error': 'Username conflict.' }));
    }, async error => {
        // Create a new user
        dbRun([
            "INSERT INTO accounts (user, pass) VALUES (?, ?)", [req.body.username, req.body.password],
        ]);
        res.end(JSON.stringify({ 'message': 'Try logging in now! :)' }));
    });
});

app.post('/api/new', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.uid || !req.body.track) return res.end(JSON.stringify({ 'error': 'Incomplete request.' }));

    console.log(`Track Create Request. Received: ${JSON.stringify(req.body)}`);

    // Get user from accounts
    return await dbGet("SELECT * FROM tracks WHERE uid=? AND track=?", [req.body.uid, req.body.track]).then(async rows => {
        // Track exists
        console.log(`Polled tracks: ${req.body.track}`)
        console.table(rows);
        res.end(JSON.stringify({ 'error': 'Track name conflict.' }));
    }, async error => {
        // Create a new user
        dbRun([
            "INSERT INTO tracks (uid, track, seconds) VALUES (?, ?, ?)", [req.body.uid, req.body.track, 0],
        ]);
        res.end(JSON.stringify({ 'message': 'Added track!' }));
    });
});

app.post('/api/update', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.uid || !req.body.track || !req.body.seconds) return res.end(JSON.stringify({ 'error': 'Incomplete request.' }));

    console.table(req.body);

    // Get track from tracks
    return await dbGet("SELECT * FROM tracks WHERE track=? COLLATE NOCASE AND uid=?", [req.body.track, req.body.uid]).then(rows => {
        // Track found
        var seconds = Number(req.body.seconds) + Number(rows[0].seconds);

        // Update track number
        dbRun(['UPDATE tracks SET seconds=? WHERE track=? COLLATE NOCASE AND uid=?', [seconds, req.body.track, req.body.uid]]);

        res.end(JSON.stringify({ behavior: 'SAVEACK', message: 'Saved!' }));
    }, error => {
        // Track not found
        res.end(JSON.stringify({ 'error': 'Track not found.' }));
    });
});

app.post('/api/delete', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.uid || !req.body.track) return res.end(JSON.stringify({ 'error': 'Incomplete request.' }));

    console.table(req.body);

    // Get track from tracks
    return await dbGet("SELECT * FROM tracks WHERE track=? COLLATE NOCASE AND uid=?", [req.body.track, req.body.uid]).then(rows => {
        // Track found

        // Delete track
        dbRun(['DELETE FROM tracks WHERE track=? COLLATE NOCASE AND uid=?', [req.body.track, req.body.uid]]);

        res.end(JSON.stringify({ message: 'Track deleted.' }));
    }, error => {
        // Track not found
        res.end(JSON.stringify({ 'error': 'Track not found.' }));
    });
});

app.post('/api/count', async (req, res) => {
    res.setHeader('Content-Type', 'application/json');

    // Has all the fields
    if (!req.body || !req.body.uid || !req.body.track) return res.end(JSON.stringify({ 'error': 'Incomplete request.' }));

    // Get track from tracks
    return await dbGet("SELECT * FROM tracks WHERE track=? COLLATE NOCASE AND uid=?", [req.body.track, req.body.uid]).then(rows => {
        // Track found
        res.end(JSON.stringify({ behavior: 'TRACKINFO', 'track': rows[0].track, 'seconds': rows[0].seconds }));
    }, () => /* Track not found */ res.end(JSON.stringify({ 'error': 'Track not found.' })));
});

var server = app.listen(port, () => {
    console.log(`Time Tracker app listening on port ${port}`);
});

process.on('SIGINT', () => {
    server.close();
    db.close();
    console.log('Exited.');
});