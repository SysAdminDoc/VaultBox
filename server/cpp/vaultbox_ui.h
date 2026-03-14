// VaultBox Desktop - Embedded Web UI (Bitwarden-styled SPA)
// Served at http://127.0.0.1:8787/ via the HTTP server
#pragma once

inline const char* VAULTBOX_SPA_HTML =
// --- Chunk 1: Head + CSS ---
R"VBHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>VaultBox</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#1a1a2e;--bg-alt:#16162a;--bg-card:#2c2c3e;--bg-input:#363649;
  --bg-hover:#3a3a4e;--bg-active:#175DDC22;--primary:#175DDC;--primary-h:#1252c4;
  --primary-light:#6d9eeb;--text:#eff1f5;--text-sec:#8d8d9b;--text-muted:#6c6c7e;
  --border:#404058;--danger:#cf3d3d;--danger-h:#b52e2e;--success:#51a95b;
  --radius:6px;--radius-lg:10px;
  --font:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,sans-serif;
  --mono:'Cascadia Mono','Fira Code','Consolas',monospace;
  --shadow:0 4px 24px rgba(0,0,0,.35);
  --transition:150ms ease;
}
html,body{height:100%;background:var(--bg);color:var(--text);font:14px/1.5 var(--font);-webkit-font-smoothing:antialiased}
body{overflow:hidden}
::selection{background:var(--primary);color:#fff}
::-webkit-scrollbar{width:6px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:#404058;border-radius:3px}
::-webkit-scrollbar-thumb:hover{background:#505068}
input,select,textarea,button{font:inherit;color:inherit}
a{color:var(--primary-light);text-decoration:none}

/* Layout */
#app{display:flex;height:100vh;flex-direction:column}
.topbar{display:flex;align-items:center;height:48px;background:var(--bg-alt);border-bottom:1px solid var(--border);padding:0 12px;flex-shrink:0;-webkit-app-region:drag}
.topbar *{-webkit-app-region:no-drag}
.topbar-logo{display:flex;align-items:center;gap:8px;font-weight:700;font-size:15px;color:var(--text);margin-right:auto}
.topbar-logo svg{width:22px;height:22px}
.topbar-actions{display:flex;gap:4px}

.main{display:flex;flex:1;overflow:hidden}
.sidebar{width:220px;background:var(--bg-alt);border-right:1px solid var(--border);display:flex;flex-direction:column;flex-shrink:0;overflow-y:auto}
.content{flex:1;display:flex;flex-direction:column;overflow:hidden}
.detail-panel{width:340px;background:var(--bg-card);border-left:1px solid var(--border);overflow-y:auto;flex-shrink:0;display:none}
.detail-panel.open{display:block}

/* Buttons */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:8px 16px;border:none;border-radius:var(--radius);cursor:pointer;font-weight:500;font-size:13px;transition:all var(--transition);white-space:nowrap}
.btn-primary{background:var(--primary);color:#fff}
.btn-primary:hover{background:var(--primary-h)}
.btn-secondary{background:var(--bg-input);color:var(--text)}
.btn-secondary:hover{background:var(--bg-hover)}
.btn-danger{background:var(--danger);color:#fff}
.btn-danger:hover{background:var(--danger-h)}
.btn-ghost{background:transparent;color:var(--text-sec);padding:6px 8px}
.btn-ghost:hover{background:var(--bg-hover);color:var(--text)}
.btn-icon{width:32px;height:32px;padding:0;border-radius:var(--radius);background:transparent;border:none;color:var(--text-sec);cursor:pointer;display:flex;align-items:center;justify-content:center;transition:all var(--transition)}
.btn-icon:hover{background:var(--bg-hover);color:var(--text)}
.btn-sm{padding:5px 10px;font-size:12px}
.btn:disabled{opacity:.4;cursor:not-allowed}

/* Inputs */
.form-group{margin-bottom:16px}
.form-group label{display:block;font-size:12px;font-weight:500;color:var(--text-sec);margin-bottom:4px;text-transform:uppercase;letter-spacing:.5px}
.form-input{width:100%;padding:9px 12px;background:var(--bg-input);border:1px solid var(--border);border-radius:var(--radius);color:var(--text);outline:none;transition:border-color var(--transition)}
.form-input:focus{border-color:var(--primary)}
.form-input::placeholder{color:var(--text-muted)}
select.form-input{appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' fill='%238d8d9b' viewBox='0 0 16 16'%3E%3Cpath d='M8 11L3 6h10z'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 10px center;padding-right:30px}
textarea.form-input{resize:vertical;min-height:80px}
.input-group{position:relative;display:flex;gap:4px}
.input-group .form-input{flex:1}

/* Sidebar nav */
.nav-section{padding:8px 0}
.nav-section-title{padding:4px 16px;font-size:11px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:.8px}
.nav-item{display:flex;align-items:center;gap:10px;padding:8px 16px;cursor:pointer;color:var(--text-sec);transition:all var(--transition);font-size:13px;border-left:3px solid transparent}
.nav-item:hover{background:var(--bg-hover);color:var(--text)}
.nav-item.active{background:var(--bg-active);color:var(--primary-light);border-left-color:var(--primary)}
.nav-item svg{width:16px;height:16px;flex-shrink:0;opacity:.7}
.nav-item.active svg{opacity:1}
.nav-item .count{margin-left:auto;font-size:11px;background:var(--bg-input);padding:1px 6px;border-radius:10px;color:var(--text-muted)}
.nav-folder{padding-left:32px}
.folder-actions{margin-left:auto;display:flex;gap:2px;opacity:0;transition:opacity var(--transition)}
.nav-item:hover .folder-actions{opacity:1}

/* Item list */
.list-header{display:flex;align-items:center;padding:12px 16px;gap:8px;border-bottom:1px solid var(--border);flex-shrink:0}
.search-box{flex:1;position:relative}
.search-box input{width:100%;padding:7px 12px 7px 32px;background:var(--bg-input);border:1px solid var(--border);border-radius:var(--radius);color:var(--text);outline:none;font-size:13px}
.search-box input:focus{border-color:var(--primary)}
.search-box svg{position:absolute;left:10px;top:50%;transform:translateY(-50%);width:14px;height:14px;color:var(--text-muted)}
.item-list{flex:1;overflow-y:auto;padding:4px 0}
.item-row{display:flex;align-items:center;padding:10px 16px;gap:12px;cursor:pointer;border-left:3px solid transparent;transition:all var(--transition)}
.item-row:hover{background:var(--bg-hover)}
.item-row.selected{background:var(--bg-active);border-left-color:var(--primary)}
.item-icon{width:36px;height:36px;border-radius:var(--radius);display:flex;align-items:center;justify-content:center;flex-shrink:0;font-size:16px}
.item-icon.login{background:#175DDC33;color:var(--primary-light)}
.item-icon.card{background:#51a95b33;color:var(--success)}
.item-icon.identity{background:#cf3d3d33;color:#e87878}
.item-icon.note{background:#d4a72c33;color:#d4a72c}
.item-info{flex:1;min-width:0}
.item-name{font-weight:500;font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item-sub{font-size:12px;color:var(--text-sec);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.item-actions{display:flex;gap:2px;opacity:0;transition:opacity var(--transition)}
.item-row:hover .item-actions{opacity:1}
.empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;color:var(--text-muted);gap:12px;padding:40px}
.empty-state svg{width:48px;height:48px;opacity:.3}

/* Detail panel */
.detail-header{padding:20px;border-bottom:1px solid var(--border)}
.detail-header .item-icon{width:48px;height:48px;font-size:22px;margin-bottom:12px}
.detail-title{font-size:18px;font-weight:600;margin-bottom:4px}
.detail-subtitle{font-size:13px;color:var(--text-sec)}
.detail-fields{padding:16px 20px}
.detail-field{margin-bottom:16px}
.detail-field-label{font-size:11px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:.5px;margin-bottom:4px}
.detail-field-value{display:flex;align-items:center;gap:8px;font-size:14px;word-break:break-all}
.detail-field-value.mono{font-family:var(--mono);font-size:13px}
.detail-field-value .masked{color:var(--text-sec);letter-spacing:2px}
.detail-actions{padding:16px 20px;border-top:1px solid var(--border);display:flex;gap:8px}

/* Unlock screen */
.unlock-screen{display:flex;align-items:center;justify-content:center;height:100vh;background:var(--bg)}
.unlock-card{width:380px;background:var(--bg-card);border-radius:var(--radius-lg);padding:40px 32px;box-shadow:var(--shadow);text-align:center}
.unlock-logo{margin-bottom:24px}
.unlock-logo svg{width:48px;height:48px}
.unlock-title{font-size:22px;font-weight:700;margin-bottom:4px}
.unlock-subtitle{color:var(--text-sec);font-size:13px;margin-bottom:28px}
.unlock-card .form-group{text-align:left}
.unlock-card .btn-primary{width:100%;padding:11px;font-size:14px;margin-top:8px}
.unlock-error{color:var(--danger);font-size:13px;margin-top:12px;display:none}
.unlock-error.show{display:block}

/* Generator */
.gen-container{padding:24px;max-width:480px}
.gen-container h2{font-size:18px;margin-bottom:20px}
.gen-preview{display:flex;gap:8px;margin-bottom:24px}
.gen-preview input{flex:1;font-family:var(--mono);font-size:14px;padding:10px 14px;background:var(--bg-input);border:1px solid var(--border);border-radius:var(--radius);color:var(--text);outline:none}
.gen-slider-row{display:flex;align-items:center;gap:12px;margin-bottom:20px}
.gen-slider-row label{font-size:13px;min-width:90px}
.gen-slider-row input[type=range]{flex:1;accent-color:var(--primary)}
.gen-slider-row .len-val{font-size:13px;min-width:30px;text-align:right;color:var(--primary-light);font-weight:600}
.gen-checks{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:20px}
.gen-check{display:flex;align-items:center;gap:8px;font-size:13px;cursor:pointer;padding:8px 12px;background:var(--bg-input);border-radius:var(--radius);transition:background var(--transition)}
.gen-check:hover{background:var(--bg-hover)}
.gen-check input{accent-color:var(--primary)}

/* Settings */
.settings-container{padding:24px;max-width:480px}
.settings-container h2{font-size:18px;margin-bottom:20px}
.settings-section{margin-bottom:24px}
.settings-section h3{font-size:14px;color:var(--text-sec);margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid var(--border)}
.settings-row{display:flex;justify-content:space-between;align-items:center;padding:10px 0}
.settings-row span{font-size:13px}
.toggle{position:relative;width:40px;height:22px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0}
.toggle-slider{position:absolute;inset:0;background:var(--bg-input);border-radius:11px;cursor:pointer;transition:all var(--transition)}
.toggle-slider::before{content:'';position:absolute;width:16px;height:16px;left:3px;top:3px;background:var(--text-sec);border-radius:50%;transition:all var(--transition)}
.toggle input:checked+.toggle-slider{background:var(--primary)}
.toggle input:checked+.toggle-slider::before{transform:translateX(18px);background:#fff}
.about-info{text-align:center;padding:32px 0;color:var(--text-sec);font-size:13px;line-height:1.8}
.about-info strong{color:var(--text);font-size:15px}

/* Log panel */
.log-panel{height:0;overflow:hidden;background:var(--bg-alt);border-top:1px solid var(--border);transition:height .2s ease;flex-shrink:0}
.log-panel.open{height:140px}
.log-content{height:100%;overflow-y:auto;padding:8px 12px;font-family:var(--mono);font-size:11px;color:var(--text-muted);white-space:pre-wrap}
.log-header{display:flex;align-items:center;justify-content:space-between;padding:4px 12px;font-size:11px;color:var(--text-muted);text-transform:uppercase;letter-spacing:.5px;border-bottom:1px solid var(--border);cursor:pointer}
.log-header:hover{background:var(--bg-hover)}

/* Dialogs */
.modal-overlay{position:fixed;inset:0;background:rgba(0,0,0,.6);display:flex;align-items:center;justify-content:center;z-index:100;opacity:0;transition:opacity .15s;pointer-events:none}
.modal-overlay.open{opacity:1;pointer-events:all}
.modal{background:var(--bg-card);border-radius:var(--radius-lg);width:440px;max-height:80vh;overflow-y:auto;box-shadow:var(--shadow);transform:scale(.95);transition:transform .15s}
.modal-overlay.open .modal{transform:scale(1)}
.modal-header{display:flex;align-items:center;justify-content:space-between;padding:16px 20px;border-bottom:1px solid var(--border)}
.modal-header h3{font-size:16px;font-weight:600}
.modal-body{padding:20px}
.modal-footer{display:flex;justify-content:flex-end;gap:8px;padding:12px 20px;border-top:1px solid var(--border)}

/* Toast */
.toast{position:fixed;bottom:20px;right:20px;background:var(--bg-card);border:1px solid var(--border);border-radius:var(--radius);padding:10px 16px;font-size:13px;box-shadow:var(--shadow);z-index:200;transform:translateY(100px);opacity:0;transition:all .25s ease}
.toast.show{transform:translateY(0);opacity:1}
.toast.success{border-left:3px solid var(--success)}
.toast.error{border-left:3px solid var(--danger)}

/* Animations */
@keyframes fadeIn{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}
.item-row{animation:fadeIn .15s ease both}
.item-row:nth-child(n+2){animation-delay:20ms}
.item-row:nth-child(n+3){animation-delay:40ms}

/* Responsive splitter (when detail is open, list gets narrower) */
@media (max-width:900px){.sidebar{width:180px}.detail-panel{width:280px}}
</style>
</head>
)VBHTML"
// --- Chunk 2: HTML Body ---
R"VBHTML(<body>

<div id="app">
  <!-- Unlock Screen -->
  <div id="unlock-screen" class="unlock-screen">
    <div class="unlock-card">
      <div class="unlock-logo">
        <svg viewBox="0 0 48 48" fill="none"><rect x="8" y="20" width="32" height="24" rx="4" fill="#175DDC"/><path d="M16 20V14a8 8 0 1116 0v6" stroke="#175DDC" stroke-width="3" fill="none"/><circle cx="24" cy="32" r="3" fill="#fff"/><path d="M24 35v3" stroke="#fff" stroke-width="2" stroke-linecap="round"/></svg>
      </div>
      <div class="unlock-title">VaultBox</div>
      <div class="unlock-subtitle">Offline Password Manager</div>
      <div class="form-group">
        <label>Email Address</label>
        <input type="email" id="unlock-email" class="form-input" placeholder="you@example.com" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Master Password</label>
        <input type="password" id="unlock-password" class="form-input" placeholder="Enter your master password" autocomplete="off">
      </div>
      <button class="btn btn-primary" id="unlock-btn" onclick="doUnlock()">Unlock</button>
      <div class="unlock-error" id="unlock-error"></div>
      <div style="margin-top:20px;font-size:12px;color:var(--text-muted)">
        Server running at 127.0.0.1:8787
      </div>
    </div>
  </div>

  <!-- Main App -->
  <div id="main-app" style="display:none;height:100vh;flex-direction:column">
    <!-- Top Bar -->
    <div class="topbar">
      <div class="topbar-logo">
        <svg viewBox="0 0 24 24" fill="none"><rect x="4" y="10" width="16" height="12" rx="2" fill="#175DDC"/><path d="M8 10V7a4 4 0 118 0v3" stroke="#175DDC" stroke-width="1.5" fill="none"/><circle cx="12" cy="16" r="1.5" fill="#fff"/></svg>
        VaultBox
      </div>
      <div class="topbar-actions">
        <button class="btn-icon" onclick="toggleLog()" title="Toggle log"><svg width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M14 1a1 1 0 011 1v8a1 1 0 01-1 1H4.414A2 2 0 003 11.586l-2 2V2a1 1 0 011-1h12zM2 0a2 2 0 00-2 2v12.793a.5.5 0 00.854.353l2.853-2.853A1 1 0 014.414 12H14a2 2 0 002-2V2a2 2 0 00-2-2H2z"/></svg></button>
        <button class="btn-icon" onclick="lockVault()" title="Lock vault"><svg width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M8 1a2 2 0 012 2v4H6V3a2 2 0 012-2zm3 6V3a3 3 0 00-6 0v4a2 2 0 00-2 2v5a2 2 0 002 2h6a2 2 0 002-2V9a2 2 0 00-2-2z"/></svg></button>
        <button class="btn-icon" onclick="appCommand('minimize')" title="Minimize"><svg width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M4 8h8v1H4z"/></svg></button>
      </div>
    </div>

    <div class="main">
      <!-- Sidebar -->
      <div class="sidebar" id="sidebar">
        <div class="nav-section">
          <div class="nav-item active" data-filter="all" onclick="setFilter('all',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M1 2.5A1.5 1.5 0 012.5 1h3A1.5 1.5 0 017 2.5v3A1.5 1.5 0 015.5 7h-3A1.5 1.5 0 011 5.5v-3zm8 0A1.5 1.5 0 0110.5 1h3A1.5 1.5 0 0115 2.5v3A1.5 1.5 0 0113.5 7h-3A1.5 1.5 0 019 5.5v-3zm-8 8A1.5 1.5 0 012.5 9h3A1.5 1.5 0 017 10.5v3A1.5 1.5 0 015.5 15h-3A1.5 1.5 0 011 13.5v-3zm8 0A1.5 1.5 0 0110.5 9h3a1.5 1.5 0 011.5 1.5v3a1.5 1.5 0 01-1.5 1.5h-3A1.5 1.5 0 019 13.5v-3z"/></svg>
            All Items <span class="count" id="count-all">0</span>
          </div>
          <div class="nav-item" data-filter="favorites" onclick="setFilter('favorites',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M3.612 15.443c-.386.198-.824-.149-.746-.592l.83-4.73L.173 6.765c-.329-.314-.158-.888.283-.95l4.898-.696L7.538.792c.197-.39.73-.39.927 0l2.184 4.327 4.898.696c.441.062.612.636.282.95l-3.522 3.356.83 4.73c.078.443-.36.79-.746.592L8 13.187l-4.389 2.256z"/></svg>
            Favorites <span class="count" id="count-fav">0</span>
          </div>
        </div>
        <div class="nav-section">
          <div class="nav-section-title">Types</div>
          <div class="nav-item" data-filter="type-1" onclick="setFilter('type-1',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M0 8a4 4 0 014-4h8a4 4 0 110 8H4a4 4 0 01-4-4zm4-3a3 3 0 000 6h8a3 3 0 100-6H4z"/><path d="M4 8a1 1 0 011-1h6a1 1 0 110 2H5a1 1 0 01-1-1z"/></svg>
            Logins <span class="count" id="count-login">0</span>
          </div>
          <div class="nav-item" data-filter="type-3" onclick="setFilter('type-3',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M0 4a2 2 0 012-2h12a2 2 0 012 2v8a2 2 0 01-2 2H2a2 2 0 01-2-2V4zm2-1a1 1 0 00-1 1v1h14V4a1 1 0 00-1-1H2zm13 4H1v5a1 1 0 001 1h12a1 1 0 001-1V7zm-5 2h3a.5.5 0 010 1h-3a.5.5 0 010-1z"/></svg>
            Cards <span class="count" id="count-card">0</span>
          </div>
          <div class="nav-item" data-filter="type-4" onclick="setFilter('type-4',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M3 14s-1 0-1-1 1-4 6-4 6 3 6 4-1 1-1 1H3zm5-6a3 3 0 100-6 3 3 0 000 6z"/></svg>
            Identities <span class="count" id="count-ident">0</span>
          </div>
          <div class="nav-item" data-filter="type-2" onclick="setFilter('type-2',this)">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M5 0h8a2 2 0 012 2v10a2 2 0 01-2 2H5a2 2 0 01-2-2V2a2 2 0 012-2zm0 1a1 1 0 00-1 1v10a1 1 0 001 1h8a1 1 0 001-1V2a1 1 0 00-1-1H5z"/><path d="M2 3h1v10a2 2 0 002 2h7v1H5a3 3 0 01-3-3V3z"/></svg>
            Secure Notes <span class="count" id="count-note">0</span>
          </div>
        </div>
        <div class="nav-section">
          <div class="nav-section-title" style="display:flex;align-items:center;justify-content:space-between">
            Folders
            <button class="btn-icon" onclick="showFolderDialog()" title="Add folder" style="width:20px;height:20px">
              <svg width="12" height="12" fill="currentColor" viewBox="0 0 16 16"><path d="M8 4a.5.5 0 01.5.5v3h3a.5.5 0 010 1h-3v3a.5.5 0 01-1 0v-3h-3a.5.5 0 010-1h3v-3A.5.5 0 018 4z"/></svg>
            </button>
          </div>
          <div id="folder-list"></div>
        </div>
        <div class="nav-section" style="margin-top:auto;border-top:1px solid var(--border);padding-top:4px">
          <div class="nav-item" onclick="showView('generator')">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M5.338 1.59a61.44 61.44 0 00-2.837.856.481.481 0 00-.328.39c-.554 4.157.726 7.19 2.253 9.188a10.725 10.725 0 002.287 2.233c.346.244.652.42.893.533.12.057.218.095.293.118a.55.55 0 00.101.025.615.615 0 00.1-.025c.076-.023.174-.061.294-.118.24-.113.547-.29.893-.533a10.726 10.726 0 002.287-2.233c1.527-1.997 2.807-5.031 2.253-9.188a.48.48 0 00-.328-.39c-.651-.213-1.75-.56-2.837-.855C9.552 1.29 8.531 1.067 8 1.067c-.53 0-1.552.223-2.662.524z"/></svg>
            Generator
          </div>
          <div class="nav-item" onclick="showView('settings')">
            <svg viewBox="0 0 16 16" fill="currentColor"><path d="M8 4.754a3.246 3.246 0 100 6.492 3.246 3.246 0 000-6.492zM5.754 8a2.246 2.246 0 114.492 0 2.246 2.246 0 01-4.492 0z"/><path d="M9.796 1.343c-.527-1.79-3.065-1.79-3.592 0l-.094.319a.873.873 0 01-1.255.52l-.292-.16c-1.64-.892-3.433.902-2.54 2.541l.159.292a.873.873 0 01-.52 1.255l-.319.094c-1.79.527-1.79 3.065 0 3.592l.319.094a.873.873 0 01.52 1.255l-.16.292c-.892 1.64.901 3.434 2.541 2.54l.292-.159a.873.873 0 011.255.52l.094.319c.527 1.79 3.065 1.79 3.592 0l.094-.319a.873.873 0 011.255-.52l.292.16c1.64.892 3.434-.902 2.54-2.541l-.159-.292a.873.873 0 01.52-1.255l.319-.094c1.79-.527 1.79-3.065 0-3.592l-.319-.094a.873.873 0 01-.52-1.255l.16-.292c.893-1.64-.902-3.433-2.541-2.54l-.292.159a.873.873 0 01-1.255-.52l-.094-.319z"/></svg>
            Settings
          </div>
        </div>
      </div>

      <!-- Content Area -->
      <div class="content" id="content-area">
        <!-- Vault View (default) -->
        <div id="view-vault" style="display:flex;flex-direction:column;flex:1;overflow:hidden">
          <div class="list-header">
            <div class="search-box">
              <svg viewBox="0 0 16 16" fill="currentColor"><path d="M11.742 10.344a6.5 6.5 0 10-1.397 1.398h-.001c.03.04.062.078.098.115l3.85 3.85a1 1 0 001.415-1.414l-3.85-3.85a1.007 1.007 0 00-.115-.1zM12 6.5a5.5 5.5 0 11-11 0 5.5 5.5 0 0111 0z"/></svg>
              <input type="text" placeholder="Search vault..." id="search-input" oninput="filterItems()">
            </div>
            <button class="btn btn-primary btn-sm" onclick="showEntryDialog()">
              <svg width="12" height="12" fill="currentColor" viewBox="0 0 16 16"><path d="M8 4a.5.5 0 01.5.5v3h3a.5.5 0 010 1h-3v3a.5.5 0 01-1 0v-3h-3a.5.5 0 010-1h3v-3A.5.5 0 018 4z"/></svg>
              Add Item
            </button>
          </div>
          <div class="item-list" id="item-list"></div>
        </div>

        <!-- Generator View -->
        <div id="view-generator" style="display:none;overflow-y:auto">
          <div class="gen-container">
            <h2>Password Generator</h2>
            <div class="gen-preview">
              <input type="text" id="gen-output" readonly>
              <button class="btn btn-secondary" onclick="copyGenerated()">Copy</button>
              <button class="btn btn-primary" onclick="regenerate()">Generate</button>
            </div>
            <div class="gen-slider-row">
              <label>Length</label>
              <input type="range" id="gen-length" min="4" max="128" value="20" oninput="updateGenLength()">
              <span class="len-val" id="gen-length-val">20</span>
            </div>
            <div class="gen-checks">
              <label class="gen-check"><input type="checkbox" id="gen-upper" checked onchange="regenerate()"> Uppercase (A-Z)</label>
              <label class="gen-check"><input type="checkbox" id="gen-lower" checked onchange="regenerate()"> Lowercase (a-z)</label>
              <label class="gen-check"><input type="checkbox" id="gen-digits" checked onchange="regenerate()"> Numbers (0-9)</label>
              <label class="gen-check"><input type="checkbox" id="gen-symbols" checked onchange="regenerate()"> Symbols (!@#$)</label>
              <label class="gen-check"><input type="checkbox" id="gen-ambiguous" onchange="regenerate()"> Ambiguous (0Ol1I|)</label>
            </div>
          </div>
        </div>

        <!-- Settings View -->
        <div id="view-settings" style="display:none;overflow-y:auto">
          <div class="settings-container">
            <h2>Settings</h2>
            <div class="settings-section">
              <h3>Application</h3>
              <div class="settings-row">
                <span>Start at login</span>
                <label class="toggle"><input type="checkbox" id="startup-toggle" onchange="toggleStartup(this.checked)"><span class="toggle-slider"></span></label>
              </div>
            </div>
            <div class="settings-section">
              <h3>Import</h3>
              <div style="display:flex;flex-wrap:wrap;gap:8px">
                <button class="btn btn-secondary" onclick="doImport('bitwarden_json')">Bitwarden JSON</button>
                <button class="btn btn-secondary" onclick="doImport('bitwarden_csv')">Bitwarden CSV</button>
                <button class="btn btn-secondary" onclick="doImport('chrome_csv')">Chrome CSV</button>
                <button class="btn btn-secondary" onclick="doImport('keepass_xml')">KeePass XML</button>
              </div>
            </div>
            <div class="settings-section">
              <h3>Export</h3>
              <div style="display:flex;gap:8px">
                <button class="btn btn-secondary" onclick="doExport('json')">Bitwarden JSON</button>
                <button class="btn btn-secondary" onclick="doExport('csv')">CSV</button>
              </div>
            </div>
            <div class="settings-section">
              <h3>Vault</h3>
              <div style="display:flex;gap:8px">
                <button class="btn btn-secondary" onclick="lockVault()">Lock Vault</button>
                <button class="btn btn-secondary" onclick="appCommand('opendata')">Open Data Folder</button>
              </div>
            </div>
            <div class="about-info">
              <strong>VaultBox Desktop</strong><br>
              Version <span id="about-version">0.5.0</span><br>
              Offline Bitwarden-compatible password manager<br>
              Server: 127.0.0.1:8787<br><br>
              <span style="font-size:11px">Encryption: AES-256-CBC + HMAC-SHA256<br>
              Key derivation: PBKDF2-SHA256 (600K iterations)<br>
              All data stored locally. Zero cloud dependency.</span>
            </div>
          </div>
        </div>
      </div>

      <!-- Detail Panel -->
      <div class="detail-panel" id="detail-panel">
        <div class="detail-header" id="detail-header"></div>
        <div class="detail-fields" id="detail-fields"></div>
        <div class="detail-actions" id="detail-actions"></div>
      </div>
    </div>

    <!-- Log Panel -->
    <div class="log-panel" id="log-panel">
      <div class="log-header" onclick="toggleLog()">
        <span>Server Log</span>
        <svg width="12" height="12" fill="currentColor" viewBox="0 0 16 16"><path d="M4.646 4.646a.5.5 0 01.708 0L8 7.293l2.646-2.647a.5.5 0 01.708.708L8.707 8l2.647 2.646a.5.5 0 01-.708.708L8 8.707l-2.646 2.647a.5.5 0 01-.708-.708L7.293 8 4.646 5.354a.5.5 0 010-.708z"/></svg>
      </div>
      <div class="log-content" id="log-content"></div>
    </div>
  </div>
</div>

<!-- Modal overlay -->
<div class="modal-overlay" id="modal-overlay" onclick="if(event.target===this)closeModal()">
  <div class="modal" id="modal-content"></div>
</div>

<!-- Toast -->
<div class="toast" id="toast"></div>

<!-- Hidden file input for imports -->
<input type="file" id="file-input" style="display:none">
)VBHTML"
// --- Chunk 3: JavaScript (state, API, rendering) ---
R"VBHTML(
<script>
// State
let vault = { entries: [], folders: [] };
let currentFilter = 'all';
let currentFolderId = null;
let selectedEntryId = null;
let currentView = 'vault';
let showPasswords = {};
let logVisible = false;

const API = '';

// =====================================================================
// API Helpers
// =====================================================================
async function api(path, opts = {}) {
  const res = await fetch(API + path, {
    headers: { 'Content-Type': 'application/json' },
    ...opts
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.message || err.Message || `HTTP ${res.status}`);
  }
  const text = await res.text();
  return text ? JSON.parse(text) : {};
}

// =====================================================================
// Toast
// =====================================================================
function toast(msg, type = 'success') {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = 'toast show ' + type;
  clearTimeout(t._timer);
  t._timer = setTimeout(() => t.className = 'toast', 3000);
}

// =====================================================================
// Unlock
// =====================================================================
async function doUnlock() {
  const email = document.getElementById('unlock-email').value.trim();
  const pass = document.getElementById('unlock-password').value;
  const errEl = document.getElementById('unlock-error');
  const btn = document.getElementById('unlock-btn');

  if (!email || !pass) { errEl.textContent = 'Enter email and password'; errEl.classList.add('show'); return; }

  btn.disabled = true;
  btn.textContent = 'Unlocking...';
  errEl.classList.remove('show');

  try {
    await api('/api/vaultbox/unlock', { method: 'POST', body: JSON.stringify({ email, password: pass }) });
    await loadVault();
    document.getElementById('unlock-screen').style.display = 'none';
    document.getElementById('main-app').style.display = 'flex';
  } catch (e) {
    errEl.textContent = e.message || 'Failed to unlock vault';
    errEl.classList.add('show');
  } finally {
    btn.disabled = false;
    btn.textContent = 'Unlock';
  }
}

// Enter key on password field
document.getElementById('unlock-password').addEventListener('keydown', e => { if (e.key === 'Enter') doUnlock(); });

// =====================================================================
// Load vault data
// =====================================================================
async function loadVault() {
  const data = await api('/api/vaultbox/vault');
  vault.entries = data.entries || [];
  vault.folders = data.folders || [];
  updateCounts();
  renderFolders();
  renderItems();
}

function updateCounts() {
  const e = vault.entries;
  document.getElementById('count-all').textContent = e.length;
  document.getElementById('count-fav').textContent = e.filter(x => x.favorite).length;
  document.getElementById('count-login').textContent = e.filter(x => x.type === 1).length;
  document.getElementById('count-card').textContent = e.filter(x => x.type === 3).length;
  document.getElementById('count-ident').textContent = e.filter(x => x.type === 4).length;
  document.getElementById('count-note').textContent = e.filter(x => x.type === 2).length;
}

// =====================================================================
// Sidebar
// =====================================================================
function setFilter(filter, el) {
  currentFilter = filter;
  currentFolderId = null;
  if (filter.startsWith('folder-')) {
    currentFolderId = filter.substring(7);
  }
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
  if (el) el.classList.add('active');
  showView('vault');
  renderItems();
  closeDetail();
}

function renderFolders() {
  const list = document.getElementById('folder-list');
  list.innerHTML = vault.folders.map(f =>
    `<div class="nav-item nav-folder" data-filter="folder-${f.id}" onclick="setFilter('folder-${f.id}',this)">
      <svg viewBox="0 0 16 16" fill="currentColor" width="14" height="14"><path d="M.54 3.87L.5 3a2 2 0 012-2h3.672a2 2 0 011.414.586l.828.828A2 2 0 009.828 3H13.5a2 2 0 012 2v.054l-.008.108A1 1 0 0015 5.5V14a2 2 0 01-2 2H3a2 2 0 01-2-2V5.5a1 1 0 00-.46-.842z"/></svg>
      <span style="flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${esc(f.name)}</span>
      <div class="folder-actions">
        <button class="btn-icon" onclick="event.stopPropagation();showFolderDialog('${f.id}','${esc(f.name)}')" style="width:20px;height:20px" title="Rename">
          <svg width="10" height="10" fill="currentColor" viewBox="0 0 16 16"><path d="M12.146.146a.5.5 0 01.708 0l3 3a.5.5 0 010 .708l-10 10a.5.5 0 01-.168.11l-5 2a.5.5 0 01-.65-.65l2-5a.5.5 0 01.11-.168l10-10z"/></svg>
        </button>
        <button class="btn-icon" onclick="event.stopPropagation();deleteFolder('${f.id}')" style="width:20px;height:20px" title="Delete">
          <svg width="10" height="10" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 016 6v6a.5.5 0 01-1 0V6a.5.5 0 01.5-.5zm2.5 0a.5.5 0 01.5.5v6a.5.5 0 01-1 0V6a.5.5 0 01.5-.5zm3 .5a.5.5 0 00-1 0v6a.5.5 0 001 0V6z"/><path d="M14.5 3a1 1 0 01-1 1H13v9a2 2 0 01-2 2H5a2 2 0 01-2-2V4h-.5a1 1 0 01-1-1V2a1 1 0 011-1H5.5l1-1h3l1 1h2.5a1 1 0 011 1v1z"/></svg>
        </button>
      </div>
    </div>`
  ).join('');
}

// =====================================================================
// Item List
// =====================================================================
function getTypeClass(type) {
  return ['', 'login', 'note', 'card', 'identity'][type] || 'login';
}
function getTypeIcon(type) {
  const icons = {
    1: '<svg viewBox="0 0 16 16" fill="currentColor" width="18" height="18"><path d="M0 8a4 4 0 014-4h8a4 4 0 110 8H4a4 4 0 01-4-4zm4-3a3 3 0 000 6h8a3 3 0 100-6H4z"/><path d="M4 8a1 1 0 011-1h6a1 1 0 110 2H5a1 1 0 01-1-1z"/></svg>',
    2: '<svg viewBox="0 0 16 16" fill="currentColor" width="18" height="18"><path d="M5 0h8a2 2 0 012 2v10a2 2 0 01-2 2H5a2 2 0 01-2-2V2a2 2 0 012-2z"/></svg>',
    3: '<svg viewBox="0 0 16 16" fill="currentColor" width="18" height="18"><path d="M0 4a2 2 0 012-2h12a2 2 0 012 2v8a2 2 0 01-2 2H2a2 2 0 01-2-2V4z"/></svg>',
    4: '<svg viewBox="0 0 16 16" fill="currentColor" width="18" height="18"><path d="M3 14s-1 0-1-1 1-4 6-4 6 3 6 4-1 1-1 1H3zm5-6a3 3 0 100-6 3 3 0 000 6z"/></svg>'
  };
  return icons[type] || icons[1];
}
function getTypeName(type) {
  return ['', 'Login', 'Secure Note', 'Card', 'Identity'][type] || 'Item';
}

function getFilteredEntries() {
  let items = vault.entries;
  const q = document.getElementById('search-input')?.value?.toLowerCase() || '';

  if (currentFilter === 'favorites') items = items.filter(e => e.favorite);
  else if (currentFilter === 'type-1') items = items.filter(e => e.type === 1);
  else if (currentFilter === 'type-2') items = items.filter(e => e.type === 2);
  else if (currentFilter === 'type-3') items = items.filter(e => e.type === 3);
  else if (currentFilter === 'type-4') items = items.filter(e => e.type === 4);
  else if (currentFolderId) items = items.filter(e => e.folderId === currentFolderId);

  if (q) items = items.filter(e =>
    (e.name || '').toLowerCase().includes(q) ||
    (e.username || '').toLowerCase().includes(q) ||
    (e.uri || '').toLowerCase().includes(q)
  );
  return items;
}

function renderItems() {
  const items = getFilteredEntries();
  const list = document.getElementById('item-list');

  if (items.length === 0) {
    list.innerHTML = `<div class="empty-state">
      <svg viewBox="0 0 16 16" fill="currentColor"><path d="M8 1a2 2 0 012 2v4H6V3a2 2 0 012-2zm3 6V3a3 3 0 00-6 0v4a2 2 0 00-2 2v5a2 2 0 002 2h6a2 2 0 002-2V9a2 2 0 00-2-2z"/></svg>
      <div>No items found</div></div>`;
    return;
  }

  list.innerHTML = items.map(e => `
    <div class="item-row ${selectedEntryId === e.id ? 'selected' : ''}" onclick="selectEntry('${e.id}')">
      <div class="item-icon ${getTypeClass(e.type)}">${getTypeIcon(e.type)}</div>
      <div class="item-info">
        <div class="item-name">${esc(e.name || '(no name)')}</div>
        <div class="item-sub">${esc(e.username || e.uri || getTypeName(e.type))}</div>
      </div>
      <div class="item-actions">
        ${e.username ? `<button class="btn-icon" onclick="event.stopPropagation();copyText('${escAttr(e.username)}','Username')" title="Copy username"><svg width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M3 14s-1 0-1-1 1-4 6-4 6 3 6 4-1 1-1 1H3zm5-6a3 3 0 100-6 3 3 0 000 6z"/></svg></button>` : ''}
        ${e.password ? `<button class="btn-icon" onclick="event.stopPropagation();copyText('${escAttr(e.password)}','Password')" title="Copy password"><svg width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M8 1a2 2 0 012 2v4H6V3a2 2 0 012-2zm3 6V3a3 3 0 00-6 0v4a2 2 0 00-2 2v5a2 2 0 002 2h6a2 2 0 002-2V9a2 2 0 00-2-2z"/></svg></button>` : ''}
        ${e.uri ? `<button class="btn-icon" onclick="event.stopPropagation();launchUri('${escAttr(e.uri)}')" title="Launch"><svg width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M8.636 3.5a.5.5 0 00-.5-.5H1.5A1.5 1.5 0 000 4.5v10A1.5 1.5 0 001.5 16h10a1.5 1.5 0 001.5-1.5V7.864a.5.5 0 00-1 0V14.5a.5.5 0 01-.5.5h-10a.5.5 0 01-.5-.5v-10a.5.5 0 01.5-.5h6.636a.5.5 0 00.5-.5z"/><path d="M16 .5a.5.5 0 00-.5-.5h-5a.5.5 0 000 1h3.793L6.146 9.146a.5.5 0 10.708.708L15 1.707V5.5a.5.5 0 001 0v-5z"/></svg></button>` : ''}
      </div>
    </div>
  `).join('');
}

function filterItems() { renderItems(); }
)VBHTML"
// --- Chunk 4: JavaScript (detail panel, entry/folder CRUD) ---
R"VBHTML(

// =====================================================================
// Detail Panel
// =====================================================================
function selectEntry(id) {
  selectedEntryId = id;
  const entry = vault.entries.find(e => e.id === id);
  if (!entry) { closeDetail(); return; }

  showPasswords = {};
  const panel = document.getElementById('detail-panel');
  panel.classList.add('open');

  const folderName = entry.folderId ? (vault.folders.find(f => f.id === entry.folderId)?.name || '') : '';

  document.getElementById('detail-header').innerHTML = `
    <div class="item-icon ${getTypeClass(entry.type)}" style="width:48px;height:48px;font-size:22px">${getTypeIcon(entry.type)}</div>
    <div class="detail-title">${esc(entry.name || '(no name)')}</div>
    <div class="detail-subtitle">${esc(getTypeName(entry.type))}${folderName ? ' &middot; ' + esc(folderName) : ''}</div>
  `;

  let fields = '';
  if (entry.username) fields += detailField('Username', entry.username, 'username', false, true);
  if (entry.password) fields += detailField('Password', entry.password, 'password', true, true);
  if (entry.uri) fields += detailField('Website', entry.uri, 'uri', false, true, true);
  if (entry.notes) fields += `<div class="detail-field"><div class="detail-field-label">Notes</div><div class="detail-field-value" style="white-space:pre-wrap;font-size:13px">${esc(entry.notes)}</div></div>`;
  if (entry.updatedAt) fields += `<div class="detail-field"><div class="detail-field-label">Last Modified</div><div class="detail-field-value" style="font-size:12px;color:var(--text-sec)">${formatDate(entry.updatedAt)}</div></div>`;

  document.getElementById('detail-fields').innerHTML = fields;
  document.getElementById('detail-actions').innerHTML = `
    <button class="btn btn-secondary" onclick="showEntryDialog('${id}')"><svg width="12" height="12" fill="currentColor" viewBox="0 0 16 16"><path d="M12.146.146a.5.5 0 01.708 0l3 3a.5.5 0 010 .708l-10 10a.5.5 0 01-.168.11l-5 2a.5.5 0 01-.65-.65l2-5a.5.5 0 01.11-.168l10-10z"/></svg> Edit</button>
    <button class="btn btn-danger" onclick="deleteEntry('${id}')"><svg width="12" height="12" fill="currentColor" viewBox="0 0 16 16"><path d="M5.5 5.5A.5.5 0 016 6v6a.5.5 0 01-1 0V6a.5.5 0 01.5-.5zm2.5 0a.5.5 0 01.5.5v6a.5.5 0 01-1 0V6a.5.5 0 01.5-.5zm3 .5a.5.5 0 00-1 0v6a.5.5 0 001 0V6z"/><path d="M14.5 3a1 1 0 01-1 1H13v9a2 2 0 01-2 2H5a2 2 0 01-2-2V4h-.5a1 1 0 01-1-1V2a1 1 0 011-1H5.5l1-1h3l1 1h2.5a1 1 0 011 1v1z"/></svg> Delete</button>
  `;
  renderItems();
}

function detailField(label, value, key, isMasked, copyable, isLink) {
  const masked = isMasked && !showPasswords[key];
  const displayVal = masked ? '&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;' : esc(value);
  return `<div class="detail-field">
    <div class="detail-field-label">${label}</div>
    <div class="detail-field-value ${isMasked ? 'mono' : ''}">
      <span ${masked ? 'class="masked"' : ''}>${isLink && !masked ? `<a href="#" onclick="launchUri('${escAttr(value)}');return false">${displayVal}</a>` : displayVal}</span>
      <span style="margin-left:auto;display:flex;gap:2px">
        ${isMasked ? `<button class="btn-icon" onclick="togglePassField('${key}')" title="${masked ? 'Show' : 'Hide'}"><svg width="14" height="14" fill="currentColor" viewBox="0 0 16 16">${masked ? '<path d="M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 8 5.5S16 8 16 8zM1.173 8a13.133 13.133 0 011.66-2.043C4.12 4.668 5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.133 13.133 0 0114.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-5.168-2.457A13.134 13.134 0 011.172 8z"/><path d="M8 5.5a2.5 2.5 0 100 5 2.5 2.5 0 000-5zM4.5 8a3.5 3.5 0 117 0 3.5 3.5 0 01-7 0z"/>' : '<path d="M13.359 11.238C15.06 9.72 16 8 16 8s-3-5.5-8-5.5a7.028 7.028 0 00-2.79.588l.77.771A5.944 5.944 0 018 3.5c2.12 0 3.879 1.168 5.168 2.457A13.134 13.134 0 0114.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 1.755-.165.165-.337.328-.517.486l.708.709z"/><path d="M11.297 9.176a3.5 3.5 0 00-4.474-4.474l.823.823a2.5 2.5 0 012.829 2.829l.822.822zm-2.943 1.299l.822.822a3.5 3.5 0 01-4.474-4.474l.823.823a2.5 2.5 0 002.829 2.829z"/><path d="M3.35 5.47c-.18.16-.353.322-.518.487A13.134 13.134 0 001.172 8l.195.288c.335.48.83 1.12 1.465 1.755C4.121 11.332 5.881 12.5 8 12.5c.716 0 1.39-.133 2.02-.36l.77.772A7.029 7.029 0 018 13.5C3 13.5 0 8 0 8s.939-1.721 2.641-3.238l.708.709z"/><path d="M13.646 14.354l-12-12 .708-.708 12 12-.708.708z"/>'}</svg></button>` : ''}
        ${copyable ? `<button class="btn-icon" onclick="copyText('${escAttr(value)}','${label}')" title="Copy"><svg width="14" height="14" fill="currentColor" viewBox="0 0 16 16"><path d="M4 1.5H3a2 2 0 00-2 2V14a2 2 0 002 2h10a2 2 0 002-2V3.5a2 2 0 00-2-2h-1v1h1a1 1 0 011 1V14a1 1 0 01-1 1H3a1 1 0 01-1-1V3.5a1 1 0 011-1h1v-1z"/><path d="M9.5 1a.5.5 0 01.5.5v1a.5.5 0 01-.5.5h-3a.5.5 0 01-.5-.5v-1a.5.5 0 01.5-.5h3zm-3-1A1.5 1.5 0 005 1.5v1A1.5 1.5 0 006.5 4h3A1.5 1.5 0 0011 2.5v-1A1.5 1.5 0 009.5 0h-3z"/></svg></button>` : ''}
      </span>
    </div>
  </div>`;
}

function togglePassField(key) {
  showPasswords[key] = !showPasswords[key];
  if (selectedEntryId) selectEntry(selectedEntryId);
}

function closeDetail() {
  selectedEntryId = null;
  document.getElementById('detail-panel').classList.remove('open');
  renderItems();
}

// =====================================================================
// CRUD
// =====================================================================
function showEntryDialog(editId) {
  const entry = editId ? vault.entries.find(e => e.id === editId) : null;
  const isEdit = !!entry;
  const title = isEdit ? 'Edit Item' : 'Add Item';

  const folderOpts = vault.folders.map(f =>
    `<option value="${f.id}" ${entry?.folderId === f.id ? 'selected' : ''}>${esc(f.name)}</option>`
  ).join('');

  const html = `
    <div class="modal-header"><h3>${title}</h3><button class="btn-icon" onclick="closeModal()"><svg width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M4.646 4.646a.5.5 0 01.708 0L8 7.293l2.646-2.647a.5.5 0 01.708.708L8.707 8l2.647 2.646a.5.5 0 01-.708.708L8 8.707l-2.646 2.647a.5.5 0 01-.708-.708L7.293 8 4.646 5.354a.5.5 0 010-.708z"/></svg></button></div>
    <div class="modal-body">
      <div class="form-group"><label>Type</label>
        <select class="form-input" id="entry-type" onchange="toggleEntryFields()">
          <option value="1" ${(entry?.type||1)===1?'selected':''}>Login</option>
          <option value="2" ${entry?.type===2?'selected':''}>Secure Note</option>
          <option value="3" ${entry?.type===3?'selected':''}>Card</option>
          <option value="4" ${entry?.type===4?'selected':''}>Identity</option>
        </select>
      </div>
      <div class="form-group"><label>Name</label><input class="form-input" id="entry-name" value="${escAttr(entry?.name||'')}" placeholder="Item name"></div>
      <div id="entry-login-fields">
        <div class="form-group"><label id="entry-user-label">Username</label><input class="form-input" id="entry-username" value="${escAttr(entry?.username||'')}" placeholder="username"></div>
        <div class="form-group"><label id="entry-pass-label">Password</label>
          <div class="input-group">
            <input class="form-input" id="entry-password" type="password" value="${escAttr(entry?.password||'')}">
            <button class="btn btn-secondary btn-sm" onclick="toggleField('entry-password')">Show</button>
            <button class="btn btn-secondary btn-sm" onclick="genForField()">Gen</button>
          </div>
        </div>
        <div class="form-group"><label id="entry-uri-label">URI</label><input class="form-input" id="entry-uri" value="${escAttr(entry?.uri||'')}" placeholder="https://"></div>
      </div>
      <div class="form-group"><label>Folder</label>
        <select class="form-input" id="entry-folder">
          <option value="">No Folder</option>
          ${folderOpts}
        </select>
      </div>
      <div class="form-group"><label>Notes</label><textarea class="form-input" id="entry-notes" rows="3">${esc(entry?.notes||'')}</textarea></div>
    </div>
    <div class="modal-footer">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="saveEntry(${isEdit ? `'${editId}'` : 'null'})">${isEdit ? 'Save' : 'Create'}</button>
    </div>`;

  openModal(html);
  toggleEntryFields();
}

function toggleEntryFields() {
  const type = parseInt(document.getElementById('entry-type')?.value || '1');
  const loginFields = document.getElementById('entry-login-fields');
  if (!loginFields) return;

  loginFields.style.display = (type === 2) ? 'none' : 'block';

  const userLabel = document.getElementById('entry-user-label');
  const passLabel = document.getElementById('entry-pass-label');
  const uriLabel = document.getElementById('entry-uri-label');

  if (type === 3) {
    if (userLabel) userLabel.textContent = 'Cardholder Name';
    if (passLabel) passLabel.textContent = 'Card Number';
    if (uriLabel) uriLabel.textContent = 'Brand';
  } else if (type === 4) {
    if (userLabel) userLabel.textContent = 'Full Name';
    if (passLabel) passLabel.textContent = 'ID Number';
    if (uriLabel) uriLabel.textContent = 'Email';
  } else {
    if (userLabel) userLabel.textContent = 'Username';
    if (passLabel) passLabel.textContent = 'Password';
    if (uriLabel) uriLabel.textContent = 'URI';
  }
}

function toggleField(id) {
  const el = document.getElementById(id);
  if (el) el.type = el.type === 'password' ? 'text' : 'password';
}

async function genForField() {
  try {
    const data = await api('/api/vaultbox/generate', { method: 'POST', body: JSON.stringify({ length: 20, upper: true, lower: true, digits: true, symbols: true }) });
    document.getElementById('entry-password').value = data.password;
    document.getElementById('entry-password').type = 'text';
  } catch (e) { toast(e.message, 'error'); }
}

async function saveEntry(editId) {
  const entry = {
    type: parseInt(document.getElementById('entry-type').value),
    name: document.getElementById('entry-name').value,
    username: document.getElementById('entry-username')?.value || '',
    password: document.getElementById('entry-password')?.value || '',
    uri: document.getElementById('entry-uri')?.value || '',
    notes: document.getElementById('entry-notes').value,
    folderId: document.getElementById('entry-folder').value
  };

  try {
    if (editId) {
      await api(`/api/vaultbox/entry/${editId}`, { method: 'PUT', body: JSON.stringify(entry) });
    } else {
      await api('/api/vaultbox/entry', { method: 'POST', body: JSON.stringify(entry) });
    }
    closeModal();
    await loadVault();
    toast(editId ? 'Item updated' : 'Item created');
    if (editId) selectEntry(editId);
  } catch (e) { toast(e.message, 'error'); }
}

async function deleteEntry(id) {
  try {
    await api(`/api/vaultbox/entry/${id}`, { method: 'DELETE' });
    closeDetail();
    await loadVault();
    toast('Item deleted');
  } catch (e) { toast(e.message, 'error'); }
}

)VBHTML"
// --- Chunk 5: JavaScript (folders, settings, import/export, init) ---
R"VBHTML(// =====================================================================
// Folders
// =====================================================================
function showFolderDialog(editId, editName) {
  const isEdit = !!editId;
  const html = `
    <div class="modal-header"><h3>${isEdit ? 'Rename Folder' : 'New Folder'}</h3><button class="btn-icon" onclick="closeModal()"><svg width="16" height="16" fill="currentColor" viewBox="0 0 16 16"><path d="M4.646 4.646a.5.5 0 01.708 0L8 7.293l2.646-2.647a.5.5 0 01.708.708L8.707 8l2.647 2.646a.5.5 0 01-.708.708L8 8.707l-2.646 2.647a.5.5 0 01-.708-.708L7.293 8 4.646 5.354a.5.5 0 010-.708z"/></svg></button></div>
    <div class="modal-body">
      <div class="form-group"><label>Folder Name</label><input class="form-input" id="folder-name" value="${escAttr(editName||'')}" placeholder="My Folder"></div>
    </div>
    <div class="modal-footer">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="saveFolder(${isEdit ? `'${editId}'` : 'null'})">${isEdit ? 'Save' : 'Create'}</button>
    </div>`;
  openModal(html);
  setTimeout(() => document.getElementById('folder-name')?.focus(), 100);
}

async function saveFolder(editId) {
  const name = document.getElementById('folder-name').value.trim();
  if (!name) return;
  try {
    if (editId) {
      await api(`/api/vaultbox/folder/${editId}`, { method: 'PUT', body: JSON.stringify({ name }) });
    } else {
      await api('/api/vaultbox/folder', { method: 'POST', body: JSON.stringify({ name }) });
    }
    closeModal();
    await loadVault();
    toast(editId ? 'Folder renamed' : 'Folder created');
  } catch (e) { toast(e.message, 'error'); }
}

async function deleteFolder(id) {
  try {
    await api(`/api/vaultbox/folder/${id}`, { method: 'DELETE' });
    if (currentFolderId === id) setFilter('all', document.querySelector('[data-filter="all"]'));
    await loadVault();
    toast('Folder deleted');
  } catch (e) { toast(e.message, 'error'); }
}

// =====================================================================
// Generator
// =====================================================================
function updateGenLength() {
  document.getElementById('gen-length-val').textContent = document.getElementById('gen-length').value;
  regenerate();
}

async function regenerate() {
  const opts = {
    length: parseInt(document.getElementById('gen-length')?.value || 20),
    upper: document.getElementById('gen-upper')?.checked ?? true,
    lower: document.getElementById('gen-lower')?.checked ?? true,
    digits: document.getElementById('gen-digits')?.checked ?? true,
    symbols: document.getElementById('gen-symbols')?.checked ?? true,
    ambiguous: document.getElementById('gen-ambiguous')?.checked ?? false
  };
  try {
    const data = await api('/api/vaultbox/generate', { method: 'POST', body: JSON.stringify(opts) });
    document.getElementById('gen-output').value = data.password;
  } catch (e) { toast(e.message, 'error'); }
}

function copyGenerated() {
  const val = document.getElementById('gen-output').value;
  if (val) copyText(val, 'Password');
}

// =====================================================================
// Import/Export
// =====================================================================
function doImport(type) {
  const accept = type.endsWith('json') ? '.json' : type.endsWith('xml') ? '.xml' : '.csv';
  const input = document.getElementById('file-input');
  input.accept = accept;
  input.onchange = async () => {
    const file = input.files[0];
    if (!file) return;
    const text = await file.text();
    try {
      const res = await api(`/api/vaultbox/import/${type}`, { method: 'POST', body: text });
      await loadVault();
      toast(`Imported ${res.count || 0} items`);
    } catch (e) { toast(e.message, 'error'); }
    input.value = '';
  };
  input.click();
}

async function doExport(type) {
  try {
    const res = await fetch(`/api/vaultbox/export/${type}`);
    if (!res.ok) throw new Error('Export failed');
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = type === 'json' ? 'vaultbox-export.json' : 'vaultbox-export.csv';
    a.click();
    URL.revokeObjectURL(url);
    toast('Export downloaded');
  } catch (e) { toast(e.message, 'error'); }
}

// =====================================================================
// Views
// =====================================================================
function showView(view) {
  currentView = view;
  document.getElementById('view-vault').style.display = view === 'vault' ? 'flex' : 'none';
  document.getElementById('view-generator').style.display = view === 'generator' ? 'block' : 'none';
  document.getElementById('view-settings').style.display = view === 'settings' ? 'block' : 'none';

  if (view === 'generator') regenerate();
  if (view === 'settings') loadStartupState();
  if (view !== 'vault') closeDetail();
}

async function loadStartupState() {
  try {
    const data = await api('/api/vaultbox/startup');
    document.getElementById('startup-toggle').checked = data.enabled;
  } catch (e) {}
}

async function toggleStartup(enabled) {
  try {
    const data = await api('/api/vaultbox/startup', { method: 'POST', body: JSON.stringify({ enabled }) });
    document.getElementById('startup-toggle').checked = data.enabled;
    toast(data.enabled ? 'Start at login enabled' : 'Start at login disabled');
  } catch (e) { toast(e.message, 'error'); }
}

// =====================================================================
// Log
// =====================================================================
function toggleLog() {
  logVisible = !logVisible;
  document.getElementById('log-panel').classList.toggle('open', logVisible);
}

async function pollLogs() {
  try {
    const data = await api('/api/vaultbox/logs');
    if (data.logs && data.logs.length > 0) {
      const el = document.getElementById('log-content');
      el.textContent += data.logs.join('\n') + '\n';
      el.scrollTop = el.scrollHeight;
    }
  } catch (e) {}
  setTimeout(pollLogs, 2000);
}

// =====================================================================
// Modal
// =====================================================================
function openModal(html) {
  document.getElementById('modal-content').innerHTML = html;
  document.getElementById('modal-overlay').classList.add('open');
}
function closeModal() {
  document.getElementById('modal-overlay').classList.remove('open');
}

// =====================================================================
// Lock / App Commands
// =====================================================================
async function lockVault() {
  try {
    await api('/api/vaultbox/lock', { method: 'POST' });
    vault = { entries: [], folders: [] };
    selectedEntryId = null;
    document.getElementById('main-app').style.display = 'none';
    document.getElementById('unlock-screen').style.display = 'flex';
    document.getElementById('unlock-password').value = '';
    document.getElementById('unlock-error').classList.remove('show');
  } catch (e) { toast(e.message, 'error'); }
}

function appCommand(cmd) {
  if (window.chrome && window.chrome.webview) {
    window.chrome.webview.postMessage({ command: cmd });
  }
}

function launchUri(uri) {
  if (!uri.startsWith('http://') && !uri.startsWith('https://')) uri = 'https://' + uri;
  appCommand('launch:' + uri);
  // Fallback: open in default browser via server
  api('/api/vaultbox/launch', { method: 'POST', body: JSON.stringify({ uri }) }).catch(() => {});
}

// =====================================================================
// Clipboard
// =====================================================================
async function copyText(text, label) {
  try {
    await navigator.clipboard.writeText(text);
    toast(`${label} copied`);
  } catch (e) {
    // Fallback
    const ta = document.createElement('textarea');
    ta.value = text;
    document.body.appendChild(ta);
    ta.select();
    document.execCommand('copy');
    document.body.removeChild(ta);
    toast(`${label} copied`);
  }
}

// =====================================================================
// Utilities
// =====================================================================
function esc(s) {
  if (!s) return '';
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
function escAttr(s) {
  if (!s) return '';
  return s.replace(/\\/g,'\\\\').replace(/'/g,"\\'").replace(/"/g,'\\"').replace(/\n/g,'\\n');
}
function formatDate(s) {
  if (!s) return '';
  try { return new Date(s).toLocaleString(); } catch(e) { return s; }
}

// =====================================================================
// Init
// =====================================================================
async function init() {
  try {
    const status = await api('/api/vaultbox/status');
    if (status.unlocked) {
      await loadVault();
      document.getElementById('unlock-screen').style.display = 'none';
      document.getElementById('main-app').style.display = 'flex';
    } else if (status.email) {
      document.getElementById('unlock-email').value = status.email;
    }
  } catch (e) {}
  pollLogs();
}

init();
</script>
</body>
</html>)VBHTML";
