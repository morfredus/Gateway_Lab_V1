/**
 * Lite Application JavaScript
 *
 * This is a simplified version for ESP32 Classic (limited memory).
 * It is automatically minified during build and embedded into the firmware.
 *
 * DO NOT EDIT web_interface.h directly - edit this file instead!
 *
 * To rebuild web_interface.h after making changes:
 *   python tools/minify_web.py
 */

function tr(k) {
    try {
        return (DEFAULT_TRANSLATIONS && DEFAULT_TRANSLATIONS[k]) || k;
    } catch (e) {
        return k;
    }
}

function esc(s) {
    return String(s ?? '').replace(/[&<>"']/g, c => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        '\'': '&#39;'
    } [c]));
}

function formatUptime(ms) {
    const s = Math.floor(ms / 1000),
        m = Math.floor(s / 60),
        h = Math.floor(m / 60),
        d = Math.floor(h / 24);
    return d + 'j ' + (h % 24) + 'h ' + (m % 60) + 'm';
}

function buildOverviewLite(d) {
    let h = '<div class="section">';
    h += '<h2>' + esc(tr('chip_info')) + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('full_model')) + '</div><div class="info-value">' + esc(d.chip.model) + ' ' + esc(tr('revision')) + ' ' + esc(d.chip.revision) + '</div></div>';
    const cpu = d.chip.cores + ' ' + esc(tr('cores')) + ' @ ' + d.chip.freq + ' MHz';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('cpu_cores')) + '</div><div class="info-value">' + cpu + '</div></div>';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('mac_wifi')) + '</div><div class="info-value">' + esc(d.chip.mac) + '</div></div>';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('uptime')) + '</div><div class="info-value" id="uptime">' + formatUptime(d.chip.uptime) + '</div></div>';
    h += '</div></div>';
    h += '<div class="section"><h2>' + esc(tr('memory_details')) + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('internal_sram')) + '</div><div class="info-value">' + (d.memory.sram.total / 1024).toFixed(2) + ' KB (' + (d.memory.sram.free / 1024).toFixed(2) + ' KB ' + esc(tr('free')) + ')</div></div>';
    if (d.memory.psram && d.memory.psram.total > 0) {
        h += '<div class="info-item"><div class="info-label">PSRAM</div><div class="info-value">' + (d.memory.psram.total / 1048576).toFixed(2) + ' MB</div></div>';
    }
    h += '</div></div>';
    h += '<div class="section"><h2>' + esc(tr('wifi_connection')) + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label">' + esc(tr('connected_ssid')) + '</div><div class="info-value">' + esc(d.wifi.ssid || '') + '</div></div>';
    h += '<div class="info-item"><div class="info-label">IP</div><div class="info-value">' + esc(d.wifi.ip || '') + '</div></div>';
    h += '</div></div>';
    return h;
}
async function loadLite() {
    try {
        const r = await fetch('/api/overview');
        const d = await r.json();
        const c = document.getElementById('overviewContainer');
        if (c) {
            c.innerHTML = buildOverviewLite(d);
        }
        startLiteAutoUpdate();
    } catch (e) {
        const c = document.getElementById('overviewContainer');
        if (c) {
            c.innerHTML = '<div class="section"><p>Erreur: ' + esc(String(e)) + '</p></div>';
        }
    }
}

function startLiteAutoUpdate() {
    setInterval(async () => {
        try {
            const r = await fetch('/api/status');
            const d = await r.json();
            const u = document.getElementById('uptime');
            if (u) u.textContent = formatUptime(d.uptime);
        } catch (e) {}
    }, 5000);
}
document.addEventListener('DOMContentLoaded', () => {
    loadLite();
});
