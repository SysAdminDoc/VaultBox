# VaultBox Server v0.1.0
# Local Bitwarden-compatible API server for VaultBox offline password manager
# Runs on 127.0.0.1:8787 - never exposed to network

import sys
import os
import json
import uuid
import sqlite3
import hashlib
import hmac
import base64
import threading
import time
import logging
import signal
from datetime import datetime, timezone, timedelta
from pathlib import Path
from contextlib import contextmanager

# --- Auto-bootstrap dependencies ---
def _bootstrap():
    required = {
        "fastapi": "fastapi",
        "uvicorn": "uvicorn",
        "pyjwt": "PyJWT",
        "pystray": "pystray",
        "PIL": "Pillow",
    }
    missing = []
    for mod, pkg in required.items():
        try:
            __import__(mod)
        except ImportError:
            missing.append(pkg)
    if missing:
        import subprocess
        subprocess.check_call([
            sys.executable, "-m", "pip", "install",
            "--break-system-packages", *missing
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

_bootstrap()

from fastapi import FastAPI, Request, Response, HTTPException, Depends
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
import uvicorn
import jwt as pyjwt
import pystray
from PIL import Image, ImageDraw

# =============================================================================
# Configuration
# =============================================================================

HOST = "127.0.0.1"
PORT = 8787
DATA_DIR = Path(os.environ.get("VAULTBOX_DATA", os.path.join(os.environ["LOCALAPPDATA"], "VaultBox")))
DB_PATH = DATA_DIR / "vault.db"
JWT_SECRET = None  # Generated on first run, stored in DB
JWT_ALGORITHM = "HS256"
TOKEN_EXPIRY_HOURS = 24 * 30  # 30 days

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("vaultbox")

# =============================================================================
# Database
# =============================================================================

def init_db():
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS config (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS accounts (
            id TEXT PRIMARY KEY,
            email TEXT UNIQUE NOT NULL,
            name TEXT DEFAULT '',
            master_password_hash TEXT NOT NULL,
            master_password_hint TEXT DEFAULT '',
            security_stamp TEXT NOT NULL,
            key TEXT DEFAULT '',
            public_key TEXT DEFAULT '',
            encrypted_private_key TEXT DEFAULT '',
            kdf INTEGER DEFAULT 1,
            kdf_iterations INTEGER DEFAULT 600000,
            kdf_memory INTEGER,
            kdf_parallelism INTEGER,
            culture TEXT DEFAULT 'en-US',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS ciphers (
            id TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            folder_id TEXT,
            organization_id TEXT,
            type INTEGER NOT NULL,
            data TEXT NOT NULL,
            favorite INTEGER DEFAULT 0,
            reprompt INTEGER DEFAULT 0,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            deleted_at TEXT,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS folders (
            id TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            name TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
        CREATE TABLE IF NOT EXISTS tokens (
            refresh_token TEXT PRIMARY KEY,
            user_id TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY (user_id) REFERENCES accounts(id)
        );
    """)

    # Generate JWT secret if not exists
    global JWT_SECRET
    row = conn.execute("SELECT value FROM config WHERE key='jwt_secret'").fetchone()
    if row:
        JWT_SECRET = row[0]
    else:
        JWT_SECRET = uuid.uuid4().hex + uuid.uuid4().hex
        conn.execute("INSERT INTO config (key, value) VALUES ('jwt_secret', ?)", (JWT_SECRET,))
        conn.commit()

    conn.close()
    log.info(f"Database ready: {DB_PATH}")

@contextmanager
def get_db():
    conn = sqlite3.connect(str(DB_PATH))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys=ON")
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()

def utcnow():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")

# =============================================================================
# JWT Auth
# =============================================================================

def create_access_token(user_id: str, email: str) -> str:
    now = datetime.now(timezone.utc)
    payload = {
        "sub": user_id,
        "email": email,
        "name": email.split("@")[0],
        "premium": True,
        "email_verified": True,
        "iss": "vaultbox|local",
        "iat": int(now.timestamp()),
        "nbf": int(now.timestamp()),
        "exp": int((now + timedelta(hours=TOKEN_EXPIRY_HOURS)).timestamp()),
        "scope": ["api", "offline_access"],
        "amr": ["Application"],
    }
    return pyjwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)

def create_refresh_token(user_id: str) -> str:
    token = str(uuid.uuid4())
    with get_db() as db:
        db.execute("INSERT INTO tokens (refresh_token, user_id, created_at) VALUES (?, ?, ?)",
                   (token, user_id, utcnow()))
    return token

def get_current_user(request: Request) -> dict:
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        raise HTTPException(401, "Unauthorized")
    token = auth[7:]
    try:
        payload = pyjwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        user_id = payload["sub"]
    except pyjwt.ExpiredSignatureError:
        raise HTTPException(401, "Token expired")
    except pyjwt.InvalidTokenError:
        raise HTTPException(401, "Invalid token")

    with get_db() as db:
        row = db.execute("SELECT * FROM accounts WHERE id=?", (user_id,)).fetchone()
    if not row:
        raise HTTPException(401, "User not found")
    return dict(row)

# =============================================================================
# FastAPI App
# =============================================================================

app = FastAPI(title="VaultBox Local Server", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ---------------------------------------------------------------------------
# Health / Info
# ---------------------------------------------------------------------------

@app.get("/")
async def root():
    return {"server": "VaultBox", "version": "0.1.0", "status": "running"}

@app.get("/alive")
@app.get("/api/alive")
async def alive():
    return Response(content="", status_code=200)

# ---------------------------------------------------------------------------
# Pre-login (KDF params)
# ---------------------------------------------------------------------------

@app.post("/api/accounts/prelogin")
async def prelogin(request: Request):
    body = await request.json()
    email = body.get("email", "").lower().strip()

    with get_db() as db:
        row = db.execute("SELECT kdf, kdf_iterations, kdf_memory, kdf_parallelism FROM accounts WHERE email=?",
                         (email,)).fetchone()

    if row:
        return {
            "kdf": row["kdf"],
            "kdfIterations": row["kdf_iterations"],
            "kdfMemory": row["kdf_memory"],
            "kdfParallelism": row["kdf_parallelism"],
        }
    # Default KDF params for new accounts (Argon2id)
    return {
        "kdf": 1,
        "kdfIterations": 3,
        "kdfMemory": 64,
        "kdfParallelism": 4,
    }

# ---------------------------------------------------------------------------
# Registration
# ---------------------------------------------------------------------------

@app.post("/api/accounts/register")
@app.post("/identity/accounts/register")
async def register(request: Request):
    body = await request.json()
    email = body.get("email", "").lower().strip()
    master_hash = body.get("masterPasswordHash", "")
    hint = body.get("masterPasswordHint", "")
    key = body.get("key", "")
    kdf = body.get("kdf", 1)
    kdf_iterations = body.get("kdfIterations", 600000)
    kdf_memory = body.get("kdfMemory")
    kdf_parallelism = body.get("kdfParallelism")

    keys = body.get("keys", {})
    public_key = keys.get("publicKey", "")
    encrypted_private_key = keys.get("encryptedPrivateKey", "")

    if not email or not master_hash:
        raise HTTPException(400, "Email and master password are required")

    user_id = str(uuid.uuid4())
    security_stamp = str(uuid.uuid4())
    now = utcnow()

    with get_db() as db:
        existing = db.execute("SELECT id FROM accounts WHERE email=?", (email,)).fetchone()
        if existing:
            raise HTTPException(400, "Email already registered")

        db.execute("""
            INSERT INTO accounts (id, email, master_password_hash, master_password_hint,
                security_stamp, key, public_key, encrypted_private_key,
                kdf, kdf_iterations, kdf_memory, kdf_parallelism, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (user_id, email, master_hash, hint, security_stamp, key,
              public_key, encrypted_private_key, kdf, kdf_iterations,
              kdf_memory, kdf_parallelism, now, now))

    log.info(f"Account registered: {email}")
    return Response(status_code=200)

# ---------------------------------------------------------------------------
# Login (Token)
# ---------------------------------------------------------------------------

@app.post("/identity/connect/token")
async def login(request: Request):
    content_type = request.headers.get("content-type", "")

    if "application/x-www-form-urlencoded" in content_type:
        form = await request.form()
        data = dict(form)
    else:
        data = await request.json()

    grant_type = data.get("grant_type", "")
    email = data.get("username", "").lower().strip()
    password_hash = data.get("password", "")

    # Handle refresh token grant
    if grant_type == "refresh_token":
        refresh = data.get("refresh_token", "")
        with get_db() as db:
            token_row = db.execute("SELECT user_id FROM tokens WHERE refresh_token=?", (refresh,)).fetchone()
            if not token_row:
                raise HTTPException(400, "Invalid refresh token")
            user = db.execute("SELECT * FROM accounts WHERE id=?", (token_row["user_id"],)).fetchone()
            if not user:
                raise HTTPException(400, "User not found")
            user = dict(user)
    elif grant_type == "password":
        if not email or not password_hash:
            raise HTTPException(400, "Email and password required")

        with get_db() as db:
            row = db.execute("SELECT * FROM accounts WHERE email=?", (email,)).fetchone()

        if not row:
            raise HTTPException(400, "Invalid email or password")

        user = dict(row)
        if not hmac.compare_digest(user["master_password_hash"], password_hash):
            raise HTTPException(400, "Invalid email or password")
    else:
        raise HTTPException(400, f"Unsupported grant_type: {grant_type}")

    access_token = create_access_token(user["id"], user["email"])
    refresh_token = create_refresh_token(user["id"])

    log.info(f"Login successful: {user['email']}")

    return {
        "access_token": access_token,
        "expires_in": TOKEN_EXPIRY_HOURS * 3600,
        "token_type": "Bearer",
        "refresh_token": refresh_token,
        "Key": user["key"],
        "PrivateKey": user["encrypted_private_key"],
        "Kdf": user["kdf"],
        "KdfIterations": user["kdf_iterations"],
        "KdfMemory": user["kdf_memory"],
        "KdfParallelism": user["kdf_parallelism"],
        "ResetMasterPassword": False,
        "ForcePasswordReset": False,
        "MasterPasswordPolicy": None,
        "UserDecryptionOptions": None,
        "scope": "api offline_access",
    }

# ---------------------------------------------------------------------------
# Account Profile
# ---------------------------------------------------------------------------

@app.get("/api/accounts/profile")
async def get_profile(request: Request):
    user = get_current_user(request)
    return _build_profile(user)

@app.put("/api/accounts/profile")
async def update_profile(request: Request):
    user = get_current_user(request)
    body = await request.json()
    name = body.get("name", user["name"])
    culture = body.get("culture", user["culture"])

    with get_db() as db:
        db.execute("UPDATE accounts SET name=?, culture=?, updated_at=? WHERE id=?",
                   (name, culture, utcnow(), user["id"]))
        user = dict(db.execute("SELECT * FROM accounts WHERE id=?", (user["id"],)).fetchone())

    return _build_profile(user)

def _build_profile(user: dict) -> dict:
    return {
        "object": "profile",
        "id": user["id"],
        "name": user["name"] or user["email"].split("@")[0],
        "email": user["email"],
        "emailVerified": True,
        "premium": True,
        "premiumFromOrganization": False,
        "masterPasswordHint": user["master_password_hint"],
        "culture": user["culture"],
        "twoFactorEnabled": False,
        "key": user["key"],
        "privateKey": user["encrypted_private_key"],
        "securityStamp": user["security_stamp"],
        "forcePasswordReset": False,
        "usesKeyConnector": False,
        "avatarColor": None,
        "organizations": [],
        "providers": [],
        "providerOrganizations": [],
    }

# ---------------------------------------------------------------------------
# Account Keys
# ---------------------------------------------------------------------------

@app.post("/api/accounts/keys")
async def set_keys(request: Request):
    user = get_current_user(request)
    body = await request.json()
    public_key = body.get("publicKey", "")
    encrypted_private_key = body.get("encryptedPrivateKey", "")

    with get_db() as db:
        db.execute("UPDATE accounts SET public_key=?, encrypted_private_key=?, updated_at=? WHERE id=?",
                   (public_key, encrypted_private_key, utcnow(), user["id"]))

    return Response(status_code=200)

@app.post("/api/accounts/key")
async def set_user_key(request: Request):
    user = get_current_user(request)
    body = await request.json()
    key = body.get("key", "")

    with get_db() as db:
        db.execute("UPDATE accounts SET key=?, updated_at=? WHERE id=?",
                   (key, utcnow(), user["id"]))

    return Response(status_code=200)

@app.post("/api/accounts/password")
async def change_password(request: Request):
    user = get_current_user(request)
    body = await request.json()
    new_hash = body.get("newMasterPasswordHash", "")
    new_key = body.get("key", "")
    master_password_hash = body.get("masterPasswordHash", "")

    if not hmac.compare_digest(user["master_password_hash"], master_password_hash):
        raise HTTPException(400, "Invalid current master password")

    security_stamp = str(uuid.uuid4())
    with get_db() as db:
        db.execute("""
            UPDATE accounts SET master_password_hash=?, key=?, security_stamp=?, updated_at=?
            WHERE id=?
        """, (new_hash, new_key, security_stamp, utcnow(), user["id"]))

    return Response(status_code=200)

@app.post("/api/accounts/kdf")
async def change_kdf(request: Request):
    user = get_current_user(request)
    body = await request.json()

    with get_db() as db:
        db.execute("""
            UPDATE accounts SET kdf=?, kdf_iterations=?, kdf_memory=?, kdf_parallelism=?,
                key=?, master_password_hash=?, updated_at=?
            WHERE id=?
        """, (
            body.get("kdf", user["kdf"]),
            body.get("kdfIterations", user["kdf_iterations"]),
            body.get("kdfMemory", user["kdf_memory"]),
            body.get("kdfParallelism", user["kdf_parallelism"]),
            body.get("key", user["key"]),
            body.get("newMasterPasswordHash", user["master_password_hash"]),
            utcnow(), user["id"],
        ))

    return Response(status_code=200)

@app.post("/api/accounts/verify-password")
async def verify_password(request: Request):
    user = get_current_user(request)
    body = await request.json()
    master_hash = body.get("masterPasswordHash", "")
    if hmac.compare_digest(user["master_password_hash"], master_hash):
        return Response(status_code=200)
    raise HTTPException(400, "Invalid password")

@app.get("/api/accounts/revision-date")
async def revision_date(request: Request):
    user = get_current_user(request)
    # Return latest update time across account and ciphers
    with get_db() as db:
        row = db.execute("""
            SELECT MAX(ts) as latest FROM (
                SELECT updated_at as ts FROM accounts WHERE id=?
                UNION ALL
                SELECT MAX(updated_at) as ts FROM ciphers WHERE user_id=?
                UNION ALL
                SELECT MAX(updated_at) as ts FROM folders WHERE user_id=?
            )
        """, (user["id"], user["id"], user["id"])).fetchone()
    ts = row["latest"] if row and row["latest"] else user["updated_at"]
    # Return as milliseconds timestamp
    try:
        dt = datetime.strptime(ts, "%Y-%m-%dT%H:%M:%S.%fZ").replace(tzinfo=timezone.utc)
        return Response(content=str(int(dt.timestamp() * 1000)), media_type="text/plain")
    except Exception:
        return Response(content=str(int(time.time() * 1000)), media_type="text/plain")

# ---------------------------------------------------------------------------
# Sync
# ---------------------------------------------------------------------------

@app.get("/api/sync")
async def sync(request: Request):
    user = get_current_user(request)

    with get_db() as db:
        cipher_rows = db.execute(
            "SELECT * FROM ciphers WHERE user_id=? ORDER BY created_at", (user["id"],)
        ).fetchall()
        folder_rows = db.execute(
            "SELECT * FROM folders WHERE user_id=? ORDER BY created_at", (user["id"],)
        ).fetchall()

    ciphers = [_build_cipher(dict(r)) for r in cipher_rows]
    folders = [_build_folder(dict(r)) for r in folder_rows]
    profile = _build_profile(user)

    return {
        "object": "sync",
        "profile": profile,
        "folders": folders,
        "collections": [],
        "ciphers": ciphers,
        "domains": {
            "object": "domains",
            "equivalentDomains": [],
            "globalEquivalentDomains": [],
        },
        "policies": [],
        "sends": [],
    }

# ---------------------------------------------------------------------------
# Ciphers (CRUD)
# ---------------------------------------------------------------------------

@app.post("/api/ciphers")
async def create_cipher(request: Request):
    user = get_current_user(request)
    body = await request.json()
    return _upsert_cipher(user["id"], None, body)

@app.post("/api/ciphers/create")
async def create_cipher_alt(request: Request):
    user = get_current_user(request)
    body = await request.json()
    # Some clients send { cipher: {...}, collectionIds: [...] }
    cipher_data = body.get("cipher", body)
    return _upsert_cipher(user["id"], None, cipher_data)

@app.put("/api/ciphers/{cipher_id}")
@app.post("/api/ciphers/{cipher_id}")
async def update_cipher(cipher_id: str, request: Request):
    user = get_current_user(request)
    body = await request.json()
    return _upsert_cipher(user["id"], cipher_id, body)

@app.delete("/api/ciphers/{cipher_id}")
async def hard_delete_cipher(cipher_id: str, request: Request):
    user = get_current_user(request)
    with get_db() as db:
        db.execute("DELETE FROM ciphers WHERE id=? AND user_id=?", (cipher_id, user["id"]))
    return Response(status_code=200)

@app.put("/api/ciphers/{cipher_id}/delete")
@app.post("/api/ciphers/{cipher_id}/delete")
async def soft_delete_cipher(cipher_id: str, request: Request):
    user = get_current_user(request)
    with get_db() as db:
        db.execute("UPDATE ciphers SET deleted_at=?, updated_at=? WHERE id=? AND user_id=?",
                   (utcnow(), utcnow(), cipher_id, user["id"]))
    return Response(status_code=200)

@app.put("/api/ciphers/{cipher_id}/restore")
async def restore_cipher(cipher_id: str, request: Request):
    user = get_current_user(request)
    with get_db() as db:
        db.execute("UPDATE ciphers SET deleted_at=NULL, updated_at=? WHERE id=? AND user_id=?",
                   (utcnow(), cipher_id, user["id"]))
        row = db.execute("SELECT * FROM ciphers WHERE id=? AND user_id=?",
                         (cipher_id, user["id"])).fetchone()
    if not row:
        raise HTTPException(404, "Cipher not found")
    return _build_cipher(dict(row))

@app.put("/api/ciphers/{cipher_id}/favorite")
async def toggle_favorite(cipher_id: str, request: Request):
    user = get_current_user(request)
    body = await request.json()
    fav = 1 if body.get("favorite", False) else 0
    with get_db() as db:
        db.execute("UPDATE ciphers SET favorite=?, updated_at=? WHERE id=? AND user_id=?",
                   (fav, utcnow(), cipher_id, user["id"]))
    return Response(status_code=200)

@app.post("/api/ciphers/purge")
async def purge_ciphers(request: Request):
    user = get_current_user(request)
    body = await request.json()
    master_hash = body.get("masterPasswordHash", "")
    if not hmac.compare_digest(user["master_password_hash"], master_hash):
        raise HTTPException(400, "Invalid password")
    with get_db() as db:
        db.execute("DELETE FROM ciphers WHERE user_id=?", (user["id"],))
    return Response(status_code=200)

@app.post("/api/ciphers/import")
async def import_ciphers(request: Request):
    user = get_current_user(request)
    body = await request.json()
    now = utcnow()

    folders_data = body.get("folders", [])
    ciphers_data = body.get("ciphers", [])
    folder_relationships = body.get("folderRelationships", [])

    # Create folders first, mapping index -> id
    folder_id_map = {}
    with get_db() as db:
        for i, f in enumerate(folders_data):
            fid = str(uuid.uuid4())
            folder_id_map[i] = fid
            db.execute("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                       (fid, user["id"], f.get("name", ""), now, now))

        # Create ciphers with folder assignments
        cipher_folder_map = {r.get("key", r.get("Key", -1)): r.get("value", r.get("Value", -1))
                             for r in folder_relationships}

        for i, c in enumerate(ciphers_data):
            cid = str(uuid.uuid4())
            folder_id = None
            if i in cipher_folder_map:
                folder_idx = cipher_folder_map[i]
                folder_id = folder_id_map.get(folder_idx)

            cipher_type = c.get("type", 1)
            data_blob = json.dumps(_extract_cipher_data(c))
            fav = 1 if c.get("favorite", False) else 0
            reprompt = c.get("reprompt", 0)

            db.execute("""
                INSERT INTO ciphers (id, user_id, folder_id, organization_id, type, data,
                    favorite, reprompt, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (cid, user["id"], folder_id, None, cipher_type, data_blob, fav, reprompt, now, now))

    log.info(f"Imported {len(ciphers_data)} ciphers, {len(folders_data)} folders")
    return Response(status_code=200)

def _extract_cipher_data(body: dict) -> dict:
    """Extract encrypted cipher fields into a storable JSON blob."""
    data = {}
    for field in ["name", "notes", "login", "card", "identity", "secureNote",
                  "fields", "passwordHistory", "attachments", "reprompt"]:
        if field in body:
            data[field] = body[field]
    return data

def _upsert_cipher(user_id: str, cipher_id: str | None, body: dict) -> dict:
    now = utcnow()
    cipher_type = body.get("type", 1)
    folder_id = body.get("folderId")
    org_id = body.get("organizationId")
    fav = 1 if body.get("favorite", False) else 0
    reprompt = body.get("reprompt", 0)
    data_blob = json.dumps(_extract_cipher_data(body))

    with get_db() as db:
        if cipher_id:
            # Update
            db.execute("""
                UPDATE ciphers SET folder_id=?, organization_id=?, type=?, data=?,
                    favorite=?, reprompt=?, updated_at=?
                WHERE id=? AND user_id=?
            """, (folder_id, org_id, cipher_type, data_blob, fav, reprompt, now,
                  cipher_id, user_id))
            row = db.execute("SELECT * FROM ciphers WHERE id=?", (cipher_id,)).fetchone()
        else:
            # Create
            cipher_id = str(uuid.uuid4())
            db.execute("""
                INSERT INTO ciphers (id, user_id, folder_id, organization_id, type, data,
                    favorite, reprompt, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (cipher_id, user_id, folder_id, org_id, cipher_type, data_blob,
                  fav, reprompt, now, now))
            row = db.execute("SELECT * FROM ciphers WHERE id=?", (cipher_id,)).fetchone()

    if not row:
        raise HTTPException(404, "Cipher not found")
    return _build_cipher(dict(row))

def _build_cipher(row: dict) -> dict:
    data = json.loads(row["data"]) if row["data"] else {}
    cipher = {
        "object": "cipher",
        "id": row["id"],
        "organizationId": row["organization_id"],
        "folderId": row["folder_id"],
        "type": row["type"],
        "name": data.get("name"),
        "notes": data.get("notes"),
        "login": data.get("login"),
        "card": data.get("card"),
        "identity": data.get("identity"),
        "secureNote": data.get("secureNote"),
        "fields": data.get("fields"),
        "passwordHistory": data.get("passwordHistory"),
        "attachments": data.get("attachments"),
        "favorite": bool(row["favorite"]),
        "reprompt": row["reprompt"],
        "organizationUseTotp": False,
        "revisionDate": row["updated_at"],
        "creationDate": row["created_at"],
        "deletedDate": row["deleted_at"],
        "collectionIds": [],
        "edit": True,
        "viewPassword": True,
    }
    return cipher

# ---------------------------------------------------------------------------
# Folders (CRUD)
# ---------------------------------------------------------------------------

@app.post("/api/folders")
async def create_folder(request: Request):
    user = get_current_user(request)
    body = await request.json()
    name = body.get("name", "")
    folder_id = str(uuid.uuid4())
    now = utcnow()

    with get_db() as db:
        db.execute("INSERT INTO folders (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)",
                   (folder_id, user["id"], name, now, now))
        row = db.execute("SELECT * FROM folders WHERE id=?", (folder_id,)).fetchone()

    return _build_folder(dict(row))

@app.put("/api/folders/{folder_id}")
async def update_folder(folder_id: str, request: Request):
    user = get_current_user(request)
    body = await request.json()
    name = body.get("name", "")

    with get_db() as db:
        db.execute("UPDATE folders SET name=?, updated_at=? WHERE id=? AND user_id=?",
                   (name, utcnow(), folder_id, user["id"]))
        row = db.execute("SELECT * FROM folders WHERE id=? AND user_id=?",
                         (folder_id, user["id"])).fetchone()

    if not row:
        raise HTTPException(404, "Folder not found")
    return _build_folder(dict(row))

@app.delete("/api/folders/{folder_id}")
async def delete_folder(folder_id: str, request: Request):
    user = get_current_user(request)
    with get_db() as db:
        # Unlink ciphers from this folder
        db.execute("UPDATE ciphers SET folder_id=NULL, updated_at=? WHERE folder_id=? AND user_id=?",
                   (utcnow(), folder_id, user["id"]))
        db.execute("DELETE FROM folders WHERE id=? AND user_id=?", (folder_id, user["id"]))
    return Response(status_code=200)

def _build_folder(row: dict) -> dict:
    return {
        "object": "folder",
        "id": row["id"],
        "name": row["name"],
        "revisionDate": row["updated_at"],
    }

# ---------------------------------------------------------------------------
# Organizations / Collections / Sends (stubs)
# ---------------------------------------------------------------------------

@app.get("/api/organizations")
async def list_organizations(request: Request):
    get_current_user(request)
    return {"object": "list", "data": [], "continuationToken": None}

@app.get("/api/collections")
async def list_collections(request: Request):
    get_current_user(request)
    return {"object": "list", "data": [], "continuationToken": None}

@app.get("/api/sends")
async def list_sends(request: Request):
    get_current_user(request)
    return {"object": "list", "data": [], "continuationToken": None}

@app.get("/api/organizations/{org_id}/auto-enroll-status")
async def org_auto_enroll(org_id: str, request: Request):
    return {"resetPasswordEnabled": False}

# ---------------------------------------------------------------------------
# Settings / Config stubs
# ---------------------------------------------------------------------------

@app.get("/api/settings/domains")
async def get_domains(request: Request):
    get_current_user(request)
    return {
        "object": "domains",
        "equivalentDomains": [],
        "globalEquivalentDomains": [],
    }

@app.put("/api/settings/domains")
async def set_domains(request: Request):
    get_current_user(request)
    return Response(status_code=200)

@app.get("/api/devices/identifier/{identifier}/type/{device_type}")
async def get_device_by_identifier(identifier: str, device_type: str, request: Request):
    raise HTTPException(404, "Device not found")

@app.get("/api/config")
async def get_config(request: Request):
    return {
        "object": "config",
        "version": "2024.1.0",
        "gitHash": "vaultbox",
        "server": {"name": "VaultBox", "url": f"http://{HOST}:{PORT}"},
        "environment": {
            "cloudRegion": None,
            "vault": f"http://{HOST}:{PORT}",
            "api": f"http://{HOST}:{PORT}",
            "identity": f"http://{HOST}:{PORT}",
            "notifications": None,
            "sso": None,
        },
        "featureStates": {},
    }

# ---------------------------------------------------------------------------
# Events / HIBP stubs
# ---------------------------------------------------------------------------

@app.post("/api/ciphers/{cipher_id}/collections")
async def set_cipher_collections(cipher_id: str, request: Request):
    return Response(status_code=200)

@app.post("/api/accounts/api-key")
async def get_api_key(request: Request):
    user = get_current_user(request)
    return {"apiKey": uuid.uuid4().hex}

@app.post("/api/accounts/rotate-api-key")
async def rotate_api_key(request: Request):
    user = get_current_user(request)
    return {"apiKey": uuid.uuid4().hex}

@app.post("/api/accounts/delete")
async def delete_account(request: Request):
    user = get_current_user(request)
    body = await request.json()
    master_hash = body.get("masterPasswordHash", "")
    if not hmac.compare_digest(user["master_password_hash"], master_hash):
        raise HTTPException(400, "Invalid password")
    with get_db() as db:
        db.execute("DELETE FROM ciphers WHERE user_id=?", (user["id"],))
        db.execute("DELETE FROM folders WHERE user_id=?", (user["id"],))
        db.execute("DELETE FROM tokens WHERE user_id=?", (user["id"],))
        db.execute("DELETE FROM accounts WHERE id=?", (user["id"],))
    return Response(status_code=200)

@app.post("/collect")
@app.post("/api/events/collect")
async def collect_events(request: Request):
    return Response(status_code=200)

# ---------------------------------------------------------------------------
# Catch-all for unimplemented endpoints
# ---------------------------------------------------------------------------

@app.api_route("/{path:path}", methods=["GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"])
async def catch_all(path: str, request: Request):
    log.debug(f"Unhandled: {request.method} /{path}")
    # Return empty success for most unhandled routes
    if request.method == "GET":
        return {"object": "list", "data": [], "continuationToken": None}
    return Response(status_code=200)

# =============================================================================
# System Tray
# =============================================================================

def create_tray_icon() -> Image.Image:
    """Generate VaultBox tray icon programmatically."""
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Dark background circle
    draw.ellipse([2, 2, size-2, size-2], fill=(13, 14, 26, 255))

    # Blue vault body
    draw.rounded_rectangle([14, 28, 50, 54], radius=4, fill=(59, 130, 246, 230))

    # Lock shackle
    draw.arc([22, 14, 42, 32], 0, 360, fill=(147, 197, 253, 255), width=4)
    draw.rectangle([22, 24, 26, 30], fill=(13, 14, 26, 255))
    draw.rectangle([38, 24, 42, 30], fill=(13, 14, 26, 255))

    # Keyhole
    draw.ellipse([28, 36, 36, 44], fill=(13, 21, 38, 255))
    draw.rectangle([30, 42, 34, 48], fill=(13, 21, 38, 255))

    # Green status dot
    draw.ellipse([44, 4, 56, 16], fill=(34, 197, 94, 230))

    return img

def start_tray(shutdown_event: threading.Event):
    icon_image = create_tray_icon()

    def on_quit(icon, item):
        log.info("Quit requested from tray")
        shutdown_event.set()
        icon.stop()

    def on_open_vault(icon, item):
        vault_path = str(DATA_DIR)
        os.startfile(vault_path)

    menu = pystray.Menu(
        pystray.MenuItem(f"VaultBox Server v0.1.0", None, enabled=False),
        pystray.MenuItem(f"http://{HOST}:{PORT}", None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Open Data Folder", on_open_vault),
        pystray.MenuItem("Quit", on_quit),
    )

    icon = pystray.Icon("VaultBox", icon_image, "VaultBox Server", menu)
    icon.run()

# =============================================================================
# Main
# =============================================================================

def main():
    init_db()

    shutdown_event = threading.Event()

    # Start system tray in background thread
    tray_thread = threading.Thread(target=start_tray, args=(shutdown_event,), daemon=True)
    tray_thread.start()

    log.info(f"VaultBox Server starting on http://{HOST}:{PORT}")

    # Run uvicorn with graceful shutdown
    config = uvicorn.Config(app, host=HOST, port=PORT, log_level="warning", access_log=False)
    server = uvicorn.Server(config)

    server_thread = threading.Thread(target=server.run, daemon=True)
    server_thread.start()

    # Wait for shutdown signal (from tray quit or Ctrl+C)
    try:
        while not shutdown_event.is_set():
            shutdown_event.wait(timeout=1)
    except KeyboardInterrupt:
        pass
    finally:
        log.info("Shutting down...")
        server.should_exit = True
        server_thread.join(timeout=5)

if __name__ == "__main__":
    main()
