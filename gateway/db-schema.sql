CREATE TABLE rules (
    rule_id INTEGER PRIMARY KEY AUTOINCREMENT,
    client_id TEXT,
    parameter_name TEXT,
    min_range REAL,
    max_range REAL,
    trigger TEXT CHECK (trigger IN ('inside_range_trigger', 'outside_range_trigger')),
    callback TEXT
);

CREATE TABLE events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    client_id TEXT,
    type TEXT,
    local_timestamp INTEGER,
    event TEXT,
    data TEXT
);
